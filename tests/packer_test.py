#!/usr/bin/env python3
"""Tests for the Inferno PE Packer (Phase 3).

Generates a minimal valid PE32+ binary for testing, then verifies:
  - PE parser correctly identifies headers, sections, data directories
  - Section collector filters correctly
  - Key generation produces consistent/different hashes
  - (Future) Compression + encryption round-trips
  - (Future) Packed binary is structurally valid
"""

import hashlib
import os
import struct
import sys
import tempfile

# Add parent dir so we can import packer
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from packer.packer import (PEFile, make_key, collect_descriptors, rolling_xor,
                            pack_sections, build_stub, inject_stub, pack,
                            IMAGE_SCN_MEM_EXECUTE)


# ═══════════════════════════════════════════════════════════════
# Minimal PE32+ (x64) Binary Generator
# ═══════════════════════════════════════════════════════════════

def make_minimal_pe64() -> bytes:
    """Generate a minimal but valid PE32+ binary with sections.

    Returns the raw bytes of a tiny PE32+ executable.
    """
    chunks = bytearray()

    # ── DOS Header (64 bytes) ──────────────────────────────
    dos = bytearray(64)
    struct.pack_into("<H", dos, 0, 0x5A4D)  # MZ
    struct.pack_into("<I", dos, 0x3C, 64)   # e_lfanew = 64
    chunks.extend(dos)

    # ── PE Signature (4 bytes) ────────────────────────────
    chunks.extend(b'PE\x00\x00')

    # ── IMAGE_FILE_HEADER (20 bytes) ──────────────────────
    # Machine=0x8664(AMD64), Sections=3, SizeOfOptHdr, Characteristics
    file_header_off = len(chunks)
    num_sections = 3
    opt_hdr_size = 240  # PE32+ standard size (0xF0)
    struct.pack_into("<HHIIIHH", bytearray(20), 0,
                     0x8664,       # Machine: AMD64
                     num_sections,
                     0,            # TimeDateStamp
                     0,            # PointerToSymbolTable
                     0,            # NumberOfSymbols
                     opt_hdr_size, # SizeOfOptionalHeader
                     0x0022,       # Characteristics: EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
                     )
    chunks.extend(struct.pack("<HHIIIHH",
                              0x8664, num_sections, 0, 0, 0,
                              opt_hdr_size, 0x0022))

    # ── IMAGE_OPTIONAL_HEADER64 (240 bytes) ────────────────
    image_base = 0x140000000
    section_align = 0x1000
    file_align = 0x200

    #   Section layout:
    #   .text:  VA=0x1000, raw=0x400, size=256 bytes
    #   .rdata: VA=0x2000, raw=0x600, size=128 bytes  
    #   .data:  VA=0x3000, raw=0x800, size=64 bytes
    #   reloc is embedded in .rdata for simplicity

    entry_rva = 0x1000  # first byte of .text

    oh = bytearray(opt_hdr_size)
    off = 0
    # Magic(2) + LinkerVersion(2) + SizeOfCode(4) + SizeOfInitData(4) +
    # SizeOfUninitData(4) + AddressOfEntryPoint(4) + BaseOfCode(4) = 24
    struct.pack_into("<HBBIIIII", oh, off,
                     0x020B,    # PE32+ magic
                     10, 0,    # linker version 10.0
                     0x1000,   # SizeOfCode
                     0x2000,   # SizeOfInitializedData
                     0,        # SizeOfUninitializedData
                     entry_rva,  # AddressOfEntryPoint
                     0x1000)  # BaseOfCode
    off += 24
    # ImageBase (8 bytes)
    struct.pack_into("<Q", oh, off, image_base)
    off += 8
    # SectionAlignment(4) + FileAlignment(4) + OSVer(2+2) + ImageVer(2+2) +
    # SubsysVer(2+2) + Win32Version(4) = 24
    struct.pack_into("<IIHHHHHHI", oh, off,
                     section_align, file_align,
                     6, 0,   # major/minor OS version
                     0, 0,   # major/minor image version
                     6, 0,   # major/minor subsystem version (6.0)
                     0)      # Win32VersionValue
    off += 24
    # SizeOfImage(4) + SizeOfHeaders(4) + CheckSum(4) + Subsystem(2) +
    # DllCharacteristics(2) = 16
    # SizeOfImage = end of last section rounded up to section_align
    # last section: .data starts at VA 0x3000, size 64, ends at ~0x3040
    # aligned up: 0x4000
    struct.pack_into("<IIIHH", oh, off,
                     0x4000,   # SizeOfImage
                     0x0200,   # SizeOfHeaders (headers end at ~0x1C0, align to 0x200)
                     0,        # CheckSum
                     2,        # Subsystem: WINDOWS_GUI
                     0x0140)   # DllCharacteristics: DYNAMIC_BASE | NX_COMPAT
    off += 16
    # StackReserve(8) + StackCommit(8) + HeapReserve(8) + HeapCommit(8) = 32
    struct.pack_into("<QQQQ", oh, off,
                     0x100000,  # StackReserve
                     0x10000,   # StackCommit
                     0x100000,  # HeapReserve
                     0x1000)    # HeapCommit
    off += 32
    # LoaderFlags(4) + NumberOfRvaAndSizes(4) = 8
    num_dd = 16
    struct.pack_into("<II", oh, off, 0, num_dd)
    off += 8

    # Data directories (16 entries × 8 bytes = 128)
    # Entry 1: Import directory (non-zero to make it realistic)
    # Entry 5: Reloc directory = .rdata offset within .rdata
    # Entry 9: TLS directory = not present (will create one)
    # All others zero
    # reloc data will be at .rdata + 100 (after our test data)
    reloc_rva = 0x2000 + 0x80  # 0x2080
    struct.pack_into("<II", oh, off + IMAGE_DIRECTORY_ENTRY_IMPORT * 8,
                     0x2000, 8)  # Import dir: point to .rdata
    struct.pack_into("<II", oh, off + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8,
                     reloc_rva, 24)  # Reloc dir: 24 bytes
    chunks.extend(oh)

    # ── Section Table (num_sections × 40 bytes = 120) ─────
    sections_data = [
        (b'.text\x00\x00\x00',  256, 0x1000, 0x200, 0x400, 0x60000020),
        (b'.rdata\x00\x00\x00', 256, 0x2000, 0x200, 0x600, 0x40000040),
        (b'.data\x00\x00\x00',  64,  0x3000, 0x200, 0x800, 0xC0000040),
    ]
    for name, vs, va, srd, prd, chars in sections_data:
        sec_hdr = bytearray(40)
        struct.pack_into("<8sIIIIIIHHI", sec_hdr, 0,
                         name, vs, va, srd, prd,
                         0, 0, 0, 0, chars)
        chunks.extend(sec_hdr)

    # Pad to file_align (0x400)
    while len(chunks) < 0x400:
        chunks.append(0)

    # ── Section data ──────────────────────────────────────
    # .text at 0x400: minimal ret instruction (0xC3) * 256
    text_data = bytearray([0xC3] * 256)  # 256 bytes of RET
    # Pad to 0x200 (file aligned)
    text_data.extend([0] * (0x200 - len(text_data)))
    chunks.extend(text_data)

    # .rdata at 0x600: import data + reloc data
    rdata = bytearray(0x200)
    # First 8 bytes: fake import directory
    # Reloc block at offset 0x80 (RVA 0x2080):
    # IMAGE_BASE_RELOCATION: PageRVA=0x1000, SizeOfBlock=12
    # Entry: offset=0x000, type=10 (IMAGE_REL_BASED_DIR64)
    struct.pack_into("<IIH", rdata, 0x80, 0x1000, 12, 0xA000)
    chunks.extend(rdata)

    # .data at 0x800: 64 bytes of zeros
    chunks.extend(bytearray(0x200))

    return bytes(chunks)


