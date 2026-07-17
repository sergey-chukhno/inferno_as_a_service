; ═════════════════════════════════════════════════════════════════════
; Inferno — PE Packer Decryptor Stub (Phase 3, Step 2)
;
; Position-independent x64 Windows TLS callback that:
;   1. Checks PEB BeingDebugged + NtGlobalFlag + rdtsc timing
;   2. Resolves VirtualProtect via PEB-walk + ROR-13 hash
;   3. XOR-decrypts + LZ4-decompresses each packed section
;   4. Applies base relocations for ASLR
;   5. Restores memory protection, returns to loader
;
; Build: nasm -f bin -o stub.bin stub.asm
;
; Stub layout at runtime:
;   [PACK header (56 bytes)] [section descs (N*16)] [code (this)]
;   ^-- packer.py patches header fields at build time
; ═════════════════════════════════════════════════════════════════════

BITS 64

; ── Stub Header offsets (patched by packer.py) ──────────────────
STRUC StubHeader
    .magic:          resd 1      ; 'PACK'
    .key_size:       resd 1      ; 32
    .xor_key:        resb 32     ; 256-bit XOR key
    .preferred_base: resq 1      ; Original ImageBase
    .num_sections:   resd 1      ; section count
    .reserved:       resd 1
ENDSTRUC

STRUC SectionDesc
    .rva:            resq 1      ; section virtual address
    .compressed_sz:  resd 1      ; size after LZ4 + XOR
    .decompressed_sz: resd 1     ; original section size
ENDSTRUC

; ── Constants ────────────────────────────────────────────────────
VIRTUAL_PROTECT_HASH  equ 0x7946C61B  ; ROR-13("VirtualProtect")
PAGE_READWRITE        equ 0x04
PAGE_EXECUTE_READWRITE equ 0x40
MEM_RELEASE           equ 0x8000

; InMemoryOrderModuleList offsets (x64)
PEB_LDR_OFFSET        equ 0x18
LDR_MODULE_FLINK      equ 0x20
LDR_MODULE_BASE       equ 0x20   ; DllBase from InMemoryOrderLinks ptr (+0x30 in LDR, -0x10 for links)
LDR_MODULE_SIZE       equ 0x40
LDR_MODULE_NAME_RVA   equ 0x58   ; UNICODE_STRING name

; ═════════════════════════════════════════════════════════════════
; Code entry point — runs as a TLS callback
; Signature: void NTAPI TlsCallback(PVOID DllHandle, ULONG Reason, PVOID Reserved)
; Parameters: rcx = DllHandle, rdx = Reason, r8 = Reserved
; ═════════════════════════════════════════════════════════════════

TlsCallback:
    ; ── Prologue: save non-volatile registers, allocate stack ──
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 0x10010  ; 64KB decomp buffer at [rsp] + 16 for alignment
    ; rsp is now 16-byte aligned (guaranteed by Windows x64 ABI)
    ; Save DllHandle (rcx) immediately — it will be clobbered by the
    ; PEB-walk resolver and subsequent section/reloc processing.
    mov     [rbp - 0x40], rcx    ; DllHandle saved below r15, above buffer

    ; ── Only run on DLL_PROCESS_ATTACH (reason == 1) ──────────
    cmp     edx, 1
    jne     .early_return

    ; ═════════════════════════════════════════════════════════
    ; Anti-debug checks
    ; ═════════════════════════════════════════════════════════

    ; ── Check 1: PEB BeingDebugged ──────────────────────────
    mov     rax, gs:[0x60]      ; rax = PEB
    mov     r15, rax             ; save PEB for later use
    cmp     byte [rax + 2], 0   ; PEB->BeingDebugged
    jne     .early_return

    ; ── Check 2: PEB NtGlobalFlag ──────────────────────────
    mov     eax, [r15 + 0x68]   ; PEB->NtGlobalFlag
    and     eax, 0x70           ; FLG_HEAP_ENABLE_TAIL_CHECK |
                                 ; FLG_HEAP_ENABLE_FREE_CHECK |
                                 ; FLG_HEAP_VALIDATE_PARAMETERS
    jne     .early_return

    ; ── Check 3: rdtsc timing check ─────────────────────────
    xor     eax, eax
    cpuid                        ; serialize
    rdtsc
    mov     r12d, eax            ; start timestamp
    mov     ecx, 200
