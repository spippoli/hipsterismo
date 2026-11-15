# Tests of the openFrameworks-free adaptive core, with plain CMake and no
# openFrameworks or OpenCV: the core (src/mosaic/core/) and the vendored CDT,
# plus zlib for the PNG fixtures.
#
#   cmake -S tests/core -B tests/core/build && cmake --build tests/core/build -j
#   tests/core/build/core_tests
#
# With the NDK's host clang (same hashes as GCC):
#   cmake -S tests/core -B tests/core/build-clang -DCMAKE_CXX_COMPILER=<ndk>/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++
cmake_minimum_required(VERSION 3.22)
project(hipsterismo_core_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CORE ${REPO}/src/mosaic/core)

find_package(ZLIB REQUIRED)

# The core's library, with the flags that keep its floats identical
# (-ffp-contract=off), shared with the Android app.
add_subdirectory(${CORE} core)

add_executable(core_tests main.cpp Png.cpp)
target_link_libraries(core_tests PRIVATE adaptive_core ZLIB::ZLIB)
target_compile_definitions(core_tests PRIVATE FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures")
target_compile_options(core_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
