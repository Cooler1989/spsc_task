find_package(GTest 1.10 QUIET)

if(NOT GTest_FOUND)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(spsc_tests
    queue_test.cpp
    concurrency_test.cpp)
spsc_configure_target(spsc_tests)
target_link_libraries(spsc_tests PRIVATE GTest::gtest_main)
if(SPSC_SANITIZER)
    # Sanitizers slow execution by an order of magnitude; keep stress tests short.
    target_compile_definitions(spsc_tests PRIVATE SPSC_STRESS_ITEMS=100000)
endif()

gtest_discover_tests(spsc_tests DISCOVERY_TIMEOUT 30)
