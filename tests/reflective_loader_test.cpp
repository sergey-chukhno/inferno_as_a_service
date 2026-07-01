#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../client/include/ReflectiveLoader.hpp"

#ifdef _WIN32

// ── Reflective DLL Loader Unit Tests ────────────────────────────
// These tests verify the PE processing logic used by the reflective
// loader. They run in-process (not against a remote target) and focus
// on header parsing, section enumeration, and helper logic.

void test_pe_header_validation() {
    // A minimal valid PE header (DOS + NT headers, no sections)
    std::vector<uint8_t> pe;
    pe.resize(4096, 0);

    // DOS header
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(pe.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;      // "MZ"
    dos->e_lfanew = sizeof(IMAGE_DOS_HEADER); // PE signature right after DOS

    // NT headers
    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        pe.data() + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;       // "PE\0\0"
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 0;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = 4096;
    nt->OptionalHeader.SizeOfHeaders = 512;
    nt->OptionalHeader.ImageBase = 0x7FFE000000; // Arbitrary preferred base
    nt->OptionalHeader.AddressOfEntryPoint = 0x1000;

    // Test: mapPESections with our own process (HANDLE = GetCurrentProcess)
    // This tests that the PE parsing logic doesn't crash.
    uint64_t entry_rva = 0;
    bool ok = inferno::nt::mapPESections(
        ::GetCurrentProcess(),
        pe.data(),                    // base (same as source — no-op essentially)
        pe,
        entry_rva);

    if (ok) {
        std::fprintf(stdout, "[PASS] test_pe_header_validation\n");
    } else {
        std::fprintf(stderr, "[FAIL] test_pe_header_validation: mapPESections failed\n");
        std::exit(1);
    }
}

void test_pe_header_invalid_signature() {
    std::vector<uint8_t> bad(512, 0);
    // No MZ signature

    uint64_t entry_rva = 0;
    bool ok = inferno::nt::mapPESections(
        ::GetCurrentProcess(),
        bad.data(),
        bad,
        entry_rva);

    if (!ok) {
        std::fprintf(stdout, "[PASS] test_pe_header_invalid_signature\n");
    } else {
        std::fprintf(stderr, "[FAIL] test_pe_header_invalid_signature: "
                             "should have failed on bad DOS signature\n");
        std::exit(1);
    }
}

void test_pe_header_empty_buffer() {
    std::vector<uint8_t> empty;
    uint64_t entry_rva = 0;
    bool ok = inferno::nt::mapPESections(
        ::GetCurrentProcess(),
        empty.data(),
        empty,
        entry_rva);

    if (!ok) {
        std::fprintf(stdout, "[PASS] test_pe_header_empty_buffer\n");
    } else {
        std::fprintf(stderr, "[FAIL] test_pe_header_empty_buffer: "
                             "should have failed on empty buffer\n");
        std::exit(1);
    }
}

void test_shellcode_bytes_valid() {
    size_t size = 0;
    const uint8_t* sc = inferno::nt::getReflectiveLoaderShellcode(size);

    if (sc != nullptr && size > 0 && size < 128) {
        std::fprintf(stdout, "[PASS] test_shellcode_bytes_valid (%zu bytes)\n", size);
    } else {
        std::fprintf(stderr, "[FAIL] test_shellcode_bytes_valid: "
                             "invalid shellcode (ptr=%p, size=%zu)\n",
                     (void*)sc, size);
        std::exit(1);
    }
}

void test_parameter_block_layout() {
    // Verify structure sizes
    if constexpr (sizeof(inferno::nt::ReflectiveLoaderParams) == 16) {
        std::fprintf(stdout, "[PASS] test_parameter_block_layout\n");
    } else {
        std::fprintf(stderr, "[FAIL] test_parameter_block_layout: "
                             "expected 16 bytes, got %zu\n",
                     sizeof(inferno::nt::ReflectiveLoaderParams));
        std::exit(1);
    }
}

void test_relocation_no_delta() {
    // applyRelocations with delta=0 should succeed immediately (no-op)
    std::vector<uint8_t> pe(4096, 0);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(pe.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = sizeof(IMAGE_DOS_HEADER);

    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        pe.data() + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 0;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = 4096;
    nt->OptionalHeader.SizeOfHeaders = 512;
    nt->OptionalHeader.ImageBase = 0x7FFE000000;
    nt->OptionalHeader.AddressOfEntryPoint = 0x1000;
    // No .reloc data directory — should be zero
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = 0;

    bool ok = inferno::nt::applyRelocations(
        ::GetCurrentProcess(),
        pe.data(),  // base (same as source in this test)
        pe,
        0);         // delta = 0

    if (ok) {
        std::fprintf(stdout, "[PASS] test_relocation_no_delta\n");
    } else {
        std::fprintf(stderr, "[FAIL] test_relocation_no_delta\n");
        std::exit(1);
    }
}

#endif // _WIN32
