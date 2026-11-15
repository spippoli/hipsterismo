# Optimising adaptive for the Tensor G3

Research for [#55](https://github.com/spippoli/hipsterismo/issues/55), part of the map [#48](https://github.com/spippoli/hipsterismo/issues/48). It decides nothing. It feeds the Pixel 8a benchmark ticket [#53](https://github.com/spippoli/hipsterismo/issues/53) with variants to measure.

The question: how could adaptive's pipeline (`src/mosaic/Adaptive.cpp`) run faster on the Pixel 8a's Tensor G3, and what would each option cost in speed, determinism (the golden hashes), complexity and sharing with the desktop?

**Short answer:**
- The pipeline is dominated by **sequential, memory-bound graph work**: the FH union-find, the Ward merges and building neighbour lists. Wide-SIMD or GPU-shaped arithmetic is only ~15–20% of a 320×240 Frame.
- The best options are, in order:
  1. Exact CPU rewrites of three hot spots. They keep the golden hash, and the desktop shares them: −22% to −28% at 320×240, −18% at 2048.
  2. Frame-level parallelism for the live preview: ~1.75× throughput on two cores, with identical output.
  3. Within-Frame threads for the data-parallel stages, for photos only.
- SVE2, GPU compute, OpenCL and the TPU are not worth it here.

## 1. Where the time goes (desktop profile)

**Set-up:**
- **Machine:** i7-11370H, Linux, g++ -O3, OpenCV 4.10. The harness is OF-free: the unmodified `Adaptive.cpp` behind the shim from `research/adaptive-core`, with timers added by `instrument.py`. Sources are in [`tensor-g3-optimisation-experiments/`](tensor-g3-optimisation-experiments/).
- **Pinning:** runs are pinned to one core (`taskset -c 2`). OpenCV's internal threads are therefore serialised, which is the honest single-core picture.
- **Statistics:** each figure is the median over 200 runs (320×240) or 12 runs (2048).
- **No `perf`:** it is unavailable (`perf_event_paranoid` = 4).
- **Load:** the machine was shared with other builds during some runs. Medians on a pinned core absorb most of that; differences under ~5% are noise.
- **Golden:** the harness reproduces the adaptive golden `0xe0d145733a499498` on `fingers.png`.

| Stage | 320×240 fingers.png (ms) | % | 2048 px, 4 images (ms) | % | Shape |
|---|---:|---:|---:|---:|---|
| RGB→Lab (OpenCV float) | 0.57 | 3.4 | 36.4 | 5.8 | data-parallel per pixel |
| Gaussian blur (σ 0.8 / 5.1) | 0.19 | 1.1 | 58.4 | 9.3 | data-parallel, separable |
| FH edge weights (4/pixel) | 1.23 | 7.4 | 56.9 | 9.1 | data-parallel per pixel |
| FH bucket sort | 1.86 | 11.2 | 88.0 | 14.1 | histogram + scatter, memory-bound |
| FH union-find + relabel | 2.66 | 16.0 | 126.4 | 20.2 | **sequential** (Kruskal order) |
| Exact-equal join | 1.46 | 8.7 | 46.4 | 7.4 | sums parallel, pairs + union-find sequential |
| Ward merge 1 | 3.49 | 20.9 | 81.2 | 13.0 | **sequential** greedy (heap) |
| Piece split (4-conn. CCL) | 0.53 | 3.2 | 23.7 | 3.8 | union-find, parallel CCL possible |
| Ward merge 2 (only small) | 0.77 | 4.6 | 19.4 | 3.1 | **sequential** |
| Crack-chain tracing | 0.97 | 5.8 | 30.9 | 4.9 | junction map parallel, walks sequential |
| DP + crossing repair | 0.32 | 1.9 | 4.6 | 0.7 | per chain parallel, tiny |
| CDT (vendored) | 1.03 | 6.1 | 3.8 | 0.6 | sequential, scales with vertices |
| Tessel colour means | 1.60 | 9.6 | 49.4 | 7.9 | data-parallel per Tessel |
| **Total** | **16.7** | | **626** | | |

**Counts per Frame:**
- **fingers.png at 320×240:** 305 k FH edges; 1929 FH Patches, then 1916 after the join; 231 pieces; 322 chains; 1392 Tessels; 3 repair rounds.
- **At 2048 (mean of the 4 images):** ~12.6 M edges, ~13.9 k FH Patches, 1188 pieces, 268 chains, 5.6 k Tessels.

**Other measurements:**
- **fingers.mov:** 8 Frames (every 30th, via ffmpeg) give the same picture, at 18.1 ms per Frame.
- **Stage shares at 2048:** the shares barely move with size. Pixel-count stages dominate, while CDT and DP shrink to under 1%, because the number of Tessels grows far more slowly than the number of pixels.
- **Unpinned:** OpenCV may thread Lab and the blur by itself, but my one unpinned run at 2048 was taken under load and is inconclusive (Lab ~31 ms, blur ~87 ms). Everything else is single-threaded code. #53 should measure this (variant 5).
- **Peak memory, base code (`/usr/bin/time`):** **460 MB at 2048×1536 and 1.6 GB at the full 4032×3024**. Most of it is the two 12-byte `PixelEdge` arrays of ~4N edges (`edges` and `sorted`, ~150 MB each at 2048). On a phone this matters as much as the time.

**Finer findings:**
- **`neighbourPairs()`:** it `std::sort`s one 64-bit key per boundary pixel pair and is called by the join and by both Ward passes. It costs **3.1 ms of the 16.7 ms** (18%) at 320×240, measured by a separate timer. It is most of the "join" and a large part of each Ward pass. The Ward heap itself (already a lazy-deletion `priority_queue` with versions) is not the problem.
- **The FH sort is already a linear counting sort** (4096 buckets of 1/16 ΔE, stable). A "radix sort for the FH edges" is in effect what it is. Its cost is memory traffic: it writes 12 B × 4N, reads it back and scatters it again.
- **FH early exit is useless.** An exact early exit, stopping once an edge is heavier than every threshold ever assigned, would skip only 0–2% of the edges: 98–100% lie below the bound of ~6–7.5 ΔE.

## 2. The Tensor G3 and Android: what is available

**CPU.** The Pixel 8a has 1× Cortex-X3, 4× Cortex-A715 and 4× Cortex-A510, all Armv9.0-A.
- **Instruction set:** LLVM's processor definitions give all three Neon and SVE/SVE2 ([AArch64Processors.td](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64Processors.td)).
- **Core numbering:** `/proc/cpuinfo` on a Pixel 8 numbers the A510s 0–3, the A715s 4–7 and the X3 as 8 ([pytorch/cpuinfo#437](https://github.com/pytorch/cpuinfo/issues/437)).
- **SVE vector length:** the architecture allows any multiple of 128 from 128 to 2048 bits ([Arm, SVE](https://developer.arm.com/Architectures/Scalable%20Vector%20Extensions)). All announced mobile Armv9 cores of this generation implement 128 bits. I could not open Arm's TRMs (they render client-side), so **#53 should confirm it on the device** with `prctl(PR_SVE_GET_VL)`. At 128 bits SVE2 is no wider than Neon.
- **Compiler tuning:** LLVM's tuning for the X3 and A510 sets `UseFixedOverScalableIfEqualCost`, so the compiler itself prefers fixed-width Neon over SVE on these cores.
- **SVE detection:** userspace detects SVE with `HWCAP_SVE` in `AT_HWCAP` and SVE2 with `HWCAP2_SVE2` in `AT_HWCAP2`, and gets the vector length with `prctl(PR_SVE_GET_VL)` ([kernel SVE ABI](https://docs.kernel.org/arch/arm64/sve.html)). Android exposes `getauxval` from API 18 ([NDK CPU features](https://developer.android.com/ndk/guides/cpu-features)).
- **Unknown:** whether the Pixel kernel reports `sve2`. I found no primary source either way; #53 can read the hwcaps without running anything heavier.
- **Neon:** it is baseline on arm64-v8a ([NDK ABIs](https://developer.android.com/ndk/guides/abis)). Google's NDK guidance is explicit: *"You should not write explicit Neon intrinsics … Clang's portable vector types will automatically use Neon instructions"* ([NDK Neon](https://developer.android.com/ndk/guides/cpu-arm-neon)). Auto-vectorisation and portable vector types are the intended path.
- **Determinism:** vectorising *across pixels* keeps each pixel's operation sequence, so the results stay bit-identical to scalar code, provided contraction is controlled. The NDK's clang contracts to FMA by default (finding of #51), so route (B) needs `-ffp-contract=off` or explicit `std::fma`.

**Big.LITTLE and threads.**
- **Use ADPF hints, not affinity.** Google's guidance is to avoid pinning threads and to use the Performance Hint API (ADPF) instead. Its NDK C API (`APerformanceHint_createSession`, `reportActualWorkDuration`) needs API 33, with the Java `PerformanceHintManager` from API 31 ([NDK Performance Hint](https://developer.android.com/ndk/reference/group/a-performance-hint), [codelab](https://developer.android.com/codelabs/adaptability-codelab)). The Pixel 8a runs Android 17, so the API is available, but minSdk 29 means a runtime check.
- **Sequential stages:** they bound the latency and should run on the X3. A hint session with a 33 ms target is the supported way to ask for it.
- **Data-parallel splits:** a split across the X3 and A715s should use dynamic chunking, so that an A510 (or a throttled core) doesn't become the straggler of a static split.

**GPU (Mali-G715).**
- **Compute APIs:** Android supports GPU compute through Vulkan and OpenGL ES 3.1 compute shaders. Google's RenderScript migration guide recommends them, and notes that *"C/C++ may be faster than RenderScript, GLES, or Vulkan compute for some use cases"* ([migrate from RenderScript](https://developer.android.com/guide/topics/renderscript/migrate)).
- **Vulkan version:** Vulkan 1.1 is the floor for Android 14+ devices, and 1.3 is strongly recommended ([Android 14 CDD](https://source.android.com/docs/compatibility/14/android-14-cdd)). #53 can read the exact driver version from `vulkaninfo` or `dumpsys`.
- **OpenCL** is not part of Android. Apps can only reach vendor libraries listed in `/vendor/etc/public.libraries.txt` ([AOSP namespaces](https://source.android.com/docs/core/permissions/namespaces_libraries)). On a Pixel 8, a public `libOpenCL.so` opens but doesn't export the CL symbols ([a recent third-party report](https://github.com/Envious-Labs-LLC/EnviousWispr-Android/issues/464)), so treat OpenCL as unavailable.

**TPU.**
- **NNAPI** is deprecated since Android 15 ([NNAPI migration](https://developer.android.com/ndk/guides/neuralnetworks/migration-guide)).
- **LiteRT's Google Tensor NPU path** is a gated beta that lists only Tensor G5 and G6 ([Tensor SDK](https://developers.google.com/edge/litert/next/tensor-sdk)), not the G3.
- **Fit:** neither path fits the workload anyway. Union-find, a greedy heap, crack tracing and CDT are not tensor programs. **The TPU is out.**

## 3. Per stage: options, gains, determinism

"Exact" means the output is bit-identical by construction, so the golden and any per-stage hashes hold. All the desktop measurements below kept the golden at 320×240 **and** the hashes of all four 2048 images.

| Stage | Option | Measured / expected gain | Determinism | Complexity, sharing |
|---|---|---|---|---|
| **Lab** | Route (B) exact port (LUT + integer trilinear, as in #51), auto-vectorised over pixels; row-parallel for photos | 320: tiny (0.57 ms). 2048: ~4× on 4–5 cores (36 → ~10 ms) | exact | medium (the port is needed for route B anyway); shared |
| | Fixed-point / 8-bit Lab | small: Lab is 3–6% | **changes golden** | not worth it |
| **Blur** | Deterministic separable FIR with `std::fma` in a fixed tap order, vectorised across x, row-parallel | 320: tiny. 2048: ~3–5× with threads | exact across platforms (route B) | low–medium; shared |
| | Recursive (Young–van Vliet/Deriche) or 3× box blur, **only for large σ** | 2048: blur 58 ms → ~10–15 ms expected (O(1) per pixel instead of 43 taps) | 320 golden kept if FIR stays for σ < ~2; **photo output changes** slightly | low–medium; shared; a look decision |
| **FH edges + sort** | **Exact rewrite (measured):** weights once into a `float[4N]`, histogram, scatter straight into the sorted array (no intermediate 12-byte edge vector) | 320: 3.08 → 1.55 ms (−1.5 ms). 2048: 145 → 115 ms; peak RSS −100 MB at 2048, −380 MB at 12 MP | exact (same weights, same stable order) | low (~40 lines); shared |
| | Compact 8-byte edges (`uint32 pixel*4+dir`, `float`) | memory −33% more, a little time | exact | low |
| | Per-thread histograms + prefix sums, parallel scatter | 2048: sort ~2–3× | exact (block order fixed) | medium |
| **FH union-find** | Keep sequential on the X3; layout tweaks (parent/size/threshold together) | maybe 10–20%, untested | exact | low |
| | Parallel / approximate FH | — | **changes output** | not recommended |
| **Equal join** | **Exact `neighbourPairs()` (measured):** counting sort by the smaller label, stamp array to de-duplicate, sort each short bucket | the join 1.46 → 0.81 ms | exact (same sorted unique list) | low (~40 lines); shared |
| **Ward 1/2** | Same `neighbourPairs()` fix (measured) | Ward 1 3.49 → 2.88 ms, Ward 2 0.77 → 0.65 ms; 2048: 81 → 58 ms | exact | included above |
| | CSR adjacency instead of `vector<vector<int>>`; derive Ward 1's pairs from the join's pairs through its union-find (no second pixel scan) | estimated another 1–1.5 ms at 320 | exact | medium; shared |
| | Parallel sums, or integer/fixed-point Lab sums | small | parallel double sums **change rounding → golden risk**; keep scan order | not worth it |
| **Piece split** | Run-based two-pass CCL | 2048: 24 ms → maybe 10 ms | exact (scan-order relabel) | medium |
| **Chain tracing** | Precompute crack bitmaps (H/V label differences, vectorisable) instead of the `hasH/hasV` lambdas; keep the walk order | 2048: 31 ms → maybe 15 ms | exact if chain order is kept (CDT input order matters with cocircular lattice points) | medium |
| **DP + repair** | Per-chain threads | < 2% of the Frame | exact | not worth it |
| **CDT** | Incremental / reused across live Frames | CDT is 1 ms (6%) at 320 and < 1% at 2048; the vertices change every Frame | reuse changes the "pure function of the Frame" contract | **not worth it** (and `CDT/` is never edited) |
| **Tessel colour** | **Exact integer spans (measured):** vertices are lattice points and centres half-integers, so on the doubled lattice each edge function is an integer; each row's inside span comes from integer divisions with the same top-left rule, then a plain sum | 320: 1.60 → 0.58 ms. 2048: 49 → 7.7 ms (6.4×) | exact | low (~50 lines); shared |
| | Threads over Tessels | after the span rewrite there is too little left (thread start-up ate the gain at 320) | exact (integer sums) | not worth it |
| **Whole Frame** | **Frame-level parallelism** for the live preview: two or three workers each run `make()` on their own Frame | measured on 2 desktop cores: 12.8 ms alone → 14.6 ms each when concurrent = **~1.75× throughput**; latency unchanged | exact (each Frame is a pure function; `Adaptive.cpp` has no global state) | low, all in the Android/JNI layer; desktop optional |
| | Temporal reuse (warm-starting from the previous Frame) | unknown | breaks the per-Frame purity and the golden contract | not recommended |

**Measured combined gain of the three exact rewrites** (variant `all` = `FAST_PAIRS` + `FAST_EDGES` + `FAST_COLOUR`, pinned to one core):

| Input | Base | `all` | Change |
|---|---:|---:|---:|
| fingers.png 320×240 | 16.7 ms | 12.9 ms | −22% |
| 8 fingers.mov Frames | 18.1 ms | 13.0 ms | −28% |
| 2048 px (fingers.png upscaled + 3 photos) | 626 ms | 512 ms | −18% |
| one 12 MP photo, peak RSS | 1.61 GB | 1.23 GB | −24% |

After these rewrites the 320×240 Frame is ~12.9 ms:
- **Sequential, ~70%:** the union-find 2.7, the Ward passes 3.6, the join 0.8, the piece split 0.55, tracing 1.0 and CDT 1.0 ms.
- **Data-parallel, ~17%:** Lab, blur, edge weights and colour together, ~2.2 ms.

By Amdahl's law, threads *inside* a live Frame can gain at most ~15%. Threads *across* Frames are the lever for frames per second. For photos the data-parallel share is larger (~40% at 2048: Lab, blur, weights, histogram and scatter), so within-Frame threads on the X3 and four A715s should take 512 ms to roughly 330–380 ms on the desktop. That figure is an estimate.

**Why not the GPU:**
- **Little to offload:** only Lab, the blur and the edge weights are GPU-shaped, ~2 ms of a 320×240 Frame. Upload, dispatch and readback on a mobile GPU cost on the order of a millisecond each way, and the sequential stages still need the result on the CPU.
- **Precision:** GLSL and SPIR-V float arithmetic is not guaranteed to be bit-identical to the CPU's (precision qualifiers, contraction, `sqrt`), so the cross-platform golden of route (B) would break unless every GPU kernel were integer-only.
- **Photos:** at 2048 the same stages gain about as much from 4–5 CPU threads, at a fraction of the code (no Vulkan in the NDK app, no shaders, no second build path for the desktop).

**Why not SVE2:**
- **No wider:** at an expected 128-bit vector length it is no wider than Neon, and the compiler's own tuning prefers Neon on the X3 and A510.
- **Unknown availability:** whether the Pixel kernel enables it in userspace is unconfirmed.
- **Little to gain:** the hot stages are not arithmetic-bound.
- **What SVE2 does add:** gathers and scatters, `HISTCNT`, predication. These could in principle help the histogram, but not enough to justify a second code path.

## 4. Ranked recommendation

1. **The three exact rewrites in the shared core:** the `neighbourPairs()` counting sort, the fused FH edge build, and integer-span Tessel colours.
   - **Gain:** −22 to −28% at 320×240 and −18% at 2048, with 24% less peak memory.
   - **Determinism:** the golden and the photo hashes hold, verified.
   - **Cost:** ~130 lines, and the desktop app gets the same speed-up.
   - **When:** do it with the core refactor (#51's `src/mosaic/core/`), as separate commits each guarded by the golden.
2. **Frame-level parallelism for the live preview:** two workers (later three) behind CameraX's keep-only-latest analyser, with an ADPF hint session targeting 33 ms.
   - **Gain:** ~1.75× throughput on two cores, with identical output. This is what makes 30 fps plausible if a single Frame takes 25–40 ms on the phone.
   - **Cost:** it lives entirely in the Android layer.
3. **Compact 8-byte FH edges**, and a resolution cap informed by memory as well as time.
   - **Why:** the base code peaks at 1.6 GB on a 12 MP photo.
   - **Determinism:** exact.
4. **Within-Frame threads for photos only:** row-parallel Lab and blur (OpenCV's `parallel_for_`, or the route-B port on a small pool), parallel edge weights and histogram, and parallel scatter.
   - **Gain:** expected −25 to −40% at 2048.
   - **Determinism:** exact, with fixed block decomposition and integer merges.
5. **Route (B)'s deterministic blur, vectorised across pixels with `-ffp-contract=off`.** It is needed anyway for desktop == Android, and auto-vectorisation (portable vectors, no intrinsics, per Google's NDK guidance) keeps it bit-identical.
6. **Optional, a look decision:** a recursive or box blur for large σ only. It cuts ~45 ms at 2048, leaves the 320 golden alone, and changes photo Mosaics slightly.
7. **Optional exact follow-ups,** worth ~1–2 ms at 320 and ~40 ms at 2048:
   - CSR adjacency for Ward, with Ward 1's pairs derived from the join;
   - a run-based CCL for the piece split;
   - crack bitmaps for tracing.

**Not recommended:**
- SVE2;
- GPU compute (Vulkan or GLES);
- OpenCL (unavailable);
- the TPU (no G3 access, NNAPI deprecated, not an NN workload);
- incremental or reused CDT;
- fixed-point Lab;
- FH early exit (measured: useless);
- parallel or approximate FH, and temporal reuse (both change the output or its contract).

## 5. Variants for the Pixel 8a benchmark (#53)

Build all variants with the NDK at -O3 for arm64-v8a. Run them with `adb shell` as a command-line binary, and report per-stage medians (the stage list of section 1) and the Mosaic hash:

1. **V0 base:** the unmodified `Adaptive.cpp`, OpenCV core+imgproc static (route A). Also with `-ffp-contract=off`, to see the FMA effect on time and hash.
2. **V1 exact rewrites:** `FAST_PAIRS`, `FAST_EDGES`, `FAST_COLOUR`, each alone and all together. They must give V0's hash on the phone; that checks the "exact" claim on arm64.
3. **Core class:** V0 and V1 pinned with `taskset` to the X3 (cpu 8), one A715 (cpu 4) and one A510 (cpu 0). This says where the sequential path must run and how much a misplaced thread costs.
4. **Frame-level parallelism:** 1, 2 and 3 concurrent `make()` calls on separate Frames: threads, or concurrent processes pinned to the X3 and A715s. Report Frames per second, not ms per Frame.
5. **OpenCV threads:** `cv::setNumThreads(1)` vs the default, at 2048, for Lab and blur.
6. **Sizes:** 320×240 (`fingers.png` + `fingers.mov` Frames), 1024, 1536, 2048 and the full 12 MP. Record **peak RSS** for each, alongside the time; this feeds the resolution cap.
7. **Sustained run:** 60 s of back-to-back 320×240 Frames on the X3, reporting the first and last 10 s, to see thermal throttling. (An ADPF session needs an app process, so measure its effect later in the app, not from `adb shell`.)
8. **Facts to record once:**
   - `getauxval(AT_HWCAP)` & `HWCAP_SVE`, and `getauxval(AT_HWCAP2)` & `HWCAP2_SVE2`;
   - `prctl(PR_SVE_GET_VL)`;
   - the Vulkan driver version;
   - the clock limits per cluster (`/sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq`).

## Sources

- LLVM, [AArch64Processors.td](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64Processors.td): the features and tuning of cortex-x3, cortex-a715 and cortex-a510.
- Arm, [Scalable Vector Extensions](https://developer.arm.com/Architectures/Scalable%20Vector%20Extensions): vector lengths from 128 to 2048 bits.
- Linux kernel, [arm64 SVE ABI](https://docs.kernel.org/arch/arm64/sve.html): `HWCAP_SVE`, `HWCAP2_SVE2`, `PR_SVE_GET_VL`.
- Android NDK: [CPU features](https://developer.android.com/ndk/guides/cpu-features), [ABIs](https://developer.android.com/ndk/guides/abis), [Neon](https://developer.android.com/ndk/guides/cpu-arm-neon), [Performance Hint](https://developer.android.com/ndk/reference/group/a-performance-hint), [ADPF codelab](https://developer.android.com/codelabs/adaptability-codelab).
- Android: [Migrate from RenderScript](https://developer.android.com/guide/topics/renderscript/migrate), [NNAPI migration guide](https://developer.android.com/ndk/guides/neuralnetworks/migration-guide), [Android 14 CDD](https://source.android.com/docs/compatibility/14/android-14-cdd), [native library namespaces](https://source.android.com/docs/core/permissions/namespaces_libraries).
- Google AI Edge, [Google Tensor with LiteRT](https://developers.google.com/edge/litert/next/tensor-sdk): supported SoCs G5 and G6, gated beta.
- Third-party, device-level reports, not authoritative: [pytorch/cpuinfo#437](https://github.com/pytorch/cpuinfo/issues/437) (Pixel 8 core numbering), [EnviousWispr-Android#464](https://github.com/Envious-Labs-LLC/EnviousWispr-Android/issues/464) (Pixel 8 OpenCL).
- Earlier research: [adaptive-core.md](https://github.com/spippoli/hipsterismo/blob/research/adaptive-core/docs/research/adaptive-core.md) (#51), [android-native-app.md](https://github.com/spippoli/hipsterismo/blob/research/android-native-app/docs/research/android-native-app.md) (#50).

## Experiments

These are in [`tensor-g3-optimisation-experiments/`](tensor-g3-optimisation-experiments/). All are throwaway, and none touch `src/`:
- `shim/Mosaic.h`: the OF shim plus timing hooks.
- `instrument.py`: copies `Adaptive.cpp` and adds per-stage timers.
- `variants.py`: adds the exact rewrites behind `FAST_PAIRS`, `FAST_EDGES`, `FAST_COLOUR` and `THREADS`.
- `profile.cpp`: the harness, which prints the hash, the counts and per-stage medians.
- `build.sh`, `run.sh`: build and run every variant from the repo root.

The user photos used at 2048 are not committed: any 12 MP JPEGs will do.