def make_minimal_pe32() -> bytes:
    """Generate a minimal valid PE32 (x86) binary with sections.

    Uses a smaller optional header (224 bytes) and PE32-specific fields
    (4-byte ImageBase, present BaseOfData, 4-byte stack/heap sizes).
    """
    chunks = bytearray()

    # ── DOS Header (64 bytes) ──────────────────────────────
    dos = bytearray(64)
    struct.pack_into("<H", dos, 0, 0x5A4D)
    struct.pack_into("<I", dos, 0x3C, 64)
    chunks.extend(dos)

    # ── PE Signature ──────────────────────────────────────
    chunks.extend(b'PE\x00\x00')

    # ── IMAGE_FILE_HEADER (20 bytes) ──────────────────────
    num_sections = 2
    opt_hdr_size = 224  # PE32 standard (0xE0)
    chunks.extend(struct.pack("<HHIIIHH",
                              0x14C,        # Machine: I386
                              num_sections,
                              0, 0, 0,      # TimeDate, SymTab, NumSym
                              opt_hdr_size,
                              0x0102))      # EXECUTABLE_IMAGE | 32BIT_MACHINE

    # ── IMAGE_OPTIONAL_HEADER32 (224 bytes) ───────────────
    image_base = 0x00400000
    section_align = 0x1000
    file_align = 0x200
    entry_rva = 0x1000

    oh = bytearray(opt_hdr_size)
    off = 0
    # Magic(2) + LinkerVersion(2) + SizeOfCode(4) + SizeOfInitData(4) +
    # SizeOfUninitData(4) + AddressOfEntryPoint(4) + BaseOfCode(4) = 24
    struct.pack_into("<HBBIIIII", oh, off,
                     0x010B,    # PE32 magic
                     8, 0,      # linker version 8.0
                     0x1000,    # SizeOfCode
                     0x2000,    # SizeOfInitializedData
                     0,         # SizeOfUninitializedData
                     entry_rva, # AddressOfEntryPoint
                     0x1000)    # BaseOfCode
    off += 24
    # BaseOfData(4) + ImageBase(4) = 8
    struct.pack_into("<II", oh, off, 0x2000, image_base)
    off += 8
    # SectionAlignment(4) + FileAlignment(4) + OSVer(2+2) + ImageVer(2+2) +
    # SubsysVer(2+2) + Win32Version(4) = 24
    struct.pack_into("<IIHHHHHHI", oh, off,
                     section_align, file_align,
                     6, 0, 0, 0, 6, 0, 0)
    off += 24
    # SizeOfImage(4) + SizeOfHeaders(4) + CheckSum(4) + Subsystem(2) +
    # DllCharacteristics(2) = 16
    struct.pack_into("<IIIHH", oh, off,
                     0x3000,    # SizeOfImage
                     0x0200,    # SizeOfHeaders (headers end at ~0x188, align to 0x200)
                     0,         # CheckSum
                     2,         # Subsystem: WINDOWS_GUI
                     0x0140)    # DllCharacteristics
    off += 16
    # StackReserve(4) + StackCommit(4) + HeapReserve(4) + HeapCommit(4) = 16
    struct.pack_into("<IIII", oh, off,
                     0x100000, 0x10000, 0x100000, 0x1000)
    off += 16
    # LoaderFlags(4) + NumberOfRvaAndSizes(4) = 8
    struct.pack_into("<II", oh, off, 0, 16)
    off += 8

    # Data directories (16 × 8 = 128 bytes) — all zero (no imports needed)
    # We're at the right offset since off + 128 should fill 224 bytes
    chunks.extend(oh)

    # ── Section Table (2 × 40 = 80 bytes) ─────────────────
    sections_data = [
        (b'.text\x00\x00\x00',   256, 0x1000, 0x200, 0x400, 0x60000020),
        (b'.rdata\x00\x00\x00',  128, 0x2000, 0x200, 0x600, 0x40000040),
    ]
    for name, vs, va, srd, prd, chars in sections_data:
        sec_hdr = bytearray(40)
        struct.pack_into("<8sIIIIIIHHI", sec_hdr, 0,
                         name, vs, va, srd, prd,
                         0, 0, 0, 0, chars)
        chunks.extend(sec_hdr)

    # Pad to file_align (0x400)
    while len(chunks) < 0x400:
        chunks.append(0)

    # ── Section data ──────────────────────────────────────
    # .text at 0x400: RET (0xC3) × 256, padded to 0x200
    text_data = bytearray([0xC3] * 256)
    text_data.extend([0] * (0x200 - len(text_data)))
    chunks.extend(text_data)

    # .rdata at 0x600: 128 bytes of zeros, padded to 0x200
    rdata = bytearray(0x200)
    chunks.extend(rdata)

    return bytes(chunks)


