# ═══════════════════════════════════════════════════════════════════════
# Inferno — Code Signing Verification Script (Phase 3.5)
# ═══════════════════════════════════════════════════════════════════════
#
# Validates that the code signing pipeline is operational: signtool is
# present, the PFX certificate is readable, and sign+verify succeeds on
# a known-valid minimal PE binary.
#
# Usage (on Windows with signtool in PATH):
#   cmake -P tests/verify_code_signing.cmake
#
# Environment variables:
#   INFERNO_CERT_PFX       — path to the PFX certificate (required)
#   INFERNO_CERT_PASSWORD  — PFX password (required)
#   INFERNO_PE_BINARY      — optional: path to a real PE to sign. If not
#                             set, a synthetic minimal PE is used.
#
# Returns exit code 0 on success, non-zero on failure.
# ═══════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.15)

# ── 1. Check Python (needed for PE generation) ─────────────────────
find_package(Python3 QUIET)
if(NOT Python3_FOUND)
    message(FATAL_ERROR "FAIL: Python3 required for PE generation")
endif()

# ── 2. Verify signtool exists ─────────────────────────────────────
find_program(SIGNTOOL signtool)
if(NOT SIGNTOOL_FOUND)
    message(FATAL_ERROR "FAIL: signtool not found in PATH")
endif()
message(STATUS "PASS: signtool found at ${SIGNTOOL}")

# ── 3. Verify certificate exists ──────────────────────────────────
set(CERT_PFX "$ENV{INFERNO_CERT_PFX}")
if(NOT CERT_PFX)
    set(CERT_PFX "${CMAKE_SOURCE_DIR}/secrets/cert.pfx")
endif()

if(NOT EXISTS "${CERT_PFX}")
    message(FATAL_ERROR "FAIL: certificate not found at ${CERT_PFX}")
endif()
message(STATUS "PASS: certificate found at ${CERT_PFX}")

# ── 4. Verify password is set ─────────────────────────────────────
set(CERT_PASSWORD "$ENV{INFERNO_CERT_PASSWORD}")
if(NOT CERT_PASSWORD)
    message(FATAL_ERROR "FAIL: INFERNO_CERT_PASSWORD environment variable not set")
endif()
message(STATUS "PASS: INFERNO_CERT_PASSWORD is set")

# ── 5. Generate or locate a valid PE binary ───────────────────────
set(PE_BINARY "$ENV{INFERNO_PE_BINARY}")
if(NOT PE_BINARY)
    set(PE_BINARY "${CMAKE_CURRENT_BINARY_DIR}/test_signing_pe.exe")
    message(STATUS "Generating minimal valid PE at ${PE_BINARY}...")

    # Generate a minimal PE32+ (x64) binary via Python.
    # This binary has a valid DOS header, PE signature, COFF header,
    # optional header, one .text section, and a single RET instruction.
    # It is structurally complete enough for signtool to sign.
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "
import struct, os

# Minimal PE32+ (x64) binary
# ── DOS header (64 bytes)
dos = bytearray(64)
dos[0:2] = b'MZ'
struct.pack_into('<I', dos, 0x3C, 0x40)  # e_lfanew = 0x40

# ── PE signature + COFF header (24 bytes)
coff = struct.pack('<HHIIIHH',
    0x8664,  # Machine: x64
    1,       # NumberOfSections
    0, 0, 0, # Timestamp, symtab ptr, sym count
    0xF4,    # SizeOfOptionalHeader (244 bytes for PE32+)
    0x02     # Characteristics: EXECUTABLE_IMAGE
)

# ── PE32+ optional header (244 bytes)
opt = bytearray(244)
struct.pack_into('<H', opt, 0,   0x020B)  # Magic: PE32+
struct.pack_into('<I', opt, 16,  0x1000)   # AddressOfEntryPoint
struct.pack_into('<Q', opt, 20,  0x1000)   # BaseOfCode
struct.pack_into('<Q', opt, 28,  0x140000000)  # ImageBase
struct.pack_into('<I', opt, 36,  0x1000)   # SectionAlignment
struct.pack_into('<I', opt, 40,  0x200)    # FileAlignment
struct.pack_into('<H', opt, 52,  5)        # MajorSubsystemVersion
struct.pack_into('<H', opt, 54,  2)        # MinorSubsystemVersion
struct.pack_into('<I', opt, 60,  0x2000)   # SizeOfImage
struct.pack_into('<I', opt, 64,  0x200)    # SizeOfHeaders
struct.pack_into('<H', opt, 72,  2)        # Subsystem: GUI
struct.pack_into('<Q', opt, 76,  0x100000) # SizeOfStackReserve
struct.pack_into('<Q', opt, 84,  0x1000)   # SizeOfStackCommit
struct.pack_into('<Q', opt, 92,  0x100000) # SizeOfHeapReserve
struct.pack_into('<Q', opt, 100, 0x1000)   # SizeOfHeapCommit
struct.pack_into('<I', opt, 112, 16)       # NumberOfRvaAndSizes
# Data directories (128 bytes) — all zero