.nop_loop:
    loop    .nop_loop
    xor     eax, eax
    cpuid
    rdtsc
    sub     eax, r12d            ; delta = end - start
    cmp     eax, 50000           ; threshold — adjust if false positives
    jg      .early_return        ; too slow → likely sandbox/vm

    ; ═════════════════════════════════════════════════════════
    ; Compute RIP-relative address of the stub header
    ; ═════════════════════════════════════════════════════════

    lea     r14, [rel TlsCallback]  ; r14 = code start (right after header)
    mov     r13, r14
    sub     r13, StubHeader_size    ; r13 = pointer to StubHeader

    ; Verify header magic
    cmp     dword [r13 + StubHeader.magic], 0x4B434150  ; 'PACK'
    jne     .early_return

    ; ═════════════════════════════════════════════════════════
    ; Resolve VirtualProtect via PEB-walk + ROR-13 hash
    ; ═════════════════════════════════════════════════════════

    mov     r12, r15             ; r12 = PEB
.vp_resolve:
    ; PEB -> Ldr -> InMemoryOrderModuleList.Flink
    ; Save the first Flink as the wrap-around sentinel.
    mov     rbx, [r12 + PEB_LDR_OFFSET]
    test    rbx, rbx
    jz      .early_return
    mov     rbx, [rbx + LDR_MODULE_FLINK]
    mov     rsi, rbx             ; rsi = current module entry (InMemoryOrderLinks)
    mov     r15, rbx             ; r15 = saved list head (wrap sentinel)

    ; Walk the module list (up to 20 entries)
    xor     r8d, r8d             ; module counter
.module_loop:
    cmp     r8d, 20
    jae     .early_return        ; too many modules, give up
    inc     r8d

    ; Get DllBase from InMemoryOrderLinks pointer.
    ; InMemoryOrderLinks is at offset 0x10 within LDR_DATA_TABLE_ENTRY.
    ; DllBase is at offset 0x30 within LDR_DATA_TABLE_ENTRY.
    ; So from the InMemoryOrderLinks pointer: DllBase = ptr + 0x20
    mov     rbx, [rsi + 0x20]    ; DllBase
    test    rbx, rbx
    jz      .next_module

    ; Validate DOS header
    cmp     word [rbx], 0x5A4D   ; 'MZ'
    jne     .next_module

    ; Get e_lfanew and validate PE signature
    mov     eax, [rbx + 0x3C]
    cmp     eax, 0x1000          ; sanity: e_lfanew should be reasonable
    jae     .next_module
    cmp     dword [rbx + rax], 0x00004550  ; 'PE\0\0'
    jne     .next_module

    ; Get optional header magic to determine DD offset
    movzx   edx, word [rbx + rax + 24]  ; opt magic at nt+24
    mov     r10d, 96             ; default for PE32
    cmp     dx, 0x020B           ; PE32+?
    jne     .check_opt32
    mov     r10d, 112            ; PE32+ DD offset
    jmp     .parse_export        ; PE32+ → don't fall through to 0x010B check
.check_opt32:
    cmp     dx, 0x010B
    je      .parse_export
    jmp     .next_module         ; unknown format, skip

.parse_export:
    ; Get IMAGE_DIRECTORY_ENTRY_EXPORT (first data directory, index 0)
    ; at nt_headers + 24 + dd_offset + 0*8
    lea     rdi, [rbx + rax + 24]  ; nt_headers
    add     rdi, r10                ; + dd_offset = data directories
    mov     ecx, [rdi]              ; export_dir_rva
    test    ecx, ecx
    jz      .next_module

    ; Parse IMAGE_EXPORT_DIRECTORY
    add     rcx, rbx             ; export_dir = dll_base + rva
    mov     r9d, [rcx + 24]      ; NumberOfNames
    test    r9d, r9d
    jz      .next_module

    mov     r10d, [rcx + 28]     ; AddressOfFunctions
    mov     r11d, [rcx + 32]     ; AddressOfNames
    mov     r12d, [rcx + 36]     ; AddressOfNameOrdinals

    add     r10, rbx             ; convert RVAs to VAs
    add     r11, rbx
    add     r12, rbx

    ; Walk export name pointer table
    xor     edi, edi             ; name index
