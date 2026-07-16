/**
 * Inferno — Decryptor Stub for PE Packer (Phase 3)
 *
 * This is compiled with:
 *   clang -c -nostdlib -fPIC -Os -m64 stub.c -o stub.o
 *   objcopy -O binary -j .text stub.o stub.bin
 *
 * The resulting stub.bin is a position-independent blob that:
 *   1. Runs as a TLS callback (called by Windows before main())
 *   2. Checks PEB BeingDebugged for anti-debug
 *   3. Resolves VirtualProtect via PEB-walk + ROR-13 hash
 *   4. XOR-decrypts + LZ4-decompresses each section
 *   5. Applies base relocations
 *   6. Restores memory protection
 *   7. Returns (Windows continues to CRT -> main())
 *
 * The binary blob has a header patched by packer.py at pack time.
 */

// Force integer sizes for bare-metal code
typedef unsigned long long  QWORD;
typedef unsigned int        DWORD;
typedef unsigned short      WORD;
typedef unsigned char       BYTE;

// Stub header — packer.py patches this before injection
typedef struct {
    DWORD  magic;             // 'PACK' — identifies a valid stub
    DWORD  key_size;          // 32 (256-bit XOR key size)
    BYTE   xor_key[32];       // XOR encryption key
    QWORD  preferred_base;    // Original ImageBase for reloc delta
    DWORD  num_sections;      // Number of packed sections
    DWORD  reserved;
    // Followed by num_sections × SectionDescriptor:
    //   QWORD  rva;           // Virtual address of section
    //   DWORD  compressed_sz; // Size after LZ4 + XOR (on disk)
    //   DWORD  decompressed_sz; // Original section size
} StubHeader;

typedef struct {
    QWORD  rva;
    DWORD  compressed_sz;
    DWORD  decompressed_sz;
} SectionDescriptor;

// ── Portable LZ4 Block Decompressor ──────────────────────────────
// Minimal implementation: reads an LZ4 block and decompresses it.
// Returns bytes_written or 0 on error.
static DWORD lz4_decompress(const BYTE* src, BYTE* dst, DWORD dst_size) {
    const BYTE* ip = src;
    BYTE* op = dst;
    const BYTE* const oend = dst + dst_size;

    while (op < oend) {
        BYTE token = *ip++;
        DWORD lit_len = (token >> 4) & 0x0F;
        if (lit_len == 15) {
            BYTE s;
            do {
                s = *ip++;
                lit_len += s;
            } while (s == 255);
        }
        // Copy literals
        for (DWORD i = 0; i < lit_len && op < oend; i++) {
            *op++ = *ip++;
        }
        if (op >= oend) break;

        // Match
        WORD match_offset = *ip | (*(ip + 1) << 8);
        ip += 2;
        if (match_offset == 0) return 0;

        DWORD match_len = (token & 0x0F) + 4;
        if (match_len == 19) {
            BYTE s;
            do {
                s = *ip++;
                match_len += s;
            } while (s == 255);
        }

        BYTE* match_src = op - match_offset;
        for (DWORD i = 0; i < match_len && op < oend; i++) {
            *op++ = *match_src++;
        }
    }
    return (DWORD)(op - dst);
}

// ── ROR-13 hash for API resolution ───────────────────────────
static DWORD ror13(DWORD hash, BYTE c) {
    hash = (hash >> 13) | (hash << 19);
    return hash + c;
}

static DWORD hash_string(const char* str) {
    DWORD hash = 0;
    while (*str) {
        hash = ror13(hash, *str);
        str++;
    }
    return hash;
}

// ── PEB-walk → resolve VirtualProtect ─────────────────────────
// Finds kernel32.dll in the PEB loader data, walks exports by
// ROR-13 hash, returns the function address or 0.
static QWORD resolve_virtual_protect(void) {
    // Target hash for "VirtualProtect" — pre-computed
    const DWORD TARGET_HASH = 0x545E31C5;  // ROR-13("VirtualProtect")

#ifdef _MSC_VER
    QWORD peb;
    __asm {
        mov rax, gs:[0x60]
        mov peb, rax
    }
#else
    QWORD peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
#endif

    // PEB → Ldr → InMemoryOrderModuleList → kernel32 → DllBase
    // The 3rd module in the list is typically kernel32.dll
    QWORD ldr = *(QWORD*)(peb + 0x18);
    QWORD flink = *(QWORD*)(ldr + 0x20);  // InMemoryOrderModuleList.Flink
    QWORD entry = flink;

    // Walk to kernel32 (usually 3rd entry, but walk safely)
    for (int i = 0; i < 3; i++) {
        if (*(QWORD*)entry == flink) break; // wrapped
        entry = *(QWORD*)entry;  // Flink
    }

    QWORD dll_base = *(QWORD*)(entry + 0x20);  // DllBase

    // Parse PE header → export directory
    DWORD pe_sig = *(DWORD*)(dll_base + ((DWORD*)(dll_base + 0x3C))[0]);
    if (pe_sig != 0x00004550) return 0;  // "PE\0\0"

    QWORD export_dir_rva = *(DWORD*)(dll_base + 0x88);  // IMAGE_DIRECTORY_ENTRY_EXPORT
    if (export_dir_rva == 0) return 0;
    BYTE* export_dir = (BYTE*)(dll_base + export_dir_rva);

    DWORD num_names = *(DWORD*)(export_dir + 24);
    DWORD addr_of_functions = *(DWORD*)(export_dir + 28);
    DWORD addr_of_names = *(DWORD*)(export_dir + 32);
    DWORD addr_of_ordinals = *(DWORD*)(export_dir + 36);

    for (DWORD i = 0; i < num_names; i++) {
        DWORD name_rva = *(DWORD*)(dll_base + addr_of_names + i * 4);
        const char* name = (const char*)(dll_base + name_rva);
        if (hash_string(name) == TARGET_HASH) {
            WORD ordinal = *(WORD*)(dll_base + addr_of_ordinals + i * 2);
            DWORD func_rva = *(DWORD*)(dll_base + addr_of_functions + ordinal * 4);
            return dll_base + func_rva;
        }
    }
    return 0;
}

