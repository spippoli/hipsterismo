# Tests of the openFrameworks-free adaptive core, with plain CMake and no
# openFrameworks: the core (src/mosaic/core/) and the vendored CDT, linked
# against OpenCV core+imgproc, plus zlib for the PNG fixtures.
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

find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(ZLIB REQUIRED)

# The core, with only its own folder and CDT on the include path: an
# openFrameworks (or glm) include would fail to compile here.
add_library(adaptive_core STATIC ${CORE}/AdaptiveCore.cpp)
target_include_directories(adaptive_core PUBLIC ${CORE})
target_include_directories(adaptive_core SYSTEM PRIVATE ${REPO}/src/mosaic/CDT ${OpenCV_INCLUDE_DIRS})
target_link_libraries(adaptive_core PUBLIC opencv_core opencv_imgproc)
# No fused multiply-adds: clang contracts a*b+c by default where the target
# has FMA (arm64), which would change the floats and the hashes.
target_compile_options(adaptive_core PRIVATE -Wall -Wextra -ffp-contract=off)

add_executable(core_tests main.cpp Png.cpp)
target_link_libraries(core_tests PRIVATE adaptive_core ZLIB::ZLIB)
target_compile_definitions(core_tests PRIVATE FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures")
target_compile_options(core_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
