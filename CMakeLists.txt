cmake_minimum_required(VERSION 3.14)
project(MemorySimulator
    VERSION 1.0.0
    DESCRIPTION "DRAM & Cache Memory Simulator: A Configurable Memory Hierarchy and Performance Analysis Framework"
    LANGUAGES CXX
)

# ============================================================================
# Build Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX- /permissive-)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion
        -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual
        -Wnon-virtual-dtor
    )
    # Debug builds: enable sanitizers on supported platforms
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT WIN32)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ============================================================================
# Core Library
# ============================================================================
add_library(memsim_core
    src/cache.cpp
    src/cache_hierarchy.cpp
    src/dram.cpp
    src/memory_controller.cpp
    src/simulator.cpp
    src/trace_loader.cpp
    src/workload_generator.cpp
    src/statistics.cpp
)

target_include_directories(memsim_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(memsim_core PUBLIC cxx_std_17)

# ============================================================================
# Main Executable
# ============================================================================
add_executable(memory_sim src/main.cpp)
target_link_libraries(memory_sim PRIVATE memsim_core)

# ============================================================================
# Testing
# ============================================================================
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    # Fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Test executables
    add_executable(cache_tests tests/cache_tests.cpp)
    target_link_libraries(cache_tests PRIVATE memsim_core GTest::gtest_main)

    add_executable(dram_tests tests/dram_tests.cpp)
    target_link_libraries(dram_tests PRIVATE memsim_core GTest::gtest_main)

    add_executable(hierarchy_tests tests/hierarchy_tests.cpp)
    target_link_libraries(hierarchy_tests PRIVATE memsim_core GTest::gtest_main)

    add_executable(integration_tests tests/integration_tests.cpp)
    target_link_libraries(integration_tests PRIVATE memsim_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(cache_tests)
    gtest_discover_tests(dram_tests)
    gtest_discover_tests(hierarchy_tests)
    gtest_discover_tests(integration_tests)
endif()

# ============================================================================
# Install
# ============================================================================
install(TARGETS memory_sim RUNTIME DESTINATION bin)
install(DIRECTORY include/memsim DESTINATION include)