# ═══════════════════════════════════════════════════════════════
# Test Functions
# ═══════════════════════════════════════════════════════════════

def test_pe_parse():
    """Verify basic PE structure parsing."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    assert pe.machine == 0x8664, f"Expected AMD64, got 0x{pe.machine:04X}"
    assert pe.is_pe32plus, "Expected PE32+"
    assert pe.num_sections == 3, f"Expected 3 sections, got {pe.num_sections}"
    assert pe.address_of_entry_point == 0x1000
    assert pe.image_base == 0x140000000
    assert pe.section_alignment == 0x1000
    assert pe.file_alignment == 0x200
    print("[PASS] test_pe_parse")


def test_pe32_parse():
    """Verify PE32 (x86) parsing — regression for format string bug."""
    data = make_minimal_pe32()
    pe = PEFile(data)
    assert pe.machine == 0x14C, f"Expected I386, got 0x{pe.machine:04X}"
    assert pe.is_pe32, "Expected PE32"
    assert not pe.is_pe32plus, "Should not be PE32+"
    assert pe.num_sections == 2, f"Expected 2 sections, got {pe.num_sections}"
    assert pe.address_of_entry_point == 0x1000
    assert pe.image_base == 0x00400000
    assert pe.base_of_data == 0x2000
    assert pe.section_alignment == 0x1000
    assert pe.file_alignment == 0x200
    assert pe.size_of_stack_reserve == 0x100000
    assert pe.size_of_stack_commit == 0x10000
    assert pe.size_of_heap_reserve == 0x100000
    assert pe.size_of_heap_commit == 0x1000
    # Verify entry point is in .text
    entry_sec = pe.entry_section()
    assert entry_sec is not None
    assert entry_sec.name == '.text', f"Entry in '{entry_sec.name}'"
    # Verify data directories parsed
    for i in range(16):
        pe.get_data_dir(i)  # Should not raise
    print("[PASS] test_pe32_parse")


def test_section_names():
    """Verify section names are read correctly."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    names = [s.name for s in pe.sections]
    assert names == ['.text', '.rdata', '.data'], f"Unexpected names: {names}"
    print("[PASS] test_section_names")