.name_loop:
    cmp     edi, r9d
    jae     .next_module

    mov     esi, [r11 + rdi * 4] ; name_rva
    add     rsi, rbx             ; name = dll_base + name_rva

    ; Compute ROR-13 hash of the function name
    xor     eax, eax             ; hash = 0
.hash_loop:
    movzx   ecx, byte [rsi]
    test    cl, cl
    jz      .hash_done
    ror     eax, 13
    add     eax, ecx
    inc     rsi
    jmp     .hash_loop
.hash_done:

    cmp     eax, VIRTUAL_PROTECT_HASH
    jne     .next_name

    ; ── Resolve function address (hash match confirmed) ────
    movzx   ecx, word [r12 + rdi * 2]  ; ordinal index
    mov     eax, [r10 + rcx * 4]       ; function RVA
    add     rax, rbx                    ; function VA
    mov     r12, rax                    ; r12 = VirtualProtect function
    jmp     .vp_found

.next_name:
    inc     edi
    jmp     .name_loop

.next_module:
    mov     rsi, [rsi]           ; Flink (next InMemoryOrderLinks entry)
    cmp     rsi, r15             ; wrapped back to list head?
    jne     .module_loop
    jmp     .early_return        ; not found

.vp_found:
    test    r12, r12
    jz      .early_return

    ; ═════════════════════════════════════════════════════════
    ; Decrypt and decompress each section
    ; ═════════════════════════════════════════════════════════

    ; r13 = StubHeader (patched by packer.py)
    ; r12 = VirtualProtect function
    mov     r15d, [r13 + StubHeader.num_sections]
    test    r15d, r15d
    jz      .apply_relocs

    ; r11 = section descriptor array (stored in header.reserved as
    ; offset from header start: 56 + code_size)
    mov     r11d, [r13 + StubHeader.reserved]
    add     r11, r13

    ; r10 = actual base address (saved from TLS callback parameter)
    mov     r10, [rbp - 0x40]     ; DllHandle

.section_loop:
    test    r15d, r15d
    jz      .apply_relocs
    dec     r15d

    ; Get section descriptor fields
    mov     rbx, [r11 + SectionDesc.rva]       ; section RVA
    mov     r8d, [r11 + SectionDesc.compressed_sz]   ; compressed size
    mov     r9d, [r11 + SectionDesc.decompressed_sz]  ; decompressed size

    test    r8d, r8d
    jz      .next_section
    test    r9d, r9d
    jz      .next_section

    ; Compute VA: DllHandle + section_rva  (module base + RVA)
    add     rbx, r10             ; section VA
    mov     r14, rbx             ; save section VA

    ; ── Spill section metadata to stack before call ─────────
    ; VirtualProtect uses r8 (flNewProtect), r9 (oldProtect) as
    ; parameters, and r11 is volatile — all three are clobbered.
    mov     [rbp - 0x48], r11    ; save descriptor pointer
    mov     [rbp - 0x50], r8d    ; save compressed_sz
    mov     [rbp - 0x54], r9d    ; save decompressed_sz

    ; ── Make section writable ──────────────────────────────
    sub     rsp, 0x30           ; shadow space for VirtualProtect
    mov     rcx, rbx            ; lpAddress
    mov     edx, r9d            ; dwSize (decompressed size)
    mov     r8d, PAGE_READWRITE ; flNewProtect
    lea     r9, [rsp + 0x30]    ; lpfOldProtect (use stack slot)
    mov     [r9], r9d           ; init old_prot
    call    r12                 ; VirtualProtect
    add     rsp, 0x30

    ; ── Restore section metadata from stack ─────────────────
    mov     r11, [rbp - 0x48]    ; restore descriptor pointer
    mov     r8d, [rbp - 0x50]    ; restore compressed_sz
    mov     r9d, [rbp - 0x54]    ; restore decompressed_sz

    ; ── XOR-decrypt in-place ──────────────────────────────
    mov     ecx, [r13 + StubHeader.key_size]
    test    ecx, ecx
    jz      .skip_xor

    xor     edi, edi             ; byte index
