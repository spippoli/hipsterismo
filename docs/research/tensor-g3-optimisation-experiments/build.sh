#!/bin/sh
# Throwaway: build the profiling harness and the exact variants, OF-free.
# Run from the repo root: docs/research/tensor-g3-optimisation-experiments/build.sh
# Needs OpenCV 4 (pkg-config opencv4) and glm (OF ships it in libs/glm).
set -e
E=docs/research/tensor-g3-optimisation-experiments
B=${BUILD:-.exp-build}
GLM=${GLM:-/home/dario/Applications/of/libs/glm/include}
mkdir -p "$B/v"
python3 $E/instrument.py src/mosaic/Adaptive.cpp "$B/inst.cpp"
python3 $E/variants.py "$B/inst.cpp" "$B/v/Adaptive.cpp"
cp src/mosaic/Adaptive.h "$B/v/"
build() {
    name=$1
    shift
    g++ -std=c++20 -O3 -DNDEBUG "$@" -I$E/shim -I"$B/v" -Isrc/mosaic/CDT -I"$GLM" $(pkg-config --cflags opencv4) \
        "$B/v/Adaptive.cpp" $E/profile.cpp -o "$B/v/$name" $(pkg-config --libs opencv4) -pthread 2>&1 | grep -E 'error' || true
}
build base &
build pairs -DFAST_PAIRS &
build edges -DFAST_EDGES &
build colour -DFAST_COLOUR &
build all -DFAST_PAIRS -DFAST_EDGES -DFAST_COLOUR &
build allt4 -DFAST_PAIRS -DFAST_EDGES -DFAST_COLOUR -DTHREADS=4 &
wait
ls "$B/v"
