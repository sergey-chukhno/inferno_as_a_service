#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace inferno {

class CryptoContext {
public:
    static constexpr size_t KEY_SIZE      = 32;  // AES-256
    static constexpr size_t IV_SIZE       = 12;  // GCM standard
    static constexpr size_t TAG_SIZE      = 16;  // GCM authentication tag
    static constexpr size_t OVERHEAD      = IV_SIZE + TAG_SIZE; // 28 bytes

    static CryptoContext& instance();

    void init(const uint8_t* key, size_t key_len);
    void initDefault();
    bool isInitialized() const;

    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext,
                                  const std::vector<uint8_t>& aad = {}) const;
    std::optional<std::vector<uint8_t>> decrypt(const std::vector<uint8_t>& ciphertext_with_iv_and_tag,
                                                  const std::vector<uint8_t>& aad = {}) const;

    // HMAC-SHA256 (uses OpenSSL, no external dependency)
    static std::vector<uint8_t> hmacSha256(const uint8_t* key, size_t key_len,
                                            const uint8_t* data, size_t data_len);
    static std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key,
                                            const std::vector<uint8_t>& data);

    // Session key negotiation: server sends 64 random bytes at connect,
    // both sides derive session_key = HMAC-SHA256(compiled_secret, greeting)
    static constexpr size_t GREETING_SIZE = 64;
    static constexpr size_t SESSION_KEY_SIZE = 16;
    static std::vector<uint8_t> deriveSessionKey(const uint8_t* greeting);

private:
    CryptoContext() = default;
    ~CryptoContext() = default;
    CryptoContext(const CryptoContext&) = delete;
    CryptoContext& operator=(const CryptoContext&) = delete;

    uint8_t m_key[KEY_SIZE];
    bool    m_initialized = false;
};

} // namespace inferno