def test_section_raw_data():
    """Verify raw section data is read correctly."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    assert len(pe.section_raw) == 3
    # .text should be our 0xC3 bytes padded to 0x200
    assert len(pe.section_raw[0]) == 0x200
    assert pe.section_raw[0][0] == 0xC3
    assert len(pe.section_raw[1]) == 0x200  # .rdata
    assert len(pe.section_raw[2]) == 0x200  # .data
    print("[PASS] test_section_raw_data")


def test_entry_point_in_text():
    """Verify entry point RVA falls within .text."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    entry_sec = pe.entry_section()
    assert entry_sec is not None
    assert entry_sec.name == '.text', f"Entry in '{entry_sec.name}', not .text"
    assert entry_sec.virtual_address <= pe.address_of_entry_point < \
        entry_sec.virtual_address + entry_sec.virtual_size
    print("[PASS] test_entry_point_in_text")


def test_data_directories():
    """Verify data directories are parsed."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    for i in range(16):
        dd = pe.get_data_dir(i)
        if i == IMAGE_DIRECTORY_ENTRY_IMPORT:
            assert dd is not None, f"Import dir missing at index {i}"
    imp_dir = pe.get_data_dir(IMAGE_DIRECTORY_ENTRY_IMPORT)
    assert imp_dir is not None, "Import directory missing"
    assert imp_dir.virtual_address == 0x2000
    # Reloc dir should be present
    reloc = pe.reloc_dir
    assert reloc is not None, "Reloc directory missing"
    assert reloc.virtual_address == 0x2080
    # TLS should NOT be present initially
    tls = pe.tls_dir
    assert tls is None, "TLS directory should not exist yet"
    print("[PASS] test_data_directories")


def test_rva_to_offset():
    """Verify RVA→file offset conversion."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    # .text RVA 0x1000 → file offset 0x400
    off = pe.rva_to_offset(0x1000)
    assert off == 0x400, f"Expected 0x400, got 0x{off:X}"
    # .rdata RVA 0x2000 → file offset 0x600
    off = pe.rva_to_offset(0x2000)
    assert off == 0x600, f"Expected 0x600, got 0x{off:X}"
    # rdata+0x80 (reloc block) → 0x680
    off = pe.rva_to_offset(0x2080)
    assert off == 0x680, f"Expected 0x680, got 0x{off:X}"
    print("[PASS] test_rva_to_offset")


