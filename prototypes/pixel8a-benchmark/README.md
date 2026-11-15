# Throwaway: adaptive's speed on the Pixel 8a (#53)

A command-line benchmark of adaptive's pipeline. It is built with the NDK for arm64-v8a and run on the Pixel 8a over `adb shell`, and it is built the same way for the desktop. Measured 2026-11-15 on `origin/master` at `c2db62a`. **This is a prototype, not code to merge.**

## What it runs

- `src/mosaic/Adaptive.{h,cpp}`, **unmodified**. CMake copies them byte for byte into the build folder, so that their `#include "Mosaic.h"` finds the shim `Mosaic.h` here (the one from `research/adaptive-core`) and not the real one. The call is `adaptive::make(frame, 32)`, which is exactly what `MosaicMaker::make()` does in adaptive mode. It gives the end-to-end time and the Mosaic hash, which is the tests' FNV-1a.
- `adaptive_timed::makeTimed()` gives the time of each stage. It is generated from the same file, with `timed_make.inc` appended inside its namespace and `-Dadaptive=adaptive_timed`. It runs the same steps as `make()`, with `blurredLab()` inlined, and it hashes the output of every stage (hashing is not timed). Its Mosaic hash must equal `make()`'s, and the harness checks it does.
- OpenCV is 4.10.0 on both machines. On the phone it is core+imgproc, built statically with the NDK: carotene HAL on (OpenCV's default for ARM), KleidiCV off (4.10.0's default), pthreads. The desktop uses the system's 4.10.0.
- Flags are `-O3 -DNDEBUG -std=c++20` on both. The phone build uses NDK r29 clang 21 for `android-29` (FMA contraction on by default). The desktop build uses GCC 15.2 on an i7-11370H, the same flags as OF's Release build, with no `-march`.
- Frames are binary PPMs, decoded on the desktop: `fingers.png` and the other two fixtures, every 5th Frame of `fingers.mov` (44 Frames, 320×240), and 12 of the user's photos at long side 1024 (12), 2048 (6) and full resolution (4: one at 4032×3024 and three at 4624×3472). The photos and their copies stay in the scratch folder and are **not committed**.

`run.sh` does everything: it clones and builds OpenCV, builds the harness, prepares the Frames, runs on both machines and summarises. `phone-run.sh` is the part that runs on the device. Before you run them, read the paths at the top of both scripts.

## Results

Times are the median ms per Frame of `adaptive::make()` (about 5–50 repetitions after warm-up). The phone numbers are unpinned, and the scheduler puts the process on the X3 core. The desktop was re-measured at low load.

| Frame | Pixel 8a | Desktop i7-11370H | Peak RSS (phone) |
|---|---|---|---|
| `fingers.png` 320×240 | **17.8** (p90 18.1) | 16.1 | 20 MB |
| `fingers.mov`, 44 Frames 320×240 | mean 17.7, range 15.7–18.7 | mean 17.4, range 15.5–19.4 | 20 MB |
| photos, long side 1024 (12) | mean 185, range 151–233 | mean 153, range 127–179 | 113 MB |
| photos, long side 2048 (6) | mean **702**, range 604–809 | mean 602, range 489–710 | 416 MB |
| photo 4032×3024 (1) | 2874 | 2478 | 1.58 GB |
| photos 4624×3472 (3) | mean **3595**, range 3490–3652 | mean 3501 | 2.08 GB |

Stage breakdown in ms. Each figure is the mean of the per-Frame medians.

| Stage | 320×240 phone | 320×240 desktop | 2048 phone | 2048 desktop | 4624 phone | 4624 desktop |
|---|---|---|---|---|---|---|
| Lab (convertTo + cvtColor) | 0.93 | 0.60 | 54 | 20 | 193 | 94 |
| blur | 0.23 | 0.19 | 83 | 51 | 957 | 894 |
| FH | 5.71 | 6.29 | 273 | 286 | 1237 | 1312 |
| join equal colours | 1.23 | 1.41 | 38 | 42 | 182 | 175 |
| Ward | 3.72 | 3.52 | 85 | 73 | 305 | 259 |
| absorb pieces | 1.38 | 1.25 | 59 | 41 | 321 | 280 |
| chains | 1.90 | 0.95 | 62 | 29 | 319 | 144 |
| simplification | 0.44 | 0.34 | 5 | 3 | 19 | 11 |
| CDT | 1.26 | 0.97 | 5 | 3 | 9 | 6 |
| colours | 1.22 | 1.56 | 32 | 44 | 151 | 210 |

The phone's X3 core is about as fast as this laptop core: within 5% at 320×240 and 15–20% slower on photos. FH is the largest stage at every size. The blur grows fastest, because the kernel grows with σ (43 taps at 2048, about 90 at 12 MP).