.xor_loop:
    cmp     edi, r8d             ; compressed_sz
    jae     .xor_done

    ; rolling_xor: data[i] ^= key[i % key_size] ^ (i & 0xFF)
    xor     edx, edx
    mov     eax, edi
    div     ecx                  ; eax = quotient, edx = remainder
    movzx   esi, byte [r13 + StubHeader.xor_key + rdx]  ; key[i % key_size]
    xor     esi, edi
    and     esi, 0xFF
    xor     byte [r14 + rdi], sil  ; data[i] ^= (key[i%klen] ^ (i&0xFF))

    inc     edi
    jmp     .xor_loop
.skip_xor:
.xor_done:

    ; ── LZ4-decompress to stack buffer ────────────────────
    cmp     r8d, r9d             ; compressed_sz == decompressed_sz?
    jae     .skip_decompress     ; no compression benefit, skip

    ; Check if decompressed size fits in our 64KB buffer
    cmp     r9d, 0x10000
    ja      .skip_decompress     ; too large, skip (stays encrypted)

    ; Decompress from section_addr (r14) to stack buffer (rsp)
    lea     rbx, [rsp]          ; decompression buffer (64KB at [rsp])
    mov     rcx, r14            ; src = section_addr
    mov     edx, r8d            ; src_size = compressed_sz
    mov     r8, rbx             ; dst = stack buffer
    mov     r9d, r9d            ; dst_size = decompressed_sz (same register)

    call    lz4_decompress

    ; Copy decompressed data back to section
    test    eax, eax
    jz      .skip_decompress     ; decompression failed
    cmp     eax, [r11 + SectionDesc.decompressed_sz]
    jne     .skip_decompress     ; wrong size

    mov     edx, [r11 + SectionDesc.decompressed_sz]
    xor     ecx, ecx
.copy_loop:
    cmp     ecx, edx
    jae     .copy_done
    mov     al, [rbx + rcx]
    mov     [r14 + rcx], al
    inc     ecx
    jmp     .copy_loop
.copy_done:

.skip_decompress:

    ; ── Restore original protection ─────────────────────────
    ; Restore to PAGE_EXECUTE_READWRITE (RX + W during decryption).
    ; The exact original protection from the first VirtualProtect
    ; call is not preserved across the XOR/decompress block.
    ; RWX is safe because this code runs during process attach
    ; before any other code in the decrypted section executes.
    sub     rsp, 0x30
    mov     rcx, r14            ; lpAddress
    mov     edx, r9d            ; dwSize
    mov     r8d, PAGE_EXECUTE_READWRITE
    lea     r9, [rsp + 0x30]    ; dummy store for lpfOldProtect
    call    r12
    add     rsp, 0x30

.next_section:
    add     r11, SectionDesc_size
    jmp     .section_loop

    ; ═════════════════════════════════════════════════════════
    ; Apply base relocations (for ASLR)
    ; ═════════════════════════════════════════════════════════

.apply_relocs:
    ; ── Compute delta = actual_base - preferred_base ────────
    ; DllHandle was saved at [rbp - 0x40] in the prologue.
    mov     rbx, [rbp - 0x40]     ; rbx = actual_base (DllHandle)
    mov     r14, [r13 + StubHeader.preferred_base]
    sub     r14, rbx              ; r14 = delta (actual - preferred)
    neg     r14                   ; delta = preferred - actual
    jz      .restore_return       ; delta == 0, no relocs needed

    ; ── Find .reloc directory via PE header ─────────────────
    ; The .reloc section was NOT encrypted (loader-critical),
    ; so it's still readable at this point.
    ;
    ; PE header: actual_base + e_lfanew
    mov     eax, [rbx + 0x3C]     ; e_lfanew
    add     rax, rbx              ; rax = nt_headers

    ; Verify PE signature (quick check)
    cmp     dword [rax], 0x00004550  ; 'PE\0\0'
    jne     .restore_return

    ; Determine data directory offset (PE32: 96, PE32+: 112)
    movzx   edx, word [rax + 24]  ; optional header magic
    mov     r8d, 96
    cmp     dx, 0x020B            ; PE32+?
    jne     .reloc_pe32
    mov     r8d, 112