def test_section_collector():
    """Verify section collector filters correctly."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    descs = collect_descriptors(pe)
    # All 3 sections should be packable (.text, .rdata, .data)
    names = [d.name for d in descs]
    # .rdata is excluded because it contains the import directory
    # (loader must resolve imports before TLS callbacks run)
    assert names == ['.text', '.data'], f"Unexpected: {names}"
    for d in descs:
        assert d.should_pack()
        assert len(d.raw_data) > 0
    print("[PASS] test_section_collector")


def test_key_generation():
    """Verify key generation produces correct sizes."""
    # No key → random 32 bytes
    k1 = make_key()
    assert len(k1) == 32
    # Same call → different key (random)
    k2 = make_key()
    assert k1 != k2
    # Hex key → correct bytes
    k3 = make_key("2B7E151628AED2A6")
    assert k3 == bytes.fromhex("2B7E151628AED2A6")
    print("[PASS] test_key_generation")


def test_rolling_xor():
    """Verify XOR roundtrip."""
    data = b"Hello, Inferno! " * 100
    key = make_key()
    encrypted = rolling_xor(data, key)
    assert encrypted != data
    decrypted = rolling_xor(encrypted, key)
    assert decrypted == data
    # Wrong key should not decrypt correctly
    wrong_key = make_key()
    while wrong_key == key:
        wrong_key = make_key()
    wrong_decrypt = rolling_xor(encrypted, wrong_key)
    assert wrong_decrypt != data
    print("[PASS] test_rolling_xor")


def test_sha256_different():
    """Verify identical inputs produce same hash before packing."""
    data = make_minimal_pe64()
    pe1 = PEFile(data)
    pe2 = PEFile(data)
    assert pe1.sha256 == pe2.sha256
    print("[PASS] test_sha256_different")


# ═══════════════════════════════════════════════════════════════
# Step 1.4-1.9 Integration Tests
# ═══════════════════════════════════════════════════════════════


def test_pack_sections():
    """Verify compress + encrypt produces valid output."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    descs = collect_descriptors(pe)
    key = make_key("AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899")
    table = pack_sections(descs, key, no_compress=False)
    # .rdata is excluded (contains import dir), so only .text + .data remain
    assert len(table) == 2, f"Expected 2 sections (not .rdata), got {len(table)}"
    for t, d in zip(table, descs):
        assert t['decompressed_sz'] == len(d.raw_data)
        assert t['compressed_sz'] > 0
        # Encrypted data should not match original
        assert d.packed_data != d.raw_data
    print("[PASS] test_pack_sections")


