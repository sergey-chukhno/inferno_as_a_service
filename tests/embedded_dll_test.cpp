#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifdef INFERNO_HAS_EMBEDDED_DLL
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../client/include/EmbeddedDll.hpp"

// ── Embedded DLL Decryption Tests ─────────────────────────────────
// Verifies that the XOR-encrypted DLL embedded at build time decrypts
// to a valid PE image that the reflective loader can use.

void test_decrypted_dll_is_valid_pe() {
    std::vector<uint8_t> dll = inferno::embedded::decryptEmbeddedDll();

    if (dll.empty()) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_is_valid_pe: "
                             "decrypted DLL is empty\n");
        std::exit(1);
    }

    // Must start with "MZ" (IMAGE_DOS_SIGNATURE)
    if (dll.size() < sizeof(IMAGE_DOS_HEADER)) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_is_valid_pe: "
                             "too small for DOS header (%zu bytes)\n",
                     dll.size());
        std::exit(1);
    }

    const IMAGE_DOS_HEADER* dos =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(dll.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_is_valid_pe: "
                             "bad DOS magic 0x%04x\n",
                     dos->e_magic);
        std::exit(1);
    }

    // NT headers must be within bounds and have valid signature
    if (dos->e_lfanew + static_cast<LONG>(sizeof(IMAGE_NT_HEADERS)) >
        static_cast<LONG>(dll.size())) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_is_valid_pe: "
                             "NT headers out of bounds (e_lfanew=%ld)\n",
                     static_cast<long>(dos->e_lfanew));
        std::exit(1);
    }

    const IMAGE_NT_HEADERS* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(
            dll.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_is_valid_pe: "
                             "bad NT signature 0x%08lx\n",
                     static_cast<unsigned long>(nt->Signature));
        std::exit(1);
    }

    // Must be x64 (matching the reflective loader's shellcode)
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_is_valid_pe: "
                             "unexpected machine 0x%04x (expected AMD64)\n",
                     nt->FileHeader.Machine);
        std::exit(1);
    }

    // Must have a non-zero entry point (it's a real DLL, not a resource-only PE)
    if (nt->OptionalHeader.AddressOfEntryPoint == 0) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_is_valid_pe: "
                             "entry point is zero\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_decrypted_dll_is_valid_pe "
                         "(%zu bytes, entry RVA 0x%x)\n",
                 dll.size(),
                 static_cast<unsigned>(nt->OptionalHeader.AddressOfEntryPoint));
}

void test_decrypted_dll_roundtrip_to_disk() {
    // Decrypt and write to a temp path, then verify the file is a valid PE
    const char* tmp_path = "embedded_dll_test_output.dll";
    if (!inferno::embedded::extractEmbeddedDllTo(tmp_path)) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_roundtrip_to_disk: "
                             "extractEmbeddedDllTo failed\n");
        std::exit(1);
    }

    // Read it back and verify
    std::FILE* f = std::fopen(tmp_path, "rb");
    if (!f) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_roundtrip_to_disk: "
                             "cannot open written file\n");
        std::exit(1);
    }

    std::fseek(f, 0, SEEK_END);
    long file_size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (file_size < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_roundtrip_to_disk: "
                             "written file too small (%ld bytes)\n", file_size);
        std::fclose(f);
        std::remove(tmp_path);
        std::exit(1);
    }

    std::vector<uint8_t> on_disk(static_cast<size_t>(file_size));
    if (std::fread(on_disk.data(), 1, on_disk.size(), f) != on_disk.size()) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_roundtrip_to_disk: "
                             "readback failed\n");
        std::fclose(f);
        std::remove(tmp_path);
        std::exit(1);
    }
    std::fclose(f);
    std::remove(tmp_path);

    const IMAGE_DOS_HEADER* dos =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(on_disk.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        std::fprintf(stderr, "[FAIL] test_decrypted_dll_roundtrip_to_disk: "
                             "file on disk has bad DOS magic\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_decrypted_dll_roundtrip_to_disk "
                         "(%ld bytes written and verified)\n", file_size);
}

#endif // INFERNO_HAS_EMBEDDED_DLL
#endif // _WIN32

int main() {
#if defined(_WIN32) && defined(INFERNO_HAS_EMBEDDED_DLL)
    test_decrypted_dll_is_valid_pe();
    test_decrypted_dll_roundtrip_to_disk();
    std::fprintf(stdout, "[PASS] All embedded DLL tests passed\n");
#else
    std::fprintf(stdout, "[SKIP] Embedded DLL tests are Windows-only "
                         "(requires Python3 at build time)\n");
#endif
    return 0;
}
