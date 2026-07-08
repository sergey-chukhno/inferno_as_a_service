#include "../include/CryptoContext.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace inferno {

CryptoContext& CryptoContext::instance() {
    static CryptoContext ctx;
    return ctx;
}

void CryptoContext::init(const uint8_t* key, size_t key_len) {
    if (key_len != KEY_SIZE) {
        std::cerr << "[CryptoContext] Invalid key size: " << key_len
                  << " (expected " << KEY_SIZE << ")\n";
        return;
    }
    std::memcpy(m_key, key, KEY_SIZE);
    m_initialized = true;
}

void CryptoContext::initDefault() {
    // Static compiled-in key. Placeholder — upgrade to DH key exchange in Phase I-bis.
    // If an attacker can read this binary, the key is compromised.
    // This defeats passive DPI/network forensics but not active RE.
    static const uint8_t DEFAULT_KEY[KEY_SIZE] = {
        0x0F, 0x1E, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78,
        0x87, 0x96, 0xA5, 0xB4, 0xC3, 0xD2, 0xE1, 0xF0,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    init(DEFAULT_KEY, KEY_SIZE);
}

bool CryptoContext::isInitialized() const {
    return m_initialized;
}

std::vector<uint8_t> CryptoContext::encrypt(const std::vector<uint8_t>& plaintext,
                                           const std::vector<uint8_t>& aad) const {
    if (!m_initialized) {
        std::cerr << "[CryptoContext] encrypt: context not initialized.\n";
        return {};
    }
    std::vector<uint8_t> out(IV_SIZE + plaintext.size() + TAG_SIZE);

    // Generate random IV
    if (RAND_bytes(out.data(), IV_SIZE) != 1) {
        std::cerr << "[CryptoContext] RAND_bytes failed.\n";
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        std::cerr << "[CryptoContext] EVP_CIPHER_CTX_new failed.\n";
        return {};
    }

    int len = 0;
    int ciphertext_len = 0;

    // Init with AES-256-GCM
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Set IV length to 12
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Init with key and IV
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, m_key, out.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Process AAD (authenticated but not encrypted)
    if (!aad.empty()) {
        int aad_len = 0;
        if (EVP_EncryptUpdate(ctx, nullptr, &aad_len,
                              aad.data(), static_cast<int>(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return {};
        }
    }

    // Encrypt plaintext
    uint8_t* ciphertext_start = out.data() + IV_SIZE;
    {
        // Use a safe non-null pointer even for empty input — OpenSSL's GCM may
        // not fully initialize internal state when nullptr is passed with length 0.
        const uint8_t* pt = plaintext.empty() ? ciphertext_start : plaintext.data();
        int pt_len = static_cast<int>(plaintext.size());
        if (EVP_EncryptUpdate(ctx, ciphertext_start, &len, pt, pt_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return {};
        }
        ciphertext_len = len;
    }

    // Finalize (produces GCM tag even for empty plaintext)
    if (EVP_EncryptFinal_ex(ctx, ciphertext_start + ciphertext_len, &len) != 1) {
        std::cerr << "[CryptoContext] EVP_EncryptFinal_ex failed.\n";
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertext_len += len;

    // Get GCM tag
    uint8_t* tag_start = out.data() + IV_SIZE + ciphertext_len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag_start) != 1) {
        std::cerr << "[CryptoContext] EVP_CTRL_GCM_GET_TAG failed.\n";
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);

    // Resize to actual size: IV + ciphertext + tag
    out.resize(IV_SIZE + static_cast<size_t>(ciphertext_len) + TAG_SIZE);
    return out;
}

std::optional<std::vector<uint8_t>> CryptoContext::decrypt(
    const std::vector<uint8_t>& ciphertext_with_iv_and_tag,
    const std::vector<uint8_t>& aad) const {

    if (!m_initialized) {
        std::cerr << "[CryptoContext] decrypt: context not initialized.\n";
        return std::nullopt;
    }
    if (ciphertext_with_iv_and_tag.size() < IV_SIZE + TAG_SIZE) {
        std::cerr << "[CryptoContext] decrypt: payload too small.\n";
        return std::nullopt;
    }

    size_t ct_len = ciphertext_with_iv_and_tag.size() - IV_SIZE - TAG_SIZE;

    // Allocate output buffer for plaintext
    std::vector<uint8_t> plaintext(ct_len + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintext_len = 0;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        std::cerr << "[CryptoContext] EVP_CIPHER_CTX_new failed.\n";
        return std::nullopt;
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    const uint8_t* iv = ciphertext_with_iv_and_tag.data();
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, m_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    // Process AAD (authenticated but not encrypted)
    if (!aad.empty()) {
        int aad_len = 0;
        if (EVP_DecryptUpdate(ctx, nullptr, &aad_len,
                              aad.data(), static_cast<int>(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }
    }

    const uint8_t* ct = ciphertext_with_iv_and_tag.data() + IV_SIZE;
    {
        const uint8_t* safe_ct = (ct_len > 0) ? ct : plaintext.data();
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                              safe_ct, static_cast<int>(ct_len)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }
        plaintext_len = len;
    }

    // Set expected tag
    const uint8_t* tag = ciphertext_with_iv_and_tag.data() + IV_SIZE + ct_len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                            const_cast<uint8_t*>(tag)) != 1) {
        std::cerr << "[CryptoContext] EVP_CTRL_GCM_SET_TAG failed.\n";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    // Verify tag and finalize
    int final_ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + plaintext_len, &len);
    if (final_ret != 1) {
        std::cerr << "[CryptoContext] Tag mismatch.\n";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    plaintext.resize(static_cast<size_t>(plaintext_len));
    return plaintext;
}

// ── HMAC-SHA256 ───────────────────────────────────────────────

std::vector<uint8_t> CryptoContext::hmacSha256(const uint8_t* key, size_t key_len,
                                                 const uint8_t* data, size_t data_len) {
    unsigned int out_len = 0;
    std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
    if (::HMAC(EVP_sha256(), key, static_cast<int>(key_len),
               data, static_cast<int>(data_len),
               result.data(), &out_len) == nullptr) {
        return {};
    }
    result.resize(out_len);
    return result;
}

std::vector<uint8_t> CryptoContext::hmacSha256(const std::vector<uint8_t>& key,
                                                 const std::vector<uint8_t>& data) {
    return hmacSha256(key.data(), key.size(), data.data(), data.size());
}

// ── Session key derivation ────────────────────────────────────
// Compiled-in handshake secret — separate from the AES-GCM key.
namespace {
    static const uint8_t kHandshakeSecret[32] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };
}

std::vector<uint8_t> CryptoContext::deriveSessionKey(const uint8_t* greeting) {
    // session_key = HMAC-SHA256(handshake_secret, greeting)[:16]
    auto full = hmacSha256(kHandshakeSecret, sizeof(kHandshakeSecret),
                           greeting, GREETING_SIZE);
    if (full.size() < SESSION_KEY_SIZE) return {};
    full.resize(SESSION_KEY_SIZE);
    return full;
}

} // namespace inferno
