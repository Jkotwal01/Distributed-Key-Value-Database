cmake_minimum_required(VERSION 3.16)
project(DistributedDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Output directories ───────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─── Include paths ────────────────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ─── Platform socket libraries ────────────────────────────────────────────────
if(WIN32)
    set(SOCKET_LIBS ws2_32)
    add_compile_definitions(PLATFORM_WINDOWS)
else()
    set(SOCKET_LIBS "")
    add_compile_definitions(PLATFORM_LINUX)
endif()

# ─── Compiler warnings ────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W3 /WX-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ─── Node source files (all modules) ─────────────────────────────────────────
set(NODE_SOURCES
    node/utils.cpp
    node/storage_engine.cpp
    node/hash_ring.cpp
    node/tcp_server.cpp
    node/heartbeat.cpp
    node/leader_election.cpp
    node/replication.cpp
    node/coordinator.cpp
    node/main.cpp
)

# ─── Main node binary ─────────────────────────────────────────────────────────
add_executable(distributed_node ${NODE_SOURCES})
target_link_libraries(distributed_node PRIVATE ${SOCKET_LIBS})

# Enable threading
find_package(Threads REQUIRED)
target_link_libraries(distributed_node PRIVATE Threads::Threads)

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

# Storage engine unit test
add_executable(test_storage
    tests/test_storage.cpp
    node/utils.cpp
    node/storage_engine.cpp
)
target_link_libraries(test_storage PRIVATE ${SOCKET_LIBS} Threads::Threads)
add_test(NAME StorageTest COMMAND test_storage)

# Hash ring unit test
add_executable(test_hash_ring
    tests/test_hash_ring.cpp
    node/utils.cpp
    node/hash_ring.cpp
)
target_link_libraries(test_hash_ring PRIVATE ${SOCKET_LIBS} Threads::Threads)
add_test(NAME HashRingTest COMMAND test_hash_ring)

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS distributed_node DESTINATION bin)
