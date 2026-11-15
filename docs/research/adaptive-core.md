# Research: what adaptive needs from openFrameworks and OpenCV, and an OF-free core

Ticket: #51 (part of map #48). The question: what do adaptive's pipeline (`src/mosaic/Adaptive.*`) and the parts of `MosaicMaker` it goes through depend on from openFrameworks, ofxCv and OpenCV? What would a plain-C++ core look like, how would the desktop tests and golden hashes keep guarding it, and how would both builds compile it?

Researched 2026-11-15 on `origin/master` at `c2db62a`. The primary sources are the code itself, OpenCV 4.10.0's sources (the version on this machine, `pkg-config --modversion opencv4`), and experiments run on this machine: Intel i7-11370H (AVX2 and AVX-512), GCC 15.2, and NDK 29.0.14206865 clang 21. The experiment sources are in [`adaptive-core-experiments/`](adaptive-core-experiments/). **This document decides nothing.** It feeds #53/#54 and the spec.

Related: the sibling study `docs/research/android-native-app.md` (#50, branch `research/android-native-app`) §3.3 and §7 covers the same seam from the Android side. Some of its caveats are sharpened here. §2 shows that the Lab conversion *can* be ported bit-identically. §2.4 confirms the NDK clang FMA contraction it marked unverified.

---

## TL;DR

1. **Adaptive's OF surface is small and all at the edges.** In: `ofPixels`, read through `getWidth/getHeight/getData/getColor`. Out: `Tessel{std::array<glm::vec3,3>, ofColor}` and `ofPolyline` (`addVertex`, `setClosed`). Also `ofLogWarning/ofLogError`, `ofClamp` and `ofxCv::toCv`. The algorithm itself uses glm vector types, the vendored CDT and the standard library.
2. **OpenCV use is three calls in one function** (`Adaptive.cpp:53-58`): `Mat::convertTo(CV_32F, 1/255)`, `cvtColor(COLOR_RGB2Lab)` on float, and `GaussianBlur(Size(0,0), σ)`. The rest uses `cv::Mat`/`cv::Vec3f`/`cv::Vec3d` as containers.
3. **Validated: the unmodified `Adaptive.cpp` builds and runs without openFrameworks.** A ~60-line shim header replaces `Mosaic.h`. It is linked against `opencv_core`+`opencv_imgproc` only and fed `fingers.png` through `cv::imread`. It prints **`0xe0d145733a499498`**, the adaptive golden (§3.4). So the seam can be cut with output unchanged.
4. **Lab is bit-exact by design in OpenCV and can be ported exactly.** For sRGB float input, OpenCV does not compute the formula. It uses trilinear interpolation of a 33³ 16-bit integer LUT built with `softfloat`, so its output lies exactly on a 1/2¹⁴ grid. The SIMD and scalar paths agree on all 2²⁴ colours. A plain-C++ port (generated table + integer interpolation) can be checked exhaustively against OpenCV in about a second. A textbook double formula differs on 99.999% of values (max 0.47 ΔE) and changes the golden (§2.1).
5. **GaussianBlur on float is *not* bit-exact across CPUs, even within x86.** `setUseOptimized(false)` changes 38% of the blurred floats on `fingers.png`. The golden still held. A plain separable blur with OpenCV's kernel and `std::fma` also kept the golden, although 48% of its floats differ. The same blur with mul+add *changed* it (1373 vs 1392 Tessels). **The golden is robust to most ULP noise in the blur, but not to all of it** (§2.2).
6. **Android/NDK clang contracts `a*b+c` into FMA by default.** `Adaptive.cpp` compiled for `aarch64-linux-android26` contains 32 fused instructions, and 0 with `-ffp-contract=off`. Forcing FMA contraction into `Adaptive.cpp` on x86 (`-mfma -ffp-contract=fast`, 58 `vfmadd`) still gave the golden. So the risk is concentrated in the blur, not in the rest of the core (§2.4).
7. **Recommended shape (not a decision):** put a plain-C++ core in **`src/mosaic/core/`**. Its interface: RGB buffer + width/height/stride + min patch area → Tessels (float vertices + RGB) + Contours (points, closed). It is compiled by the OF Makefile automatically and by a small `CMakeLists.txt` for Android and host checks. `MosaicMaker` keeps the OF adapter: pixel-format conversion, `ofParameter`, conversion to `Tessel`/`ofPolyline`, and logging. Two options for the three OpenCV calls:
   - **(A) Keep OpenCV `core`+`imgproc` in the core.** Golden unchanged on desktop by construction. Android output is likely but *not* guaranteed to match.
   - **(B) Port Lab exactly and blur deterministically** (fixed order, explicit `std::fma`, `-ffp-contract=off`). No OpenCV in the core. Very likely keeps the desktop golden (it did in the experiment), and makes desktop == Android by construction, apart from the libm `exp` used for the kernel (§2.3).
8. **The cross-platform check:** a host executable outside `src/` hashes the core's output (and intermediate stages) on raw-RGB fixtures. The same executable is cross-built with the NDK and run on the Pixel 8a with `adb shell`. A Pixel was attached over adb during this research but **was not used**.

---

## 1. Inventory

### 1.1 openFrameworks (and glm) used by adaptive

| Where | What | Role |
|---|---|---|
| `Adaptive.h:6` | `#include "Mosaic.h"` | Pulls in `ofMain.h`, `ofxCv.h`, `ofxDelaunay.h`, `MSAInterpolator.h` (`Mosaic.h:8-17`), the whole OF world, for two structs |
| `Adaptive.h:60` | `Mosaic make(const ofPixels& rgb, int minPatchArea)` | Entry point, `ofPixels` in, `Mosaic` out |
| `Adaptive.cpp:53-54` | `ofPixels` → `ofxCv::toCv(rgb)` | Wraps the buffer as `cv::Mat` with no copy (assumes tightly packed RGB) |
| `Adaptive.cpp:149-152` | `ofPixels::getWidth/getHeight/getData` | `joinEqualColours`, raw `data[i*3+c]` |
| `Adaptive.cpp:631-641` | `ofPixels::getWidth/getHeight/getData` | `meanColour`, raw `data + (y*w+x)*3` |
| `Adaptive.cpp:663` | `ofPixels::getColor(x,y)`, `ofClamp` | Fallback colour at the centroid |
| `Adaptive.cpp:631, 666` | `ofColor` (return type, `ofColor(r,g,b)`) | Tessel colour |
| `Adaptive.cpp:617, 622` | `ofLogWarning("Mosaic")`, `ofLogError("Mosaic")` | CDT failure fallbacks |
| `Adaptive.cpp:702-706` | `ofPolyline`, `addVertex(x,y)`, `setClosed` | Contours out |
| `Adaptive.cpp:710-713` | `Tessel{std::array<glm::vec3,3>, ofColor}` (`Mosaic.h:23-26`), `Mosaic{tessels, vector<ofPolyline>}` (`Mosaic.h:29-32`) | Result |
| `Adaptive.cpp:290, 375-389, 427-491, 531, 582` | `glm::ivec2`, `glm::i64vec2` | Lattice points of the chains, exact integer geometry |
| `Adaptive.cpp:396-410, 576, 602, 631-662` | `glm::dvec2`, `glm::dot` | Douglas–Peucker, triangles, point-in-triangle |

glm is header-only and not part of OF proper, but the core gets it through OF's `libs/glm`. OF compiles with `-DGLM_FORCE_CTOR_INIT` (visible in the build line), so glm vectors are zero-initialised in OF builds. Outside OF they are left uninitialised. Adaptive assigns every vector before reading it, and the OF-free experiment matched without the define. A core that keeps glm should still either define it or drop glm for a ~30-line `Vec2i/Vec2d` (recommended: one dependency fewer for the NDK build).

The parts of `MosaicMaker` adaptive goes through:

| Where | What |
|---|---|
| `Mosaic.cpp:50` | `ofParameter<int> minPatchArea.set("min patch area", 32, 8, 1024)` (XML tag `min_patch_area`) |
| `Mosaic.cpp:88-91` | `ofPixels::isAllocated/getWidth/getHeight`: empty Frame → empty Mosaic |
| `Mosaic.cpp:94-97` | `rgb = frame; rgb.setImageType(OF_IMAGE_COLOR)`: grayscale/RGBA → RGB (tested by `testAdaptivePixelFormats`, `tests/src/main.cpp:1180`) |
| `Mosaic.cpp:102-110` | Reset bookkeeping on size or mode change (irrelevant to adaptive, which is pure) |
| `Mosaic.cpp:113-115` | `return adaptive::make(rgb, minPatchArea)` |
| `Mosaic.cpp:57-73` | `parametersFor`: `isReferenceTo(minPatchArea)` |
| `Mosaic.cpp:222-232` | `toMesh`: `ofMesh`, `OF_PRIMITIVE_TRIANGLES`, `addVertex/addColor` (used by `ofApp.cpp:72,136`) |
| `Mosaic.h:122-126` | Members adaptive does not use but compiles: `ofPixels rgb`, `cv::Mat gray, edges`, `ofxCv::ContourFinder`, `msa::Interpolator2D`, `ofxDelaunay` |

The tests reach into adaptive directly through `adaptive::sizesFor`, `adaptive::Sizes`, `adaptive::blurSigma`, `adaptive::boundaryEpsilon` (`tests/src/main.cpp:1219-1232, 1309`). They use `cv::resize(ofxCv::toCv(frame), …, INTER_LINEAR)` to build ×4 inputs (`main.cpp:1247-1248`), plus `ofLoadImage` for fixtures (`main.cpp:212`).

### 1.2 OpenCV / ofxCv used by adaptive

| Where | Call | Notes |
|---|---|---|
| `Adaptive.cpp:54` | `ofxCv::toCv(const ofPixels&)` | Header-inline wrapper: `cv::Mat(h, w, CV_8UC3, data)` |
| `Adaptive.cpp:55` | `Mat::convertTo(unit, CV_32F, 1.0/255)` | `float(v) * float(1/255)` |
| `Adaptive.cpp:56` | `cv::cvtColor(unit, lab, COLOR_RGB2Lab)` | Float sRGB → `RGB2Lab_f` with `useInterpolation` (§2.1) |
| `Adaptive.cpp:57` | `cv::GaussianBlur(lab, lab, Size(0,0), σ)` | Float, 3 channels, `BORDER_DEFAULT` = `BORDER_REFLECT_101`, ksize `cvRound(σ·8+1)\|1` = 7 at 320×240 and 43 at σ = 5.12 (2048×1536) |
| `Adaptive.cpp:71-91, 105-106` | `cv::Mat::cols/rows/ptr<cv::Vec3f>(y)`, `Vec3f` `-`, `dot` | FH edge weights `sqrt(d·d)` |
| `Adaptive.cpp:205-216, 233-236` | `cv::Mat::ptr<Vec3f>`, `cv::Vec3d` `+=`, `/`, `-`, `dot` | Ward sums and costs |

Nothing else from OpenCV (no Canny, no ContourFinder) is on the adaptive path.

### 1.3 Vendored CDT (`src/mosaic/CDT/`)

- Upstream `artem-ogre/CDT` 2.0.2 (`b4413ee`), 11 headers, header-only, MPL-2.0 (+ BSD predicates, Apache-LLVM `portable_nth_element`). See `CDT/README.md`: **never edit**.
- Used at `Adaptive.cpp:578-626`: `CDT::V2d<double>`, `CDT::Edge`, `CDT::VertInd`, `CDT::Triangulation<double>(VertexInsertionOrder::Auto, IntersectingConstraintEdges::NotAllowed, 0.0)`, `insertVertices`, `insertEdges`, `eraseSuperTriangle`, `cdt.triangles`, `cdt.vertices`. It throws `std::exception` on failure.
- **No OF, no OpenCV, no Boost** (Boost includes in `CDTUtils.h:66-101` sit behind `CDT_USE_BOOST`).
- **Portable by design:** its own `SplitMix64RandGen` and `detail::random_shuffle` (`Triangulation.h:1241-1275`), `portable_nth_element.hpp`, and no `std::shuffle`/`<random>`, whose results differ between libstdc++ and libc++. Its predicates are adaptive-exact. They take the `std::fma` branch when `FP_FAST_FMA` or `__FMA__` is defined (`predicates.h:321-323, 435-437`), and are exact either way. Every vertex is on the integer lattice. So CDT's output is a combinatorial function of the chains, identical on every IEEE platform (expected; to be confirmed by the check in §4.3).
- `CDT.hpp` and `Triangulation.hpp:256-267` iterate `unordered_map`s, whose order differs between libstdc++ and libc++. These maps only hold overlapping or intersecting constraint pieces, which the crossing repair rules out. Low risk, still covered by the device check.

### 1.4 The rest of `Adaptive.cpp`

The rest is plain C++17/20: `<algorithm> <numeric> <queue> <unordered_map> <cstdint>`, `std::sqrt/lround/floor/ceil/abs`. Determinism notes:
- Bucket sort of edges: stable. Ward `priority_queue`: total order with tie-breaks on ids. `std::sort` on unique `uint64` pairs. `unordered_map<long,…>` in `triangulate` is used only for lookups. All are deterministic across standard libraries.
- `long` key `p.y*(w+1)+p.x` (`Adaptive.cpp:581-583`) is 64-bit on Linux and Android arm64. It would be 32-bit on Windows or armv7, still fine below about 46k×46k.
- Exact by construction: lattice geometry in `int64`, and doubles holding small integers (`simplify`, `conflict`, the `meanColour` edge functions). FMA contraction cannot change these, because the products are exact.
- Float-sensitive: FH weights (`Vec3f::dot`, `sqrt`), the FH threshold `w + k/size` (float), and Ward means and costs (double). All of them flow from the blurred Lab values.

---

## 2. Each OpenCV call: keep or replace?

### 2.1 `convertTo` + `cvtColor(RGB2Lab)`: exactly portable

What OpenCV 4.10.0 does (`modules/imgproc/src/color_lab.cpp`, tag 4.10.0):
- `RGB2Lab_f` sets `useInterpolation = (!_coeffs && !_whitept && srgb && enableRGB2LabInterpolation)`, which is true for `COLOR_RGB2Lab` (l. 1897).
- Each channel is clipped, then `iR = cvRound(R*LAB_BASE)` with `LAB_BASE = 2¹⁴`. A trilinear interpolation follows in **integers** over `RGB2LabLUT_s16`, a 33³ grid (`lab_lut_shift = 5`), with integer weights `trilinearLUT` (`TRILINEAR_BASE = 16`) (l. 966-976, 1318-1334, 1344-1380, 2030-2040). Output: `L = iL/2¹⁴·100`, `a = ia/2¹⁴·256 − 128`, `b = ib/2¹⁴·256 − 128`.
- The LUT is built once with `softfloat` (`applyGamma` and `cbrt` in software IEEE) (l. 1146-1220), so it is identical on every platform.
- The SIMD path (l. 1943-2025) produces the same integers and uses only power-of-two scalings (`100/2¹⁴`, `256/2¹⁴`, then −128), which are exact in float. So it equals the scalar tail bit for bit.
- `cvtBGRtoLab` has a `CALL_HAL` hook and an IPP branch (l. 4229-4231). The IPP branch is only for `!srgb` 8U. ARM's KleidiCV HAL (on by default on Android aarch64 since OpenCV 4.11 [kleidi-blog], [yocto]) does **not** list RGB→Lab among its accelerated conversions. It *does* list `Mat::convertTo` [kleidi-doc], which could in principle round `v·(1/255)` differently. That is worth checking if OpenCV is kept on Android.

Measured (`adaptive-core-experiments/labblur.cpp`, all 2²⁴ RGB colours):
- Optimised vs `setUseOptimized(false)`: **0** of 50,331,648 floats differ.
- Every value sampled lies on the 1/2¹⁴ grid (0 off-grid).
- The textbook double formula (sRGB → linear → XYZ D65 → Lab): 50,331,048 floats differ, max |Δ| 0.47. Substituted into adaptive: hash `0x45d8cb55b8f04df2`, 1531 Tessels and 118 Patches instead of 1392 and 110. The ×4 scale test also failed ("320x120 upscaled x4 … within 40%").

**Verdict.** Either keep OpenCV for this call (it is already platform-exact), or port it exactly:
- Ship `RGB2Labprev` (33³×3 `int16` = 107,811 values) as a generated header (~0.5 MB of source), or port OpenCV's `softfloat` `pow`/`cbrt` (much more code).
- Port the integer interpolation as written (~80 lines, Apache-2.0 with notice) and the `float(v)*float(1/255)` → `cvRound(·2¹⁴)` front end. Only 256 values per channel are possible, so this could even be a 256-entry index table.
- Add a desktop test that compares the port with `cv::cvtColor` on all 2²⁴ colours.

Effort: about 1 day. Risk to the golden: none once the exhaustive test passes. The textbook formula is not an option if the golden must hold.

### 2.2 `GaussianBlur` on `CV_32FC3`: deterministic port possible, exact match with OpenCV is not

What OpenCV 4.10.0 does (`smooth.dispatch.cpp`, `filter.simd.hpp`):
- The kernel comes from `getGaussianKernelBitExact`, in `softdouble` with a software `exp`, then cast to float (`smooth.dispatch.cpp:81-197`, used at l. 200-215). So it is platform-exact.
- Filtering: `CALL_HAL(gaussianBlur)` → OpenVX → (IPP only if `ENABLE_IPP_GAUSSIAN_BLUR`, "not bit-exact") → `sepFilter2D` (l. 611-786). KleidiCV's GaussianBlur is `CV_8U` only and "not bit-exact" [kleidi-doc], so it does not touch this float path.
- `filter` is a **runtime-dispatched** file (`ocv_add_dispatched_file(filter SSE2 SSE4_1 AVX2)`, `imgproc/CMakeLists.txt:5`):
  - Under AVX2, `RowVec_32f` has an explicit 8-wide loop with `_mm256_fmadd_ps` (`filter.simd.hpp:1632-1650`). The universal-intrinsic column filters use `v_muladd`: fused with FMA3 or NEON, mul+add on SSE.
  - On arm64, NEON is baseline and fused.
  - The scalar tails are whatever the compiler made of them: clang contracts by default (§2.4).
  - So the bits depend on the CPU, the ISA and the image width.

Measured on `fingers.png` Lab (230,400 floats):

| Variant | Floats differing from OpenCV/AVX2 | Max \|Δ\| | Adaptive golden |
|---|---|---|---|
| OpenCV `setUseOptimized(false)` (σ 0.8) | 87,171 (38%) | 2.3e-5 | **held** `0xe0d145733a499498` |
| Plain separable, OpenCV kernel, `std::fma`, taps in order | 111,426 (48%) | 1.5e-5 | **held** |
| Plain separable, OpenCV kernel, mul+add | 125,840 (55%) | 2.3e-5 | **changed** `0x830e0c0bcf1425fb`, 1373 Tessels |

(At σ 3.2: 39% / 72% / 72% differ.) So the golden on one 320×240 fixture is a coarse guard. A few FH or Ward decisions sit close enough to a tie that one ordering of roundings flips them.

**Verdict.**
- **Keep OpenCV:** desktop unchanged. Android unknown: NEON fused and 4-wide versus AVX2's 8-wide FMA path and different tails. It would need the device check (§4.3).
- **Port:** a separable 3-channel float blur with `BORDER_REFLECT_101` (~80 lines):
  - **Accumulation:** taps in a fixed order with explicit `std::fma`, compiled with `-ffp-contract=off`. IEEE `fma` is correctly rounded, so this gives identical bits on every IEEE platform. glibc's x86-64 `fma` dispatches to hardware FMA by ifunc; **measure** the cost (about 3.2 M fma per 320×240 Frame).
  - **Kernel:** port `getGaussianKernelBitExact` in `double`. The only non-exact step is `std::exp` in glibc versus bionic, which is not guaranteed correctly rounded. After the cast to float a mismatch is very unlikely. Add a desktop test against `cv::getGaussianKernel` over every σ that `sizesFor` produces for sizes up to 4096, and use a correctly rounded `exp` (e.g. CORE-MATH, MIT) if total certainty is wanted.
  - **Effort:** about 0.5–1 day. Risk to the golden: low but real. It held with `fma` in the experiment, and the golden test is the guard.
- **A fixed-point alternative** (an integer kernel over the int16 Lab grid, like OpenCV's own 8U `GaussianBlurFixedPoint`) is exact by construction everywhere, with no `exp` subtlety at 2⁻¹⁶ quantisation. It changes the output, so it is a deliberate golden re-record, which #48 currently does not allow.

### 2.3 Summary

| Operation | Keep OpenCV | Plain C++ | Effort | Bit-identical to current golden | Desktop == Android |
|---|---|---|---|---|---|
| `convertTo` 1/255 | trivial | `float(v)*float(1/255)`, or fold into the Lab index | minutes | yes (exhaustive test) | yes |
| `cvtColor RGB2Lab` (float sRGB) | platform-exact already | generated LUT + integer trilinear | ~1 day | **yes** (exhaustive test vs OpenCV) | yes |
| `GaussianBlur` float | CPU-dispatch-dependent | fixed-order `std::fma`, bit-exact kernel port | ~0.5–1 day | **very likely** (held in experiment); golden test decides | yes, by construction (modulo libm `exp`) |
| Containers `cv::Mat/Vec3f/Vec3d` | — | `std::vector<float>` + 3-float struct | ~2 h | yes (no arithmetic change if `dot` order kept and `-ffp-contract=off`) | yes |

### 2.4 FMA contraction in the core itself

- **NDK clang 21** (`--target=aarch64-linux-android26 -O3`): `Adaptive.cpp` → **32** `fmadd/fmsub/fmla…` instructions; with `-ffp-contract=off` → **0**. Clang's default is `-ffp-contract=on`, which fuses within an expression. Apple clang on arm64 behaves the same way.
- **GCC x86-64** without `-mfma`: cannot contract. With `-mfma -ffp-contract=fast`, applied to `Adaptive.cpp` in the tests build (58 `vfmadd`), **all 210 tests still pass and the golden holds**. The contractible spots (`Vec3f::dot`, the Ward cost, the exact lattice arithmetic) did not flip a decision on this fixture.
- Recommendation: compile the core with `-ffp-contract=off` on every platform. GCC ignores `#pragma STDC FP_CONTRACT` in C++, so use the flag rather than the pragma. Then the only float that differs between platforms is whatever OpenCV does, if it is kept.

---

## 3. A minimal plain-C++ core

### 3.1 Where it lives

**`src/mosaic/core/`**, with CDT either left at `src/mosaic/CDT/` or moved to `src/mosaic/core/CDT/` (an unmodified move, licences kept).
- The OF Makefile compiles every source under `src/` recursively and adds every subfolder as `-I` (the build line shows `-I../src/mosaic -I../src/mosaic/CDT`). So the app and the tests pick the core up with no config change.
- The tests' single external path (`PROJECT_EXTERNAL_SOURCE_PATHS = ../src` with `PROJECT_EXCLUSIONS = ../src`, `tests/config.make`) already covers it. OF supports only one external source path, so a core *outside* `src/` would need that scheme reworked.
- Anything that must **not** be built by OF stays outside `src/`: the JNI glue (`android/…`), the core-only golden tool (`tests/core/` or `tools/core-golden/`), any `main()`. A `CMakeLists.txt` inside `src/mosaic/core/` is ignored by the OF Makefile, which only collects source extensions.
- Rule for the folder: **no `of*`, `ofx*`, `glm` or `cv` include in `src/mosaic/core/`** under option (B), or only `<opencv2/core.hpp>`/`<opencv2/imgproc.hpp>` under (A). The CMake host build in §5 enforces this by compiling the folder with no OF include path.

### 3.2 Interface sketch

```cpp
// src/mosaic/core/AdaptiveCore.h — no openFrameworks, no glm.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mosaic::core {

// A Frame: 8-bit RGB, rows `stride` bytes apart (stride >= 3 * width).
struct RgbImage {
    const std::uint8_t* data;
    int width, height;
    std::size_t stride;
};

struct Point { float x, y; };                      // Frame pixel coordinates (lattice values, exact in float)

struct Tessel {
    std::array<Point, 3> triangle;
    std::array<std::uint8_t, 3> rgb;
};

struct Contour {                                   // a Patch boundary off the Frame border
    std::vector<Point> points;                     // a closed Contour does not repeat its first point
    bool closed;
};

struct Mosaic {
    std::vector<Tessel> tessels;                   // tile (0,0)-(width,height); order is part of the golden
    std::vector<Contour> contours;
};

// Sizes and constants as today (Adaptive.h:13-32).
constexpr int referenceWidth = 320, referenceHeight = 240;
constexpr float blurSigma = 0.8f;
constexpr double boundaryEpsilon = 3.0;
constexpr float segmentationK = 5.0f;
struct Sizes { int minPatchArea; float blurSigma; double boundaryEpsilon; };
Sizes sizesFor(int width, int height, int minPatchArea);

// The adaptive Mosaic; a pure function. An empty image gives an empty Mosaic.
Mosaic makeAdaptive(const RgbImage& frame, int minPatchArea);

// Optional: where the two CDT-failure messages go (default: none).
enum class LogLevel { Warning, Error };
using LogSink = void (*)(LogLevel, const char* message);
void setLogSink(LogSink);

// FNV-1a over the Tessels exactly as tests/src/main.cpp:185-208 does, so every build reports the same number.
std::uint64_t hash(const Mosaic&);

} // namespace mosaic::core
```

Notes:
- **Stride.** Today's code assumes tight rows (`toCv` without a step, `data[i*3]`, `data + (y*w+x)*3`). The cheapest correct implementation copies a strided image into a tight buffer at entry: 230 KB at 320×240, negligible next to the pipeline. Alternatively, thread `stride` through `joinEqualColours`, `meanColour` and the Lab conversion. RGBA input (CameraX's `RGBA_8888`, #50) can be stripped in the JNI copy, or the core can accept `channels ∈ {3,4}`. That is a spec choice.
- **Output types.** `Point{float}` keeps the hash identical: today's hash reads `glm::vec3::x/y` as float bits, and the values are lattice integers converted from `double`, so they are exact. `std::array<uint8_t,3>` maps 1:1 to `ofColor`. A JNI layer can flatten it to `FloatArray`+`IntArray` (#50 §4).
- **Contours.** Plain point lists plus a `closed` flag, mirroring `ofPolyline::setClosed`. The adapter rebuilds `ofPolyline` with `addVertex(x, y)`, which gives z = 0 as today.
- **Debug and trace hook (for §4.3).** Optionally `Mosaic makeAdaptive(const RgbImage&, int, Trace*)`, where `Trace` collects the hashes of the blurred Lab bytes, the FH labels, the labels after each merge stage, the chains, and the triangles. That makes a cross-platform mismatch pinpointable.

### 3.3 What stays in the OF adapter (`MosaicMaker`)

- `ofParameter<int> minPatchArea` and the panel/XML wiring, `parametersFor`, `vertexMode`: unchanged (`Mosaic.cpp:50-73`).
- Frame checks and pixel formats: `isAllocated`, and `setImageType(OF_IMAGE_COLOR)` for grayscale or RGBA (`Mosaic.cpp:90-97`). `ofPixels` rows are tight, so `RgbImage{rgb.getData(), w, h, size_t(w) * 3}`.
- Conversion back: `core::Tessel` → `::Tessel{glm::vec3(x, y, 0)…, ofColor(r, g, b)}`, `core::Contour` → `ofPolyline`. `::Tessel`, `::Mosaic` and `toMesh` stay as they are, so `ofApp`, the CLI and every test keep compiling.
- `setLogSink` → `ofLogWarning/ofLogError("Mosaic")`, installed once in the constructor.
- `adaptive::` names the tests use (`sizesFor`, `Sizes`, `blurSigma`, `boundaryEpsilon`): a `namespace adaptive = mosaic::core;` alias in `Adaptive.h` keeps `tests/src/main.cpp:1219-1232, 1309` compiling. Alternatively, update the tests.
- Spline and stable keep ofxCv, ofxDelaunay and ofxMSAInterpolator in `Mosaic.cpp`, desktop only.

### 3.4 Validation: the core already runs without OF

`adaptive-core-experiments/shim-Mosaic.h` is the experiment's stand-in for `Mosaic.h`. It defines `ofPixels` (a view over a packed RGB buffer), `ofColor`, `ofPolyline`, `ofClamp`, `ofLogWarning/Error` (to `std::cerr`), `ofxCv::toCv`, `Tessel` and `Mosaic`, with standalone glm. Copied next to **unmodified** `Adaptive.{h,cpp}`, plus `golden-without-of.cpp` (loads `fingers.png` with `cv::imread`, BGR→RGB, then hashes as the tests do):

```
g++ -O3 -std=c++20 -I<exp> -Isrc/mosaic/CDT -I$OF/libs/glm/include $(pkg-config --cflags opencv4) \
    Adaptive.cpp golden-without-of.cpp -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
→ 1392 Tessels, 276 Contours, hash 0xe0d145733a499498
```

`ldd` shows only `libopencv_core/imgproc/imgcodecs.so.410`, no openFrameworks. So the refactor in §3.3 can keep the golden: the algorithm does not depend on anything OF does beyond carrying the buffer.

---

## 4. Keeping it guarded

### 4.1 The existing desktop tests keep working unchanged

- All 14 `testAdaptive*` tests (`tests/src/main.cpp:1025-1320`) observe adaptive through `MosaicMaker::make()`. They keep working through the adapter. That includes the golden `0xe0d145733a499498` (`main.cpp:1213`) and the spline golden `0xe2ece51f4a5d770d` (`main.cpp:843`), which does not touch adaptive.
- The tests build already compiles everything under `src/`, so it compiles the new folder too.
- Desktop bit-identity, by option:
  - Option (A): holds by construction (§3.4).
  - Option (B): the exhaustive Lab test makes Lab exact. The blur is guarded by the golden itself, which is coarse. Add a **blurred-Lab hash** test on `fingers.png` against `cv::GaussianBlur`: it may legitimately differ by ULPs, so record the port's own hash as a new golden for the stage.

### 4.2 New desktop tests worth adding with the refactor

- `core::hash(core::makeAdaptive(…))` equals the OF-path hash on the same Frame (adapter round-trip).
- Option (B): Lab port == `cv::cvtColor` on all 2²⁴ colours (about 1 s). Kernel port == `cv::getGaussianKernel` for all σ from `sizesFor` up to 4096 px. These two keep OpenCV as a **test-only** oracle on desktop.
- A golden for a *strided* and an *RGBA* input, if the interface accepts them.
- Fixtures for cross-platform use should be files, not `cv::resize` outputs: `main.cpp:1247` builds the ×4 Frames with OpenCV `resize`, which KleidiCV accelerates on Android [kleidi-doc]. Save `fingers.png ×4` as a PNG or raw fixture instead.

### 4.3 A cross-platform determinism check

1. A small host executable **outside `src/`** (e.g. `tests/core/golden.cpp`). It reads raw RGB fixtures (`.rgb` + size, converted once from the PNGs, so the core needs no image decoder), runs `makeAdaptive` at a few `min patch area` values (8, 32, 1024) and sizes (320×240, 240×320, 1280×960, about 2048×1536), and prints `core::hash` and the trace-stage hashes. A desktop test asserts the printed values. Today's `fingers.png` @32 must print `0xe0d145733a499498`.
2. The same source cross-built with the NDK for `arm64-v8a` (static, `-ffp-contract=off`). `adb push` it with the fixtures to `/data/local/tmp`, run it with `adb shell`, and `diff` the output against the desktop's. No APK is needed. Cheaper still: `qemu-aarch64` user-mode on this machine (not installed) to run the arm64 binary without the phone, then confirm once on the Pixel 8a.
3. Under (A) this check tells whether OpenCV's NEON blur happens to match. Under (B) it should match by construction, and a mismatch points at the stage (trace hashes) or at libm `exp`.
4. A device was attached (`adb devices`) during this research. Nothing was run on it.

---

## 5. Build integration notes

**OF Makefile (app and tests)**
- Sources: recursive under `src/`, and include dirs are every subfolder, so `#include "CDT.h"` works from `src/mosaic/core/` without a path. CMake has to list the include dirs explicitly.
- Tests: `PROJECT_EXTERNAL_SOURCE_PATHS = ../src` + `PROJECT_EXCLUSIONS = ../src` (`tests/config.make`) compiles every module folder but not `src/main.cpp`/`ofApp`. A core under `src/mosaic/core/` needs no change. Do not put a `main()` or JNI code under `src/`.
- Flags: `PROJECT_CFLAGS` in `config.make` (app and tests) can add `-ffp-contract=off`. It applies to all project sources, which is harmless. OF's own `-Werror=return-type` comes after the project flags (see CLAUDE.md), which does not matter here. `PROJECT_CFLAGS` overrides from the command line work (used in §2.4).
- OpenCV stays on desktop via `ofxOpenCv`/`ofxCv` (linux64: `pkg-config opencv4`) for spline and stable. Under (A) the core also uses it. Under (B) only the tests use it, as an oracle.

**CMake for the core (Android and host checks)**, e.g. `src/mosaic/core/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(mosaic_core LANGUAGES CXX)

add_library(mosaic_core STATIC
    AdaptiveCore.cpp   # today's Adaptive.cpp minus OF
    Lab.cpp Blur.cpp)  # option (B) only
target_compile_features(mosaic_core PUBLIC cxx_std_20)
target_include_directories(mosaic_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../CDT)        # vendored, header-only
target_compile_options(mosaic_core PRIVATE -ffp-contract=off)

option(MOSAIC_CORE_WITH_OPENCV "Option (A): use OpenCV core+imgproc for Lab and blur" OFF)
if(MOSAIC_CORE_WITH_OPENCV)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    target_link_libraries(mosaic_core PUBLIC opencv_core opencv_imgproc)
endif()
```

- Android: `android/app/src/main/cpp/CMakeLists.txt` does `add_subdirectory(${REPO}/src/mosaic/core mosaic_core)` and links it into the JNI `.so`. The default STL `c++_static` is fine under (B). Under (A) the prebuilt OpenCV AAR is monolithic and forces `c++_shared` (#50 §7). Build OpenCV statically with `-DBUILD_LIST=core,imgproc` instead, and mind KleidiCV (on by default for Android aarch64 since 4.11 [kleidi-blog]; `-DWITH_KLEIDICV=OFF` to rule it out).
- Host: `cmake -S src/mosaic/core -B build/core && cmake --build build/core` with no OF include path. It fails to compile if anything OF sneaks into the core, which makes it a cheap guard. The golden tool (§4.3) links `mosaic_core` from `tests/core/CMakeLists.txt`.
- If the core keeps glm: on Android, vendor it or `FetchContent` it, and define `GLM_FORCE_CTOR_INIT` and `GLM_ENABLE_EXPERIMENTAL` as OF does. Simpler to drop glm (§1.1).

---

## 6. Risks, in order

1. **Blur bits differ across CPUs** under option (A), even x86 SSE versus AVX2. Shown: 38% of floats differ with no golden change, while a different rounding order changed the golden. Android == desktop is plausible but unproven under (A). Under (B), determinism comes from construction plus `-ffp-contract=off`.
2. **Compiler contraction** (NDK clang, Apple clang): 32 fused ops in today's `Adaptive.cpp` on arm64. It is harmless on this fixture (tested on x86 with forced FMA), but should be turned off.
3. **libm `exp`** in a ported kernel (glibc versus bionic): very unlikely to matter after the cast to float. Remove it with a correctly rounded `exp`, or check it by a σ sweep on desktop and the device run.
4. **The golden is one Frame.** It flips on rare near-ties and stays put under most noise. Add the stage hashes and more fixtures (`fingers-003.png`, `fingers-060.png`, a large raw fixture) before relying on it for a port.
5. **OpenCV on Android** (option A): HAL interception (KleidiCV `convertTo`), a different OpenCV version from desktop 4.10.0, a monolithic AAR. Each is a reason the device check could fail for reasons outside this code.

---

## Experiments (throwaway, nothing in `src/` or `tests/` changed on this branch)

- `adaptive-core-experiments/labblur.cpp`: OpenCV Lab versus textbook and optimised versus not; blur variants. `g++ -O2 labblur.cpp $(pkg-config --cflags --libs opencv4)`, run with `tests/fixtures/fingers.png`.
- `adaptive-core-experiments/shim-Mosaic.h` + `golden-without-of.cpp`: the OF-free build of unmodified `Adaptive.cpp` (§3.4).
- In-tree, reverted before committing: `cv::setUseOptimized(false)` at the start of the tests' `run()`; env-selected plain Lab and plain blur inside `blurredLab()`; `PROJECT_CFLAGS="-mfma -ffp-contract=fast"` on `Adaptive.cpp`. The tests were built with `make -C tests OF_ROOT=/home/dario/Applications/of Release`. Unmodified: 210/210 pass.

## Sources

- Code: `src/mosaic/Adaptive.h`, `src/mosaic/Adaptive.cpp`, `src/mosaic/Mosaic.h`, `src/mosaic/Mosaic.cpp`, `src/mosaic/CDT/` (`README.md`, `Triangulation.h`, `Triangulation.hpp`, `predicates.h`), `tests/src/main.cpp`, `tests/config.make`, at `c2db62a`.
- [ocv-lab] OpenCV 4.10.0 `modules/imgproc/src/color_lab.cpp`: https://github.com/opencv/opencv/blob/4.10.0/modules/imgproc/src/color_lab.cpp
- [ocv-smooth] OpenCV 4.10.0 `modules/imgproc/src/smooth.dispatch.cpp`: https://github.com/opencv/opencv/blob/4.10.0/modules/imgproc/src/smooth.dispatch.cpp
- [ocv-filter] OpenCV 4.10.0 `modules/imgproc/src/filter.simd.hpp`: https://github.com/opencv/opencv/blob/4.10.0/modules/imgproc/src/filter.simd.hpp
- [ocv-cmake] OpenCV 4.10.0 `modules/imgproc/CMakeLists.txt` (dispatched files): https://github.com/opencv/opencv/blob/4.10.0/modules/imgproc/CMakeLists.txt
- [kleidi-doc] KleidiCV, OpenCV HAL function list and constraints: https://gitlab.arm.com/kleidi/kleidicv/-/blob/main/doc/opencv.md
- [kleidi-blog] Arm, KleidiCV on by default for Android in OpenCV 4.11: https://developer.arm.com/community/arm-community-blogs/b/ai-blog/posts/kleidicv030-updates and https://newsroom.arm.com/blog/arm-kleidicv-opencv-integration
- [yocto] OpenCV 4.12 → 4.13 `WITH_KLEIDICV` default (Android aarch64, then all aarch64 Unix): https://patchwork.yoctoproject.org/project/oe/patch/20260112080619.7065-1-liuyd.fnst@fujitsu.com
- Sibling study #50: `docs/research/android-native-app.md` on branch `research/android-native-app`.
