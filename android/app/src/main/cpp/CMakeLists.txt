# The native library of the Android app: the shared adaptive core
# (src/mosaic/core/, with its -ffp-contract=off) and the JNI glue.
cmake_minimum_required(VERSION 3.22)
project(hipsterismo_android CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
add_subdirectory(${REPO}/src/mosaic/core core)
# The debug APK still runs the core optimised, as the tests do: -O3 comes after
# the build type's -O0. The hashes don't depend on it (no FMA contraction).
target_compile_options(adaptive_core PRIVATE -O3)

add_library(hipsterismo SHARED mosaic_jni.cpp)
# The glue copies every pixel of every preview Frame: optimise it like the core.
target_compile_options(hipsterismo PRIVATE -Wall -Wextra -O3)
target_link_libraries(hipsterismo PRIVATE adaptive_core jnigraphics log)
