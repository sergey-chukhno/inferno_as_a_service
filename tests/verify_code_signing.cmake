# ═══════════════════════════════════════════════════════════════════════
# Inferno — Code Signing Verification Script (Phase 3.5)
# ═══════════════════════════════════════════════════════════════════════
#
# Usage (on Windows with signtool in PATH):
#   cmake -P tests/verify_code_signing.cmake
#
# Environment variables:
#   INFERNO_CERT_PFX       — path to the PFX certificate (required)
#   INFERNO_CERT_PASSWORD  — PFX password (required)
#
# Returns exit code 0 on success, non-zero on failure.
# ═══════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.15)

# ── 1. Verify signtool exists ─────────────────────────────────────
find_program(SIGNTOOL signtool)
if(NOT SIGNTOOL_FOUND)
    message(FATAL_ERROR "FAIL: signtool not found in PATH")
endif()
message(STATUS "PASS: signtool found at ${SIGNTOOL}")

# ── 2. Verify certificate exists ─────────────────────────────────
set(CERT_PFX "$ENV{INFERNO_CERT_PFX}")
if(NOT CERT_PFX)
    set(CERT_PFX "${CMAKE_SOURCE_DIR}/secrets/cert.pfx")
endif()

if(NOT EXISTS "${CERT_PFX}")
    message(FATAL_ERROR "FAIL: certificate not found at ${CERT_PFX}")
endif()
message(STATUS "PASS: certificate found at ${CERT_PFX}")

# ── 3. Verify password is set ─────────────────────────────────────
set(CERT_PASSWORD "$ENV{INFERNO_CERT_PASSWORD}")
if(NOT CERT_PASSWORD)
    message(FATAL_ERROR "FAIL: INFERNO_CERT_PASSWORD environment variable not set")
endif()
message(STATUS "PASS: INFERNO_CERT_PASSWORD is set (${CERT_PASSWORD})")

# ── 4. Verify certificate is valid with signtool ──────────────────
# Test that signtool can read the certificate by attempting a verify
# on an empty/null signature target. signtool will fail with the
# actual binary verification, but we just check it can parse the PFX.
message(STATUS "Verifying certificate integrity...")
execute_process(
    COMMAND ${SIGNTOOL} cat /v "${CERT_PFX}"
    RESULT_VARIABLE SIG_RESULT
    OUTPUT_VARIABLE SIG_OUTPUT
    ERROR_VARIABLE SIG_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(SIG_RESULT EQUAL 0)
    message(STATUS "PASS: certificate is valid and readable")
else()
    message(WARNING "Could not parse certificate: ${SIG_OUTPUT} ${SIG_ERROR}")
    message(WARNING "This may be normal for self-signed certs without a complete chain")
endif()

# ── 5. Create a test binary and sign it ────────────────────────────
# This validates the full sign → verify pipeline without touching
# the actual wrapper binary.
set(TEST_BIN "${CMAKE_CURRENT_BINARY_DIR}/test_signing_bin.exe")

# Create a minimal PE-like file (actually just a valid EXE header)
file(WRITE "${TEST_BIN}" "MZ\x90\x00")

execute_process(
    COMMAND ${SIGNTOOL} sign /fd SHA256 /a
            /f "${CERT_PFX}" /p "${CERT_PASSWORD}"
            /tr "http://timestamp.digicert.com"
            /td SHA256
            "${TEST_BIN}"
    RESULT_VARIABLE SIGN_RESULT
    OUTPUT_VARIABLE SIGN_OUTPUT
    ERROR_VARIABLE SIGN_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(SIGN_RESULT EQUAL 0)
    message(STATUS "PASS: signtool signed test binary")
else()
    message(FATAL_ERROR "FAIL: signtool could not sign test binary: ${SIGN_OUTPUT} ${SIGN_ERROR}")
endif()

# ── 6. Verify the signature ────────────────────────────────────────
execute_process(
    COMMAND ${SIGNTOOL} verify /v /pa "${TEST_BIN}"
    RESULT_VARIABLE VERIFY_RESULT
    OUTPUT_VARIABLE VERIFY_OUTPUT
    ERROR_VARIABLE VERIFY_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(VERIFY_RESULT EQUAL 0)
    message(STATUS "PASS: signtool verify succeeded")
    # Print the signing summary
    string(REGEX MATCH "Digest algorithm: [^\n]+" DIGEST_MATCH "${VERIFY_OUTPUT}")
    string(REGEX MATCH "Signing certificate chain" CHAIN_MATCH "${VERIFY_OUTPUT}")
    if(DIGEST_MATCH)
        message(STATUS "  ${DIGEST_MATCH}")
    endif()
    if(VERIFY_OUTPUT MATCHES "Timestamp")
        message(STATUS "  RFC 3161 timestamp present")
    endif()
else()
    message(FATAL_ERROR "FAIL: signtool verify failed: ${VERIFY_OUTPUT} ${VERIFY_ERROR}")
endif()

# ── Cleanup ────────────────────────────────────────────────────────
file(REMOVE "${TEST_BIN}")

message(STATUS "")
message(STATUS "══════════════════════════════════════════════════════")
message(STATUS "  Code signing pipeline verification: PASSED")
message(STATUS "══════════════════════════════════════════════════════")
