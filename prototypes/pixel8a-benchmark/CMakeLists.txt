# Throwaway benchmark for #53. Builds for the desktop (system OpenCV) or for
# Android arm64-v8a (NDK toolchain + a static OpenCV core+imgproc), see run.sh.
cmake_minimum_required(VERSION 3.22)
project(adaptive_bench CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(MOSAIC ${REPO}/src/mosaic)
# Standalone glm (header-only).
set(GLM_INCLUDE_DIR "/usr/include" CACHE PATH "directory holding glm/glm.hpp (e.g. <OF_ROOT>/libs/glm/include)")
# Override to benchmark a variant of Adaptive.cpp (e.g. the eval-order fix).
set(ADAPTIVE_CPP "${MOSAIC}/Adaptive.cpp" CACHE FILEPATH "the Adaptive.cpp to build")
option(FP_CONTRACT_OFF "compile with -ffp-contract=off" OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc)

# The unmodified Adaptive.{h,cpp}, copied byte for byte next to each other so
# that its #include "Mosaic.h" finds the shim in this folder, not the real
# Mosaic.h next to the original.
set(GEN ${CMAKE_BINARY_DIR}/gen)
configure_file(${MOSAIC}/Adaptive.h ${GEN}/Adaptive.h COPYONLY)
configure_file(${ADAPTIVE_CPP} ${GEN}/Adaptive.cpp COPYONLY)
# The instrumented copy: the same file with timed_make.inc appended inside
# its namespace, compiled with adaptive renamed to adaptive_timed.
file(READ ${ADAPTIVE_CPP} ADAPTIVE_SRC)
string(REPLACE "} // namespace adaptive" "#include \"timed_make.inc\"\n} // namespace adaptive" TIMED_SRC "${ADAPTIVE_SRC}")
file(WRITE ${GEN}/Adaptive_timed.cpp "#include \"trace.h\"\n${TIMED_SRC}")

add_executable(bench bench.cpp ${GEN}/Adaptive.cpp ${GEN}/Adaptive_timed.cpp)
set_source_files_properties(${GEN}/Adaptive_timed.cpp PROPERTIES COMPILE_DEFINITIONS adaptive=adaptive_timed)
target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GEN} ${MOSAIC}/CDT ${GLM_INCLUDE_DIR})
target_link_libraries(bench PRIVATE ${OpenCV_LIBS})
if(FP_CONTRACT_OFF)
  target_compile_options(bench PRIVATE -ffp-contract=off)
endif()
if(ANDROID)
  target_link_options(bench PRIVATE -static-libstdc++)
endif()
