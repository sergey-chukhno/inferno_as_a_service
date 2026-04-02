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
# 2. Common Library (Networking & Protocol)
# ==========================================
add_library(inferno_common STATIC
    common/src/Socket.cpp
)
target_include_directories(inferno_common PUBLIC common/include)

# ==========================================
# 3. Server Executable
# ==========================================
add_executable(inferno_server
    server/src/main.cpp
    server/src/server.cpp
)
target_link_libraries(inferno_server PRIVATE inferno_common)

# ==========================================
# 4. Client Agent Executable 
# ==========================================
add_executable(inferno_client
    client/src/main.cpp
)
target_link_libraries(inferno_client PRIVATE inferno_common)

# ==========================================
# 5. TDD Infrastructure
# ==========================================
enable_testing()
add_executable(inferno_tests
    tests/main_test.cpp
    tests/socket_test.cpp
    tests/server_test.cpp
    server/src/server.cpp
)
target_link_libraries(inferno_tests PRIVATE inferno_common)
add_test(NAME CoreNetworkingTests COMMAND inferno_tests)