### Cores, concurrency, thermals (320×240, `fingers.png`)

- **Pinned with `taskset`:** cpu 8 (X3, 2.91 GHz) 17.8 ms; cpu 4 (A715, 2.37 GHz) 27.3 ms; cpu 0 (A510, 1.70 GHz) 123 ms. Unpinned runs give the X3 time.
- **Concurrent processes, aggregate fps:** 1 process gives 54 fps. Unpinned, 2 processes give 50 fps (the scheduler stacked them) and 3 give 118. Pinned to X3 + A715, 2 processes give 53 + 37 = 89 fps. Pinned to X3 + 2× A715, 3 processes give 47 + 36 + 36 = 118 fps.
- **Sustained 60 s:** 52–54 Frames per second all the way through, with a per-second median of 18.5–19.1 ms. There was no throttling trend, only one blip (second 49: median 20.3, max 60 ms). The thermal service's cached temperatures did not change during the run (BIG 53 °C), so they say nothing.
- **CPU:** SVE and SVE2 are present (HWCAP and `/proc/cpuinfo`), with a 128-bit vector length (`PR_SVE_GET_VL` = 16 bytes). The max frequencies are 1.704 GHz (cpu 0–3), 2.367 GHz (cpu 4–7) and 2.914 GHz (cpu 8).

## Recommendations

- **Live preview at 320×240: feasible, 30 fps included, if the analysis thread runs on the X3.** The core takes 17.8 ms median and at most about 19 ms (54 fps). With 5–10 ms for camera, conversion and drawing, that is 23–28 ms, or 35–43 fps. On an A715 the core takes 27 ms: 15 fps is safe there, 30 fps is not. On an A510 it takes 123 ms, which is not usable. The time did not degrade over 60 s.
- **Photo cap: keep 2048.** A photo takes 0.6–0.8 s at 2048 and 416 MB peak. At full resolution it takes 2.9–3.7 s and 1.6–2.1 GB peak. That ran under `adb shell`, but in an app process it risks the heap limit or the low-memory killer, and it is right at "a few seconds". Long side 1024 gives 0.15–0.23 s.

## Determinism: phone vs desktop

**The unmodified code gives a different Mosaic hash on the phone:** `fingers.png` gives `0x6ee7de8809149532` against the golden `0xe0d145733a499498`. The cause is **not** the blur, FMA or NEON:

- Every stage hash up to the simplified chains is **bit-identical** between the phone and the desktop: the float RGB, Lab, **blurred Lab**, FH labels, joins, Ward, absorb, exact chains and simplified chains. This holds for all three fixtures, so OpenCV's NEON/carotene float blur and clang's FMA contraction produced the same bits as AVX2/GCC here.
- The first stage that differs is the CDT output, and the vertex-only triangulation is identical. A desktop x86-64 build with NDK clang + libc++ reproduces the phone hashes exactly, and GCC with `-mfma -ffp-contract=fast` does not change the golden. So the cause is the compiler, not the CPU.
- **Root cause:** in `triangulate()` (`Adaptive.cpp:589`), `edges.push_back(CDT::Edge(vertex(chain.points[i]), vertex(chain.points[i + 1])));` has two `vertex()` calls as constructor arguments. `vertex()` assigns indices on first sight, and **the evaluation order of function arguments is unspecified**: GCC evaluates right to left, clang left to right. So the CDT gets its vertices in a different order and produces another valid CDT. Its Tessels come out in a different order (`fingers.png`, `fingers-060.png`: the same set, see the `set=` hash), or, where points are cocircular, with different diagonals (`fingers-003.png`). This also means a macOS (Apple clang) desktop build would fail the golden today.
- **With the two calls sequenced** in GCC's order (`eval-order-fix.py`: `b = vertex(p[i+1]); a = vertex(p[i]);`), the phone build reproduces the desktop hashes on **all 69 inputs**: the 3 fixtures (golden `0xe0d145733a499498` included), the 44 `fingers.mov` Frames, and the 22 photos, 12 MP ones included. The desktop golden does not change, because the fix keeps GCC's order. Under option (A) of `research/adaptive-core` (keep OpenCV), desktop == phone is then shown on these inputs. It is still not guaranteed by construction for the blur.

## Not done

- KleidiCV on: not measured. 4.10.0 can build it (`WITH_KLEIDICV`, off by default), but it accelerates neither float RGB→Lab nor float GaussianBlur, and Lab + blur together are 1.2 of the 18 ms at 320×240.
- The #55 "V1" exact rewrites were not tried.
- Camera, YUV→RGB conversion and drawing are not included. These are core-only numbers.
