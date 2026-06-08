#pragma once

#include <cstdint>
#include <vector>
#include <string>

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

    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext) const;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext_with_iv_and_tag) const;

private:
    CryptoContext() = default;
    ~CryptoContext() = default;
    CryptoContext(const CryptoContext&) = delete;
    CryptoContext& operator=(const CryptoContext&) = delete;

    uint8_t m_key[KEY_SIZE];
    bool    m_initialized = false;
};

} // namespace inferno
