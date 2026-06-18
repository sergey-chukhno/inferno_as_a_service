#pragma once
#include <cstddef>

namespace inferno { namespace wrapper {

// 16-byte XOR key for compile-time string obfuscation.
// Distinct from the agent binary encryption key (wrapper-only).
constexpr unsigned char STR_XOR_KEY[16] = {
    0x4F, 0x8C, 0xD1, 0x3A, 0xB7, 0xE2, 0x65, 0x1D,
    0xF0, 0x0A, 0x93, 0x6B, 0xC4, 0x7E, 0x28, 0x55
};

template <size_t N>
struct Unlit {
    char data[N];
    bool decrypted;

    constexpr Unlit(const char(&s)[N])
        : data{}
        , decrypted(false)
    {
        for (size_t i = 0; i < N; ++i)
            data[i] = static_cast<char>(
                static_cast<unsigned char>(s[i])
                ^ STR_XOR_KEY[i % 16]
            );
    }

    const char* get() {
        if (!decrypted) {
            for (size_t i = 0; i < N; ++i)
                data[i] = static_cast<char>(
                    static_cast<unsigned char>(data[i])
                    ^ STR_XOR_KEY[i % 16]
                );
            decrypted = true;
        }
        return data;
    }
};

}} // namespace inferno::wrapper

// Wraps a string literal: returns const char* with runtime deobfuscation.
// The ciphertext is computed at compile time and stored in the binary's
// data section. The first call XORs it back to plaintext in-place.
// Note: -Wformat-security is suppressed at the CMake target level for
// this file because the string IS compile-time fixed — the warning is
// a false positive from the lambda wrapper. _Pragma cannot be used
// here because GCC does not allow it inside expression-expanding macros.
#define UNLIT(str)                                                     \
    ([]{                                                                \
        static auto _unlit_ =                                           \
            ::inferno::wrapper::Unlit<sizeof(str)>(str);               \
        return _unlit_.get();                                           \
    }())