.reloc_pe32:

    ; Get IMAGE_DIRECTORY_ENTRY_BASERELOC (index 5)
    ; at nt_headers + 24 + dd_offset + 5*8
    lea     rcx, [rax + 24 + r8]  ; rcx = data directories
    mov     r9d, [rcx + 5 * 8]    ; r9d = reloc RVA
    test    r9d, r9d
    jz      .restore_return
    mov     r10d, [rcx + 5 * 8 + 4]  ; r10d = reloc size
    test    r10d, r10d
    jz      .restore_return

    ; ── Walk relocation blocks ─────────────────────────────
    ; r9 = reloc RVA, r10 = reloc size, r14 = delta
    ; rbx = actual_base
    add     r9, rbx               ; r9 = reloc VA (dll_base + rva)
    mov     r11, r9               ; r11 = current block
    lea     r15, [r9 + r10]       ; r15 = end address

.reloc_block_loop:
    ; Guard: need at least 8 bytes for header (PageRVA + SizeOfBlock)
    lea     r8, [r11 + 8]
    cmp     r8, r15
    ja      .restore_return

    mov     ebx, [r11]            ; ebx = PageRVA
    mov     r8d, [r11 + 4]        ; r8d = SizeOfBlock (save for later)
    test    r8d, r8d
    jz      .restore_return
    cmp     r8d, 8
    jb      .restore_return        ; minimum valid block: 8 bytes header

    ; Guard: SizeOfBlock must not extend past the reloc table end
    mov     eax, r8d
    add     rax, r11
    cmp     rax, r15
    ja      .restore_return

    ; Number of entries = (SizeOfBlock - 8) / 2
    mov     ecx, r8d
    sub     ecx, 8
    shr     ecx, 1                ; ecx = entry count
    test    ecx, ecx
    jz      .next_reloc_block

    lea     rsi, [r11 + 8]        ; rsi = entries start

.reloc_entry_loop:
    movzx   eax, word [rsi]       ; entry: high 4 = type, low 12 = offset
    add     rsi, 2

    mov     edx, eax
    and     eax, 0x0FFF           ; eax = offset (low 12 bits)
    shr     edx, 12               ; edx = type (high 4 bits)

    ; Skip absolute entries (type 0, used for alignment padding)
    test    edx, edx
    jz      .reloc_entry_check_cnt

    ; Only handle DIR64 (type 10) for x64
    cmp     edx, 10
    jne     .reloc_entry_check_cnt

    ; Apply fixup: *(uint64_t*)(dll_base + PageRVA + offset) += delta
    ; Compute full VA: actual_base + PageRVA + offset
    mov     r9, [r13 + StubHeader.preferred_base]
    add     r9, r14               ; r9 = actual_base = preferred + delta
    mov     rdi, rbx              ; rdi = PageRVA
    add     rdi, rax              ; rdi = PageRVA + offset (within page)
    add     rdi, r9               ; rdi = full VA of fixup location

    ; Reject obviously invalid addresses (< 64KB or null page)
    cmp     rdi, 0x10000
    jb      .reloc_entry_check_cnt

    add     qword [rdi], r14

.reloc_entry_check_cnt:
    dec     ecx
    jnz     .reloc_entry_loop

.next_reloc_block:
    add     r11, r8               ; advance past this entire block
    jmp     .reloc_block_loop

    ; ═════════════════════════════════════════════════════════
    ; Epilogue: restore and return
    ; ═════════════════════════════════════════════════════════

.restore_return:
.early_return:
    lea     rsp, [rbp - 56]     ; restore stack (rbp + 7 regs = 56 bytes)
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    pop     rbp
    ret

