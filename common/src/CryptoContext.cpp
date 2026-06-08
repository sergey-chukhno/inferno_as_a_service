#include "../include/CryptoContext.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
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

std::vector<uint8_t> CryptoContext::encrypt(const std::vector<uint8_t>& plaintext) const {
    if (!m_initialized) {
        std::cerr << "[CryptoContext] encrypt: context not initialized.\n";
        return {};
    }

    // Allocate output: IV + ciphertext + tag
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

    // Encrypt plaintext
    uint8_t* ciphertext_start = out.data() + IV_SIZE;
    if (EVP_EncryptUpdate(ctx, ciphertext_start, &len,
                          plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertext_len = len;

    // Finalize
    if (EVP_EncryptFinal_ex(ctx, ciphertext_start + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertext_len += len;

    // Get GCM tag
    uint8_t* tag_start = out.data() + IV_SIZE + ciphertext_len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag_start) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);

    // Resize to actual size: IV + ciphertext + tag
    out.resize(IV_SIZE + static_cast<size_t>(ciphertext_len) + TAG_SIZE);
    return out;
}

std::vector<uint8_t> CryptoContext::decrypt(
    const std::vector<uint8_t>& ciphertext_with_iv_and_tag) const {

    if (!m_initialized) {
        std::cerr << "[CryptoContext] decrypt: context not initialized.\n";
        return {};
    }

    if (ciphertext_with_iv_and_tag.size() < IV_SIZE + TAG_SIZE) {
        std::cerr << "[CryptoContext] decrypt: payload too small.\n";
        return {};
    }

    size_t ct_len = ciphertext_with_iv_and_tag.size() - IV_SIZE - TAG_SIZE;

    // Allocate output buffer for plaintext
    std::vector<uint8_t> plaintext(ct_len + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintext_len = 0;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        std::cerr << "[CryptoContext] EVP_CIPHER_CTX_new failed.\n";
        return {};
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    const uint8_t* iv = ciphertext_with_iv_and_tag.data();
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, m_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    const uint8_t* ct = ciphertext_with_iv_and_tag.data() + IV_SIZE;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ct, static_cast<int>(ct_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    plaintext_len = len;

    // Set expected tag
    const uint8_t* tag = ciphertext_with_iv_and_tag.data() + IV_SIZE + ct_len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                            const_cast<uint8_t*>(tag)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Verify tag and finalize
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};  // Tag mismatch — data corrupted or tampered
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    plaintext.resize(static_cast<size_t>(plaintext_len));
    return plaintext;
}

} // namespace inferno