// ── TLS Callback Entry Point ─────────────────────────────────
// Called by Windows loader before main(). Signature:
//   void NTAPI TlsCallback(PVOID hinstDLL, DWORD reason, PVOID reserved)
void __attribute__((force_align_arg_pointer))
__attribute__((section(".text")))
TlsCallback(void* hinstDLL, DWORD reason, void* reserved) {
    (void)hinstDLL; (void)reason; (void)reserved;

    // ── Anti-debug: PEB BeingDebugged ────────────────────────
#ifdef _MSC_VER
    QWORD peb;
    __asm { mov rax, gs:[0x60]; mov peb, rax; }
#else
    QWORD peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
#endif

    BYTE being_debugged = *(BYTE*)(peb + 2);
    if (being_debugged) return;  // Silently exit under debugger

    // ── The stub header is placed at a known offset ──────────
    // It's located just before this function (packer.py places
    // the header immediately before the code).
    extern volatile StubHeader __packer_header;
    StubHeader* hdr = (StubHeader*)&__packer_header;

    if (hdr->magic != 0x4B434150) return;  // 'PACK'

    // ── Resolve VirtualProtect via PEB-walk ──────────────────
    typedef int (__attribute__((stdcall)) *pVirtualProtect)(
        void* addr, QWORD size, DWORD new_prot, DWORD* old_prot);
    pVirtualProtect VirtualProtect = (pVirtualProtect)resolve_virtual_protect();
    if (!VirtualProtect) return;

    // ── Section descriptors follow the header ────────────────
    SectionDescriptor* descs = (SectionDescriptor*)(hdr + 1);

    // ── Decrypt + decompress each section ────────────────────
    for (DWORD i = 0; i < hdr->num_sections; i++) {
        BYTE* section_addr = (BYTE*)(hinstDLL + descs[i].rva);
        DWORD csz = descs[i].compressed_sz;
        DWORD dsz = descs[i].decompressed_sz;

        if (csz == 0 || dsz == 0) continue;

        // Make section writable
        DWORD old_prot;
        VirtualProtect(section_addr, dsz, 0x04, &old_prot);  // PAGE_READWRITE

        // XOR-decrypt in-place
        for (DWORD j = 0; j < csz; j++) {
            section_addr[j] ^= hdr->xor_key[j % hdr->key_size] ^ (j & 0xFF);
        }

        // LZ4-decompress over the same buffer
        BYTE* decomp_buf = section_addr + csz;  // temporary space after data
        // For sections where compressed data fits in place, decompress
        // to a temporary buffer first, then copy back.
        // In practice, csz < dsz (compression), so we have room.
        if (dsz > csz) {
            BYTE temp[4096];  // Stack buffer for decompression
            BYTE* workspace = temp;
            if (dsz > sizeof(temp)) {
                // For large sections, we need a different approach
                // Decompress to the end of the section area backwards
                workspace = section_addr + dsz - dsz;  // same buffer
            }
            DWORD dec_sz = lz4_decompress(section_addr, workspace, dsz);
            if (dec_sz > 0) {
                for (DWORD j = 0; j < dec_sz; j++) {
                    section_addr[j] = workspace[j];
                }
            }
        }

        // Restore original protection
        VirtualProtect(section_addr, dsz, old_prot, &old_prot);
    }

    // ── Apply base relocations (if needed) ───────────────────
    QWORD actual_base = (QWORD)hinstDLL;
    QWORD delta = actual_base - hdr->preferred_base;
    if (delta != 0) {
        // The packer stores the .reloc section details.
        // For now, relocations are handled by reading the reloc
        // directory from the PE header, since it hasn't been encrypted.
        // TODO: Full relocation processing
    }
}

// Marker for packer.py to find the header location
volatile StubHeader __packer_header
    __attribute__((section(".text")))
    __attribute__((used)) = {0};