; ═════════════════════════════════════════════════════════════════
; LZ4 Block Decompressor (minimal, safe implementation)
;
; Input:  rcx = src, edx = src_size, r8 = dst, r9d = dst_size
; Output: eax = bytes written (0 on error)
; Preserves: r12, r13, r14, r15
; ═════════════════════════════════════════════════════════════════

lz4_decompress:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    rsi
    push    rdi

    mov     rsi, rcx             ; ip = src
    mov     edi, edx             ; iend = src + src_size
    add     rdi, rsi
    mov     rbx, r8              ; op = dst
    mov     r10d, r9d            ; oend = dst + dst_size
    add     r10, rbx
    xor     eax, eax             ; default: 0 bytes written

.lz4_loop:
    cmp     rbx, r10             ; op >= oend?
    jae     .lz4_done

    ; Read token
    cmp     rsi, rdi
    jae     .lz4_error
    movzx   ecx, byte [rsi]      ; token
    inc     rsi

    ; ── Literal length ────────────────────────────────────
    mov     edx, ecx
    shr     edx, 4               ; lit_len = token >> 4
    cmp     edx, 15
    jne     .lit_len_done
.lit_len_extra:
    cmp     rsi, rdi
    jae     .lz4_error
    movzx   eax, byte [rsi]
    inc     rsi
    add     edx, eax
    cmp     eax, 255
    je      .lit_len_extra
.lit_len_done:

    ; ── Copy literals ─────────────────────────────────────
    mov     eax, edx
    test    eax, eax
    jz      .literals_done

    ; Check bounds
    mov     r11d, edx
    add     r11, rbx
    cmp     r11, r10             ; op + lit_len > oend?
    ja      .lz4_error
    add     r11, rsi
    cmp     r11, rdi             ; ip + lit_len > iend?
    ja      .lz4_error

    ; Manual literal copy (rep movsb would write to [rdi] = iend
    ; instead of [rbx] = op, and use ecx = token instead of edx)
    mov     ecx, edx             ; ecx = lit_len
.lit_copy:
    test    ecx, ecx
    jz      .literals_done
    mov     al, [rsi]
    mov     [rbx], al
    inc     rsi
    inc     rbx
    dec     ecx
    jmp     .lit_copy

.literals_done:
    cmp     rbx, r10             ; op >= oend?
    jae     .lz4_done

    ; ── Match offset ──────────────────────────────────────
    cmp     rsi, rdi
    jae     .lz4_error
    movzx   r11d, word [rsi]     ; match_offset (little-endian)
    add     rsi, 2
    test    r11d, r11d
    jz      .lz4_error           ; match_offset == 0 is invalid

    ; Check match_src >= dst (no back-reference before buffer start)
    mov     r9, rbx              ; r9 = match_src (save before clobbering)
    sub     r9, r11
    cmp     r9, r8
    jb      .lz4_error           ; match_src < dst

    ; ── Match length ──────────────────────────────────────
    mov     edx, ecx
    and     edx, 0x0F            ; match_len = token & 0x0F
    add     edx, 4
    cmp     edx, 19
    jne     .match_len_done
.match_len_extra:
    cmp     rsi, rdi
    jae     .lz4_error
    movzx   eax, byte [rsi]
    inc     rsi
    add     edx, eax
    cmp     eax, 255
    je      .match_len_extra
.match_len_done:

    ; Check op + match_len <= oend
    mov     r11d, edx
    add     r11, rbx
    cmp     r11, r10
    ja      .lz4_error

    ; ── Copy match ────────────────────────────────────────
    ; r9 = match_src (saved above), edx = match_len
    mov     edi, edx             ; edi = match_len (counter)
.lz4_match_copy:
    test    edi, edi
    jz      .lz4_match_done
    mov     al, [r9]
    mov     [rbx], al
    inc     r9
    inc     rbx
    dec     edi
    jmp     .lz4_match_copy
.lz4_match_done:

    jmp     .lz4_loop

.lz4_done:
    ; Success: return bytes written
    mov     rax, rbx
    sub     rax, r8
    jmp     .lz4_exit

.lz4_error:
    xor     eax, eax

.lz4_exit:
    pop     rdi
    pop     rsi
    pop     rbx
    pop     rbp
    ret


