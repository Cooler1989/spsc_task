cmake_minimum_required(VERSION 3.16)

project(spsc VERSION 1.0.0 LANGUAGES CXX)

option(SPSC_BUILD_TESTS "Build unit and stress tests" ON)
option(SPSC_BUILD_BENCHMARKS "Build the throughput benchmark" ON)
set(SPSC_SANITIZER "" CACHE STRING "Sanitizer to enable for tests/benchmarks: address, thread, undefined or empty")

add_library(spsc INTERFACE)
add_library(spsc::spsc ALIAS spsc)
target_include_directories(spsc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(spsc INTERFACE cxx_std_17)

find_package(Threads REQUIRED)

# Applied globally so GoogleTest is instrumented too (avoids TSan false positives).
if(SPSC_SANITIZER)
    add_compile_options(-fsanitize=${SPSC_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${SPSC_SANITIZER})
endif()

function(spsc_configure_target target)
    target_link_libraries(${target} PRIVATE spsc::spsc Threads::Threads)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endfunction()

if(SPSC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SPSC_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
