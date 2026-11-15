# The openFrameworks-free adaptive core as a static library, for the plain-CMake
# builds: the core tests (tests/core/) and the Android app
# (android/app/src/main/cpp/). Both add this folder with add_subdirectory(), so
# the flags that keep the floats identical live in one place. The
# openFrameworks makefiles ignore this file and compile the sources directly.
#
# Only this folder and the vendored CDT are on the include path and no library
# is linked but the system's threads (std::thread, for photo-sized Frames, see
# AdaptiveParallel.h): an openFrameworks, glm or OpenCV include fails to
# compile.
add_library(adaptive_core STATIC AdaptiveCore.cpp AdaptiveLab.cpp)
target_compile_features(adaptive_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(adaptive_core PUBLIC Threads::Threads)
target_include_directories(adaptive_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(adaptive_core SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../CDT)
# No fused multiply-adds: clang contracts a*b+c by default where the target
# has FMA (arm64), which would change the floats and the hashes.
target_compile_options(adaptive_core PRIVATE -Wall -Wextra -ffp-contract=off)
