# Speed of the adaptive core on budget and mid-range arm64 SoCs

Research for [#95](https://github.com/spippoli/hipsterismo/issues/95), part of map #85, feeding the support-threshold ticket #89. This research decides nothing. All numbers below are **estimates**: no phone other than the Pixel 8a was measured.

## Question

How fast is the adaptive core on 2019+ arm64 phones (Cortex-A76/A77/A78-class big cores, A55-only budget SoCs, Snapdragon 7xx / Dimensity 7xxx)? Two follow-ups: does the analysis thread land on a big core without ADPF, and how do the photo's 5 threads behave on 4+4 and 2+6 clusters?

## Anchors (measured in this repo)

| Measurement | Value | Source |
|---|---|---|
| Live camera Frame, 320×240, X3 (2.91 GHz), in the app | 22–24 ms core | CLAUDE.md, #64 |
| `fingers.png` core-only, one pinned core, before #76 | X3 17.8 ms, A715 (2.37 GHz) 27.3 ms, A510 (1.70 GHz) 123 ms | #53 |
| Same, after #76 (bit-exact speed-ups, about −30%) | desktop 17 → 12 ms | CLAUDE.md, #76 |
| 2048×1536 photo, core | 450 ms pinned to the X3, 420 ms on the X3 + 4 A715 | CLAUDE.md, #76 |
| Camera photo, shutter to result | 0.93–1.10 s, of which core 0.50–0.62 s | CLAUDE.md, #76 |
| 4624×3472 JPEG decoded to 2048 | about 70 ms on an A715 | CLAUDE.md |
| Peak RSS, 2048 photo | 416 MB before #76, about −24% expected from the 8-byte edges | #53, #55 |

Two ratios come out of #53 and carry the whole estimate. They come from the pre-#76 core, but #76 changed the code, not how much of it is memory-bound versus compute-bound:

- **X3 / A715 = 1.53×** at a clock ratio of 1.23, so the X3 has about 1.25× the A715's per-clock speed on this workload.
- **X3 / A510 = 6.9×** at a clock ratio of 1.71, so the X3 has about 4× the A510's per-clock speed. An in-order little core does especially badly on this workload, which is mostly union-find, Ward and tracing. #55 measured it as about 70% sequential graph work.

## Method

1. Take the **A715 at 2.37 GHz** as the reference big core. A live camera Frame costs about 23 ms × 1.53 ≈ **35 ms** on it.
2. Place each other core relative to the A715 per clock, using **Arm's own generation-over-generation claims** (sources at the end):
   - A715 is about 5% faster than the A710, and the A710 about 10% faster than the A78, so A78 ≈ 0.87 × A715.
   - A78 is about 7% faster single-thread than the A77, so A77 ≈ 0.81.
   - A77 has about 20% higher IPC than the A76, so A76 ≈ 0.68.
   - A75 ≈ 0.52 and A73 ≈ 0.45 are extrapolated from the same trend. These are the weakest numbers in the table.
   - The X1 counts as A715-level, since Arm says the A715 "matches the performance of Cortex-X1".
3. Scale linearly with the SoC's clock: `ms ≈ 35 × 2.37 / (relative speed × GHz)` = `84 / (relative speed × GHz)`.
4. Place the little cores from the measured A510:
   - Arm claims "up to 35%" more performance for the A510 than the A55 at the same clock (SPECint2006).
   - Chips and Cheese measured the two with similar L1 and L2 latencies and similar FP and vector throughput.
   - So the A55 is taken as 1.15–1.35× slower than the A510 per clock.
   - The A53 is taken as no faster than the A55, so it is probably somewhat slower.
5. Photo, 2048 px: scale the X3's 450 ms by the same per-core ratio, then take 0–20% off for the helper threads (see the scheduling section).
   - To that add about 0.45 s of non-core time (capture, decode, drawing), the Pixel's total minus its core time.
   - Non-core time is scaled up on slow SoCs.

**Uncertainty.** Arm's per-generation claims are "up to" figures, and some compare different clocks, so they probably overstate the newer cores' lead. Budget SoCs also have smaller caches and slower DRAM than the Tensor G3, which hurts a latency-bound workload more than Geekbench suggests. Read every number as **±25%**. I could not retrieve per-core SPEC2017 tables: AnandTech's site now redirects to its forums, the Wayback Machine is unreachable from here, and Geekbench's browser returns 403. A one-day measurement on two or three real phones with `core-on-device.sh` would replace this table.

## Estimates

Preview fps assumes about 5 ms per Frame outside the core (as on the Pixel) and is capped at the camera's 30. The photo time is from the shutter to the result, at 2048 px, `min patch area` 32.

| Class | Example SoCs (fastest cores) | Live Frame, core ms | Preview fps | Photo core | Photo to result |
|---|---|---|---|---|---|
| **Reference: Pixel 8a, X3 2.91** | Tensor G3 | 22–24 (measured) | 27–31 (measured) | 0.50–0.62 s (measured) | 0.93–1.10 s (measured) |
| X1 2.84 | Snapdragon 888 | ~30 | ~28 | ~0.5 s | ~1.0 s |
| A715 2.6–2.8 | Snapdragon 7 Gen 3 (2.63), Dimensity 7200 (2.8) | 30–32 | 27–30 | 0.5–0.6 s | 1.0–1.1 s |
| A710 2.4 | Snapdragon 7 Gen 1 | ~37 | ~24 | ~0.65 s | ~1.15 s |
| A77 2.84 | Snapdragon 865 | ~36 | ~24 | ~0.6 s | ~1.1 s |
| A78 2.4–2.6, 4+4 | Dimensity 7300 (2.5), Dimensity 7050 (2.6), Snapdragon 7s Gen 2 (2.4), 778G (2.4) | 37–40 | 22–24 | 0.6–0.7 s | 1.1–1.3 s |
| A76 2.84 | Snapdragon 855 | ~43 | ~21 | ~0.75 s | ~1.2 s |
| A78 2.0–2.2, 2+6 | Snapdragon 4 Gen 1 (2.0), 4 Gen 2 (2.2), 695 (2.2), Dimensity 7020 (2.2) | 44–48 | 19–20 | 0.75–0.85 s | 1.3–1.5 s |
| A76 2.0–2.2, 2+6 | Helio G99 (2.2), Dimensity 700 / 6080 / 6100+ (2.2) | ~56 | ~16 | ~0.95–1.05 s | 1.5–1.9 s |
| A73 2.4, 4+4 | Snapdragon 680 / 685 | ~78 | ~12 | ~1.3 s | ~1.9 s |
| A75 1.6–2.0, 2+6 | Helio G85 / G88 (2.0), Unisoc T606 / T616 (1.6–2.0) | 80–100 | 10–12 | 1.4–1.8 s | 2.0–2.4 s |
| **A510 1.7 (Pixel's little, measured before #76)** | — | ~160 | ~6 | — | — |
| **A55-only, 8× 2.0** | Exynos 850 | 155–185 | 5–6 | 2.4–3.6 s | 3–4 s |
| **A53-only, 8× 2.2–2.3** | Helio G35 / G36 / G37 / P35 | 180–230 | 4–5 | 3–4.5 s | 3.5–5 s |

Against the thresholds of #59 (≥ 15 fps preview, ≤ 1.5 s photo):

- **Comfortable:** any phone with an A77, A78, A710, A715, X1 or newer at ≥ 2.4 GHz. That covers 2020+ flagships and 2021+ Snapdragon 7xx and Dimensity 7xxx. Expect 20–30 fps and photos in 1.0–1.3 s.
- **Marginal:**
  - A78 at 2.0–2.2 GHz (Snapdragon 4 Gen 1/2, 695): about 20 fps, photos about 1.3–1.5 s.
  - A76 at 2.2 GHz (Helio G99, Dimensity 6100+): about 16 fps, right on the floor, and photos over 1.5 s.
  - These are the most common budget SoCs of 2022–2025, so the floor decides whether they are in.
- **Below the floor:**
  - A73 and A75 big cores (Snapdragon 680, Helio G85, Unisoc T606): about 10–12 fps, photos about 2 s.
  - A55-only and A53-only phones (Exynos 850, Helio G3x): about 5 fps live and photos of 3–5 s. On these the live preview is not usable, as #53 found for the A510. At most a photo-only mode would make sense.
- **30 fps** needs a core time of 28 ms or less. Only an X-class core or an A715 at 2.8 GHz or more reaches that.

Memory is a second floor for photos. A 2048 photo peaks at about 320–420 MB native (#53, #55), which an A55-only phone with 3 GB of RAM may not hold without the low-memory killer stepping in. Unmeasured.

## Does the analysis thread land on a big core without ADPF?

**Most likely yes, on any two-cluster SoC.** The reason is that the thread is CPU-bound, not that something marks it. Kernel and Android documentation:

- **EAS places a task by its utilisation.**
  - The kernel's capacity-aware scheduling keeps a task on a CPU only while `task_util(p) < capacity(cpu)`.
  - It picks the wakeup CPU by the same test, with uclamp applied.
  - A task that outgrew its CPU is a "misfit" task, and the load balancer actively migrates it to a CPU with more capacity.
  - Utilisation is the duty cycle normalised to the biggest CPU at its maximum frequency, tracked by PELT.
- **The analysis thread runs back to back.** With `STRATEGY_KEEP_ONLY_LATEST` it takes the next Frame as soon as it finishes.
  - On a little core it is busy close to 100% of the time, so its utilisation reaches the little core's whole capacity, it becomes misfit and moves up.
  - On a big core it runs 50–100% of each 33 ms period. That is still far above a little core's capacity (an A55 has roughly a quarter or less of an A78's), so it stays big.
- **ADPF needs API 31 and is optional.** `PerformanceHintManager` is API 31. `createHintSession()` returns `null` "if hint session is not supported on this device", and the app already handles that (`manager?.createHintSession(...)` in `PreviewPipeline.kt`).
- **What ADPF adds over plain EAS:**
  - A frequency floor: schedutil's frequency follows utilisation, so a 50–70% duty thread may run at a lower clock without hints.
  - On a tri-cluster SoC, a push to the prime core rather than a mid core: on the Pixel, the X3 rather than an A715, 23 versus about 35 ms.
  - On two-cluster budget SoCs with no prime core, it matters less.
- **Where it can still go wrong (unverified, device-specific):**
  - The cpuset decides which cores the process may use. This repo saw it with the screen locked: the test process moved to the `foreground` cpuset, cpus 0–7, which drops the X3 (CLAUDE.md, `CorePerformanceTest`). While the app is the top app it normally gets every core, but the cpuset contents are each OEM's choice.
  - Vendor schedulers and boosts (MediaTek, Unisoc, some OEM kernels) and aggressive thermal caps on cheap phones can hold the big cores at lower clocks.
  - On a 2+6 SoC the two big cores are shared with the camera HAL, RenderThread and the UI, so the analysis thread may lose some of its big-core time to contention.
  - None of this can be checked without the phones. The `hipsterismo` logcat line (fps, core ms) on a test device answers it directly.
- **Without ADPF the thread is not lost to the little cores. Expect, at worst, 10–30% slower Frames from lower clocks or a mid core.**

## How do the 5 photo threads behave on 4+4 and 2+6?

How the threads are used:

- `parallelFor()` (`src/mosaic/core/AdaptiveParallel.h`) is used only for Frames of 2²⁰ pixels or more. It uses at most 5 threads, the caller included (`min(hardware_concurrency, 5)`).
- It hands out chunks of 16–32 rows, or one 256-float strip for the column blur, "as threads ask for them, so a slow core takes fewer".
- Only the Lab conversion, both blur passes and the FH edge weights run in parallel.
- The union-find, the scatter, Ward, tracing and CDT, which are most of the photo time, run on the calling thread alone. In the app that is the `mosaic-preview` thread, with its ADPF session.

What follows:

- **The calling thread sets the photo time**, so the photo goes about as fast as the best core that thread gets. The Pixel shows how little the helpers add: four A715 helpers took only 450 → 420 ms off (−7%).
- **The helpers are new threads in each parallel stage.** They have no utilisation history, so EAS may well put them on little cores, and a stage of 10–30 ms is shorter than PELT needs to promote them. That is harmless:
  - with on-demand chunks, a little core just takes fewer of them;
  - the worst tail is one chunk of 16 rows on an A55, about a millisecond or two;
  - starting the threads costs well under a millisecond.
- **On 4+4** (Snapdragon 680, 7s Gen 2, Dimensity 7300), up to three helpers can share the big cluster with the caller. Expect the parallel stages to give up to −15–20% of the core time, a little more than on the Pixel, because the helpers are the same core type as the caller.
- **On 2+6** (Helio G99, Snapdragon 4 Gen x / 695, Unisoc T606), one helper gets the second big core and the others land on A55s. Expect −5–10%.
- **On 8× A55**, all five threads are equal cores. The parallel stages then scale well, perhaps −20%, but on a 3 s baseline.
- **No change to `maxThreads` looks warranted.** More threads would only add A55s to stages that are already a minority of the time. Fewer would lose the little that 4+4 gains.

## Sources

Repo (measured):
- CLAUDE.md, the Android app and Speed sections: in-app preview, photo and decode times.
- [#53](https://github.com/spippoli/hipsterismo/issues/53): Pixel 8a per-core core times (X3, A715, A510) and peak RSS.
- [#55](https://github.com/spippoli/hipsterismo/issues/55) and [tensor-g3-optimisation.md](https://github.com/spippoli/hipsterismo/blob/research/tensor-g3-optimisation/docs/research/tensor-g3-optimisation.md): stage profile (about 70% sequential graph work) and the RSS estimate.
- [#76](https://github.com/spippoli/hipsterismo/issues/76): post-speed-up photo times.
- `src/mosaic/core/AdaptiveParallel.h` and `android/.../PreviewPipeline.kt`: threading and ADPF use.

Arm (first-party claims):
- [Cortex-A715 product page](https://www.arm.com/products/silicon-ip-cpu/cortex-a/cortex-a715) and [Arm newsroom, June 2022](https://newsroom.arm.com/news/arm-total-compute-solutions-redefine-visual-experiences-and-supercharge-mobile-gaming): A715 is 5% faster than the A710 and matches the X1.
- [Arm newsroom, 2021](https://newsroom.arm.com/news/arm-total-compute-solutions-bring-performance-security-and-armv9-to-the-broadest-range-of-client-devices) and [First Armv9 Cortex CPUs blog](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/first-armv9-cpu-cores):
  - A710 is 10% faster than the A78.
  - A510 is up to 35% faster than the A55, measured with SPECint_base2006 at the same process and frequency.
- [Cortex-A78 blog](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/arm-cortex-a78-cpu): A78 is 7% faster single-thread than the A77.
- [Cortex-A77 blog](https://community.arm.com/arm-community-blogs/b/architectures-and-processors-blog/posts/new-arm-cortex-a77-provides-compute-performance-leadership) and [product page](https://www.arm.com/products/silicon-ip-cpu/cortex-a/cortex-a77): A77 has 20% higher IPC than the A76.

Independent measurement:
- [Chips and Cheese, "Arm's Cortex A510: Two Kids in a Trench Coat"](https://old.chipsandcheese.com/2023/10/01/arms-cortex-a510-two-kids-in-a-trench-coat/):
  - the A510's L1 and L2 latencies are similar to the A55's, and so are its FP and vector throughputs;
  - its 128-bit loads give it more bandwidth.

SoC specs (vendor pages):
- [Exynos 850](https://semiconductor.samsung.com/processor/mobile-processor/exynos-850/): 8× A55 at 2.0 GHz.
- [Helio G99](https://www.mediatek.com/products/smartphones/mediatek-helio-g99): 2× A76 at 2.2 + 6× A55 at 2.0.
- [Helio G85](https://www.mediatek.com/products/smartphones/mediatek-helio-g85): 2× A75 at 2.0 + 6× A55 at 1.8.
- [Helio G36](https://www.mediatek.com/products/smartphones/mediatek-helio-g36): 8× A53 at 2.2.
- [Dimensity 7300](https://www.mediatek.com/products/smartphones/mediatek-dimensity-7300): 4× A78 at 2.5 + 4× A55.
- The other SoCs' core layouts are from their makers' spec sheets as commonly reported, not re-fetched here.

Scheduling (first-party):
- [Linux kernel, capacity-aware scheduling](https://docs.kernel.org/scheduler/sched-capacity.html): the `task_util < capacity` fit, misfit migration and PELT utilisation.
- [Android `PerformanceHintManager`](https://developer.android.com/reference/android/os/PerformanceHintManager): API 31, and `null` where it is unsupported.
- [AOSP, jank and jitter](https://source.android.com/docs/core/debug/jank_jitter): little cores and capacity-unaware RT scheduling.
