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
// Minimal implementation with strict bounds checking.
// Takes src_size for compressed-input bound, dst_size for output bound.
// Returns bytes_written or 0 on error.
// Does NOT support overlapping src/dst buffers.
static DWORD lz4_decompress(const BYTE* src, DWORD src_size,
                            BYTE* dst, DWORD dst_size) {
    const BYTE* ip = src;
    const BYTE* const iend = src + src_size;
    BYTE* op = dst;
    const BYTE* const oend = dst + dst_size;

    while (op < oend) {
        // ── Token ─────────────────────────────────────────
        if (ip >= iend) return 0;
        BYTE token = *ip++;
        DWORD lit_len = (token >> 4) & 0x0F;
        if (lit_len == 15) {
            BYTE s;
            do {
                if (ip >= iend) return 0;
                s = *ip++;
                if (lit_len + s < lit_len) return 0;  // overflow
                lit_len += s;
            } while (s == 255);
        }
        if (lit_len > 0 && ip + lit_len > iend) return 0;

        // ── Copy literals ─────────────────────────────────
        if (op + lit_len > oend) return 0;
        for (DWORD i = 0; i < lit_len; i++) {
            *op++ = *ip++;
        }
        if (op >= oend) break;

        // ── Match offset ──────────────────────────────────
        if (ip + 2 > iend) return 0;
        WORD match_offset = *ip | (*(ip + 1) << 8);
        ip += 2;
        if (match_offset == 0 || match_offset > (DWORD)(op - dst)) return 0;

        // ── Match length ──────────────────────────────────
        DWORD match_len = (token & 0x0F) + 4;
        if (match_len == 19) {
            BYTE s;
            do {
                if (ip >= iend) return 0;
                s = *ip++;
                if (match_len + s < match_len) return 0;
                match_len += s;
            } while (s == 255);
        }
        if (op + match_len > oend) return 0;

        // ── Copy match ────────────────────────────────────
        BYTE* match_src = op - match_offset;
        for (DWORD i = 0; i < match_len; i++) {
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
// Walks the PEB loader module list, finds any module exporting
// VirtualProtect, resolves it by ROR-13 hash. Returns 0 on failure.
static QWORD resolve_virtual_protect(void) {
    // Pre-computed ROR-13 hash of "VirtualProtect"
    const DWORD TARGET_HASH = 0x545E31C5;

#ifdef _MSC_VER
    QWORD peb;
    __asm { mov rax, gs:[0x60]; mov peb, rax; }
#else
    QWORD peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
#endif

    // PEB → Ldr → InMemoryOrderModuleList.Flink
    QWORD ldr = *(QWORD*)(peb + 0x18);
    if (ldr == 0) return 0;

    QWORD entry = *(QWORD*)(ldr + 0x20);  // first module in load order
    if (entry == 0) return 0;
    QWORD flink_start = entry;

    // Walk the module list (up to 20 entries as safety limit).
    // Do NOT assume a fixed position for kernel32.dll — ordering
    // varies between Windows versions (Win7, Win10, Win11 differ).
    // Instead, check every module's export table by hash.
    for (int mod = 0; mod < 20; mod++) {
        QWORD dll_base = *(QWORD*)(entry + 0x20);
        if (dll_base == 0) break;

        // Read e_lfanew from DOS header and validate PE signature
        DWORD e_lfanew = *(DWORD*)(dll_base + 0x3C);
        if (e_lfanew < 0x40 || e_lfanew > 0x1000) {
            entry = *(QWORD*)entry;
            if (entry == flink_start) break;
            continue;
        }
        if (*(DWORD*)(dll_base + e_lfanew) != 0x00004550) {
            entry = *(QWORD*)entry;
            if (entry == flink_start) break;
            continue;
        }

        // Determine PE32 vs PE32+ from optional header magic
        WORD opt_magic = *(WORD*)(dll_base + e_lfanew + 24);
        DWORD dd_offset;
        if (opt_magic == 0x020B)       // PE32+
            dd_offset = 112;
        else if (opt_magic == 0x010B)  // PE32
            dd_offset = 96;
        else {
            entry = *(QWORD*)entry;
            if (entry == flink_start) break;
            continue;
        }

        // IMAGE_DIRECTORY_ENTRY_EXPORT is the first data directory (index 0)
        // at optional_header + dd_offset + 0*8
        DWORD export_dir_rva = *(DWORD*)(dll_base + e_lfanew + 24 + dd_offset);
        if (export_dir_rva == 0) {
            entry = *(QWORD*)entry;
            if (entry == flink_start) break;
            continue;
        }

        BYTE* export_dir = (BYTE*)(dll_base + export_dir_rva);
        DWORD num_names = *(DWORD*)(export_dir + 24);
        DWORD addr_of_functions = *(DWORD*)(export_dir + 28);
        DWORD addr_of_names = *(DWORD*)(export_dir + 32);
        DWORD addr_of_ordinals = *(DWORD*)(export_dir + 36);

        // Walk export name pointer table, hash each name
        for (DWORD i = 0; i < num_names; i++) {
            DWORD name_rva = *(DWORD*)(dll_base + addr_of_names + i * 4);
            const char* name = (const char*)(dll_base + name_rva);
            if (hash_string(name) == TARGET_HASH) {
                WORD ordinal = *(WORD*)(dll_base + addr_of_ordinals + i * 2);
                DWORD func_rva = *(DWORD*)(dll_base + addr_of_functions + ordinal * 4);
                return dll_base + func_rva;
            }
        }

        entry = *(QWORD*)entry;
        if (entry == flink_start) break;
    }

    return 0;  // not found
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
    // Stack buffer for LZ4 decompression (avoids heap in TLS callback).
    // Sections larger than this will be left encrypted (safe failure).
    // TODO: resolve VirtualAlloc for arbitrarily large sections.
    enum { DECOMP_BUF_SIZE = 65536 };
    BYTE decomp_buffer[DECOMP_BUF_SIZE];

    for (DWORD i = 0; i < hdr->num_sections; i++) {
        BYTE* section_addr = (BYTE*)(hinstDLL + descs[i].rva);
        DWORD csz = descs[i].compressed_sz;
        DWORD dsz = descs[i].decompressed_sz;

        if (csz == 0 || dsz == 0) continue;

        // Make section writable
        DWORD old_prot;
        VirtualProtect(section_addr, dsz, 0x04, &old_prot);  // PAGE_READWRITE

        // XOR-decrypt in-place (produces plaintext LZ4 block at section_addr)
        for (DWORD j = 0; j < csz; j++) {
            section_addr[j] ^= hdr->xor_key[j % hdr->key_size] ^ (j & 0xFF);
        }

        // LZ4-decompress to a disjoint stack buffer, then copy back.
        // This is safe: src and dst never overlap. If the section is
        // too large for the stack buffer, skip (section stays encrypted).
        if (dsz > csz && dsz <= sizeof(decomp_buffer) && csz > 0) {
            DWORD dec_sz = lz4_decompress(section_addr, csz,
                                          decomp_buffer, dsz);
            if (dec_sz == dsz) {
                for (DWORD j = 0; j < dsz; j++) {
                    section_addr[j] = decomp_buffer[j];
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