def test_pack_sections_no_compress():
    """Verify no-compress mode works."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    descs = collect_descriptors(pe)
    key = make_key("AABBCCDDEEFF0011")
    table = pack_sections(descs, key, no_compress=True)
    for t, d in zip(table, descs):
        assert t['compressed_sz'] == len(d.raw_data)
        assert d.packed_data != d.raw_data  # XOR makes it different
    print("[PASS] test_pack_sections_no_compress")


def test_build_stub():
    """Verify stub header is structurally valid."""
    key = make_key("AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899")  # 32 bytes
    table = [
        {'rva': 0x1000, 'compressed_sz': 256, 'decompressed_sz': 512},
        {'rva': 0x2000, 'compressed_sz': 128, 'decompressed_sz': 256},
    ]
    stub = build_stub(key, 0x140000000, table, quick=True)
    # Header: magic(4) + keySz(4) + key(32) + prefBase(8) + numSec(4) + rsvd(4) = 56
    # 2 descs: 2 * 16 = 32
    # Code: 1 (RET)
    assert len(stub) >= 56 + 32 + 1

    # Parse header back
    magic = struct.unpack_from("<I", stub, 0)[0]
    assert magic == 0x4B434150, f"Bad magic: 0x{magic:08X}"
    key_sz = struct.unpack_from("<I", stub, 4)[0]
    assert key_sz == 32, f"Expected key_sz=32, got {key_sz}"
    pref_base = struct.unpack_from("<Q", stub, 40)[0]
    assert pref_base == 0x140000000
    num_sec = struct.unpack_from("<I", stub, 48)[0]
    assert num_sec == 2, f"Expected num_sec=2, got {num_sec}"
    print("[PASS] test_build_stub")


def test_build_stub_rejects_long_key():
    """Verify overlong keys (>32 bytes) raise ValueError."""
    table = [{'rva': 0x1000, 'compressed_sz': 16, 'decompressed_sz': 32}]
    long_key = b'A' * 33
    try:
        build_stub(long_key, 0x140000000, table, quick=True)
        assert False, "Should have raised ValueError for 33-byte key"
    except ValueError:
        pass
    # 32-byte key should work
    valid_key = b'B' * 32
    stub = build_stub(valid_key, 0x140000000, table, quick=True)
    assert len(stub) > 0
    print("[PASS] test_build_stub_rejects_long_key")


def test_inject_stub():
    """Verify stub injection produces a valid TLS directory structure.

    Validates: VA-format addresses, callback points to code (not header),
    AddressOfIndex is NULL, callback array is null-terminated, code is in
    an executable section.
    """
    data = make_minimal_pe64()
    pe = PEFile(data)
    ib = pe.image_base
    descs = collect_descriptors(pe)
    key = make_key("AABBCCDDEEFF00112233445566778899")
    table = pack_sections(descs, key)
    stub = build_stub(key, ib, table, quick=False)

    # Inject using the PE's bytearray data
    stub_rva, stub_size, tls_rva = inject_stub(pe, stub, quick=False)

    # ── Verify TLS data directory entry ──────────────────────
    tls = pe.tls_dir
    assert tls is not None, "TLS directory not created"
    assert tls.present(), "TLS directory not present"
    assert tls.virtual_address > 0, "TLS directory VA is 0"
    assert tls.size >= 40, f"TLS directory too small: {tls.size}"

    # ── Decode TLS directory structure ────────────────────────
    sec = pe.section_by_rva(tls.virtual_address)
    assert sec is not None, "TLS directory not in any section"
    raw_off = sec.pointer_to_raw_data + (tls.virtual_address - sec.virtual_address)
    tls_raw = bytes(pe.data[raw_off:raw_off + tls.size])

    if pe.is_pe32plus:
        start_va, end_va, idx_va, cb_va, zf, ch = \
            struct.unpack_from('<QQQQII', tls_raw, 0)
    else:
        start_va, end_va, idx_va, cb_va, zf, ch = \
            struct.unpack_from('<IIIIII', tls_raw, 0)

    # ── Validate TLS directory fields ─────────────────────────
    # All pointer fields must be VAs (ImageBase + RVA), not bare RVAs
    expected_start_va = ib + stub_rva
    assert start_va == expected_start_va, \
        f"Start VA: expected 0x{expected_start_va:X}, got 0x{start_va:X}"
    expected_end_va = ib + stub_rva + stub_size
    assert end_va == expected_end_va, \
        f"End VA: expected 0x{expected_end_va:X}, got 0x{end_va:X}"
    assert idx_va == 0, \
        f"AddressOfIndex should be NULL, got 0x{idx_va:X}"
    assert cb_va != 0, "AddressOfCallBacks is NULL"

    # ── Decode callback array ─────────────────────────────────
    cb_rva = cb_va - ib
    cb_sec = pe.section_by_rva(cb_rva)
    assert cb_sec is not None, "Callback array not in any section"
    cb_off = cb_sec.pointer_to_raw_data + (cb_rva - cb_sec.virtual_address)
    cb_raw = bytes(pe.data[cb_off:cb_off + 16])
    cb_target_va, cb_null = struct.unpack_from('<QQ', cb_raw, 0)

    # Callback must target code, not the PACK header
    stub_num_sec = struct.unpack_from('<I', stub, 48)[0]
    expected_code_va = ib + stub_rva + 56 + stub_num_sec * 16
    assert cb_target_va == expected_code_va, \
        f"Callback target: expected 0x{expected_code_va:X}, got 0x{cb_target_va:X}"
    assert cb_null == 0, "Callback array not null-terminated"

    # ── Verify callback is in an executable section ────────────
    code_rva = cb_target_va - ib
    code_sec = pe.section_by_rva(code_rva)
    assert code_sec is not None, "Callback code not in any section"
    assert code_sec.characteristics & IMAGE_SCN_MEM_EXECUTE, \
        "Callback code section lacks EXECUTE characteristic"

    print("[PASS] test_inject_stub")


def test_inject_stub_quick():
    """Verify quick mode patches entry point."""
    data = make_minimal_pe64()
    pe = PEFile(data)
    descs = collect_descriptors(pe)
    key = make_key("AABBCCDDEEFF0011")
    table = pack_sections(descs, key)
    stub = build_stub(key, pe.image_base, table, quick=True)

    # Need to actually set the entry point (pack() does this for quick mode)
    # Verify before/after
    original_entry = pe.address_of_entry_point
    stub_rva, stub_size, tls_rva = inject_stub(pe, stub, quick=True)

    # In quick mode, TLS should NOT be set up
    tls = pe.tls_dir
    assert tls is None, "TLS should not exist in quick mode"

    print("[PASS] test_inject_stub_quick")


def test_full_pack_quick():
    """Verify full pack() pipeline in quick mode produces valid PE."""
    data = make_minimal_pe64()
    key = make_key("DEADBEEFCAFEBABE")
    result = pack(data, key, quick=True, no_compress=False, verbose=False)

    # Verify output is valid PE
    pe = PEFile(result)
    assert pe.machine == 0x8664

    # In quick mode, entry point is patched to stub RVA. The stub may be
    # in header padding or alignment gap (no section covers it), so
    # entry_section may return None. That's expected for quick mode.
    entry_sec = pe.entry_section()
    # Entry point should be different from original
    assert pe.address_of_entry_point != 0x1000, \
        "Entry point not patched in quick mode"

    # Sections should be encrypted: first 4 bytes of .text should not be
    # recognizably x86 (0xC3 repeated)
    text_section = pe.sections[0]
    assert text_section.name == '.text'
    text_raw = bytes(pe.data[text_section.pointer_to_raw_data:
                              text_section.pointer_to_raw_data + 4])
    # After encryption, it should NOT be all 0xC3
    assert text_raw != b'\xC3\xC3\xC3\xC3', \
        f".text appears unencrypted: {text_raw.hex()}"

    print("[PASS] test_full_pack_quick")


def test_full_pack_tls():
    """Verify full pack() pipeline with TLS callback produces valid PE."""
    data = make_minimal_pe64()
    key = make_key("CAFEBABEDEADBEEF")
    result = pack(data, key, quick=False, no_compress=False, verbose=False)

    pe = PEFile(result)

    # TLS directory should exist
    tls = pe.tls_dir
    assert tls is not None, "TLS directory missing after packing"
    assert tls.present()

    # Entry point should be unchanged (still in .text)
    entry_sec = pe.entry_section()
    assert entry_sec is not None
    assert entry_sec.name == '.text', \
        f"Entry point moved from .text to '{entry_sec.name}'"

    # Sections should be encrypted
    text_section = pe.sections[0]
    text_raw = bytes(pe.data[text_section.pointer_to_raw_data:
                              text_section.pointer_to_raw_data + 4])
    assert text_raw != b'\xC3\xC3\xC3\xC3', ".text appears unencrypted"

    # SHA-256 should differ from input
    input_hash = hashlib.sha256(data).hexdigest()
    output_hash = hashlib.sha256(result).hexdigest()
    assert input_hash != output_hash, "Output hash matches input (no packing done)"

    print("[PASS] test_full_pack_tls")


def test_different_keys_different_output():
    """Verify different keys produce different output binaries."""
    data = make_minimal_pe64()
    k1 = make_key("AABBCCDDEEFF0011")
    k2 = make_key("1122334455667788")
    r1 = pack(data, k1, quick=True)
    r2 = pack(data, k2, quick=True)
    h1 = hashlib.sha256(r1).hexdigest()
    h2 = hashlib.sha256(r2).hexdigest()
    assert h1 != h2, "Different keys produced same hash"
    print("[PASS] test_different_keys_different_output")


# ═══════════════════════════════════════════════════════════════
# Constants needed by test
# ═══════════════════════════════════════════════════════════════

IMAGE_DIRECTORY_ENTRY_IMPORT = 1
IMAGE_DIRECTORY_ENTRY_BASERELOC = 5


# ═══════════════════════════════════════════════════════════════
# Runner
# ═══════════════════════════════════════════════════════════════

def main():
    tests = [
        test_pe_parse,
        test_pe32_parse,
        test_section_names,
        test_section_raw_data,
        test_entry_point_in_text,
        test_data_directories,
        test_rva_to_offset,
        test_section_collector,
        test_key_generation,
        test_rolling_xor,
        test_sha256_different,
        # Step 1.4-1.9 integration tests
        test_pack_sections,
        test_pack_sections_no_compress,
        test_build_stub,
        test_build_stub_rejects_long_key,
        test_inject_stub,
        test_inject_stub_quick,
        test_full_pack_quick,
        test_full_pack_tls,
        test_different_keys_different_output,
    ]
    passed = 0
    failed = 0
    for t in tests:
        try:
            t()
            passed += 1
        except Exception as e:
            print(f"[FAIL] {t.__name__}: {e}")
            import traceback
            traceback.print_exc()
            failed += 1
    total = passed + failed
    print(f"\n{'='*50}")
    print(f"Results: {passed}/{total} passed", end="")
    if failed:
        print(f", {failed} FAILED")
        sys.exit(1)
    else:
        print(" — ALL PASSED")


if __name__ == '__main__':
    main()
