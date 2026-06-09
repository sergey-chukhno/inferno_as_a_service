# ==========================================
# Inferno Agent Wrapper / Dropper (Phase 2.3)
# ==========================================

find_package(Python3 QUIET)
if(NOT Python3_FOUND)
    message(WARNING "Python3 not found — skipping inferno_wrapper build")
    return()
endif()

set(AGENT_BINARY_HEADER "${CMAKE_CURRENT_BINARY_DIR}/agent_binary.h")

add_custom_command(
    OUTPUT  ${AGENT_BINARY_HEADER}
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/bin2header.py"
            "${CMAKE_BINARY_DIR}/inferno_client${CMAKE_EXECUTABLE_SUFFIX}"
            "${AGENT_BINARY_HEADER}"
    DEPENDS  inferno_client
    COMMENT "Generating agent_binary.h from inferno_client binary"
)

add_executable(inferno_wrapper
    src/main.cpp
    ${AGENT_BINARY_HEADER}
)
target_include_directories(inferno_wrapper PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if(WIN32)
    target_link_libraries(inferno_wrapper PRIVATE shell32)
endif()