# ── Section header: .text (40 bytes)
sec = struct.pack('<8sIIIIIIHHI',
    b'.text\x00\x00\x00',
    0x1000,         # VirtualSize
    0x1000,         # VirtualAddress
    0x200,          # SizeOfRawData
    0x200,          # PointerToRawData (right after headers)
    0, 0, 0, 0,
    0x60000020      # CODE | EXECUTE | READ
)

# ── Section data: single RET instruction
text_data = b'\\xC3'  # ret

# ── Assemble
pe = bytearray()
pe.extend(dos)
# Pad to e_lfanew (0x40)
pe.extend(b'\\x00' * (0x40 - len(dos)))
pe.extend(b'PE\\x00\\x00')
pe.extend(coff)
pe.extend(opt)
# Section table starts at 0x40 + 4 + 20 + 244 = 0x14C
while len(pe) < 0x14C:
    pe.append(0)
pe.extend(sec)
# Section data at file offset 0x200
while len(pe) < 0x200:
    pe.append(0)
pe[0x200:0x201] = text_data

os.write(1, bytes(pe))
"
        OUTPUT_FILE "${PE_BINARY}"
        RESULT_VARIABLE GEN_RESULT
        ERROR_VARIABLE GEN_ERROR
    )

    if(NOT GEN_RESULT EQUAL 0)
        file(REMOVE "${PE_BINARY}")
        message(FATAL_ERROR "FAIL: could not generate valid PE: ${GEN_ERROR}")
    endif()
    message(STATUS "PASS: valid PE generated (${PE_BINARY})")
else()
    if(NOT EXISTS "${PE_BINARY}")
        message(FATAL_ERROR "FAIL: INFERNO_PE_BINARY not found at ${PE_BINARY}")
    endif()
    message(STATUS "PASS: using existing PE binary at ${PE_BINARY}")
endif()

# ── 6. Sign the PE binary ─────────────────────────────────────────
message(STATUS "Signing PE binary...")
execute_process(
    COMMAND ${SIGNTOOL} sign /fd SHA256 /a
            /f "${CERT_PFX}" /p "${CERT_PASSWORD}"
            /tr "http://timestamp.digicert.com"
            /td SHA256
            "${PE_BINARY}"
    RESULT_VARIABLE SIGN_RESULT
    OUTPUT_VARIABLE SIGN_OUTPUT
    ERROR_VARIABLE SIGN_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(SIGN_RESULT EQUAL 0)
    message(STATUS "PASS: signtool signed PE binary")
else()
    file(REMOVE "${PE_BINARY}")
    message(FATAL_ERROR "FAIL: signtool sign failed: ${SIGN_OUTPUT} ${SIGN_ERROR}")
endif()

# ── 7. Verify the signature ───────────────────────────────────────
message(STATUS "Verifying signature...")
execute_process(
    COMMAND ${SIGNTOOL} verify /v /pa "${PE_BINARY}"
    RESULT_VARIABLE VERIFY_RESULT
    OUTPUT_VARIABLE VERIFY_OUTPUT
    ERROR_VARIABLE VERIFY_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(VERIFY_RESULT EQUAL 0)
    message(STATUS "PASS: signtool verify succeeded")
    string(REGEX MATCH "Digest algorithm: [^\n]+" DIGEST_MATCH "${VERIFY_OUTPUT}")
    if(DIGEST_MATCH)
        message(STATUS "  ${DIGEST_MATCH}")
    endif()
    if(VERIFY_OUTPUT MATCHES "Timestamp")
        message(STATUS "  RFC 3161 timestamp present")
    endif()
else()
    file(REMOVE "${PE_BINARY}")
    message(FATAL_ERROR "FAIL: signtool verify failed: ${VERIFY_OUTPUT} ${VERIFY_ERROR}")
endif()

# ── Cleanup ────────────────────────────────────────────────────────
file(REMOVE "${PE_BINARY}")

message(STATUS "")
message(STATUS "══════════════════════════════════════════════════════")
message(STATUS "  Code signing pipeline verification: PASSED")
message(STATUS "══════════════════════════════════════════════════════")
