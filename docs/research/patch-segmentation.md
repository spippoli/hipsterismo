# Patch segmentation and constrained triangulation at 320×240

Research for [#15](https://github.com/spippoli/hipsterismo/issues/15), part of map [#6](https://github.com/spippoli/hipsterismo/issues/6).

**Question.** Adaptive mode now works Patches first ([#10](https://github.com/spippoli/hipsterismo/issues/10)): segment the Frame into Patches, simplify each Patch boundary, and triangulate with the boundaries as constrained edges, so no Tessel crosses two Patches. `min tessel area` is the smallest Patch (and sets the colour tolerance only indirectly), `max tessel area` the largest Tessel. Four parts: (1) which segmentation fits a min-area control, (2) how to extract and simplify shared boundaries so the mesh stays conforming and covers the Frame border, (3) which constrained triangulation library, with Steiner points for the max area, determinism and a compatible licence, (4) what a Frame costs.

Claims are tagged **[src]** when the cited source states them, **[measured]** when they come from the benchmark below, and **[derived]** when they are my own reasoning. Figures marked *estimate* were not measured.

## TL;DR

1. **Segmentation: Felzenszwalb–Huttenlocher (FH) graph segmentation, written in `src/mosaic/` (about 60 lines), with `k = κ·min tessel area` and `min_size = min tessel area`.**
   - FH's threshold is `τ(C) = k/|C|`: two components merge when their boundary difference is below their internal difference plus `k/|C|`. With `k = κ·min`, a Patch of exactly `min` pixels tolerates a colour step of `κ` (ΔE), a Patch 4× larger tolerates `κ/4`. So `min tessel area` sets the tolerance, and `κ` is a fixed internal constant (2 ΔE in the benchmark), not a user parameter.
   - FH's own post-pass merges every component smaller than `min_size` along the cheapest crossing edge, i.e. into its most similar neighbour. That is exactly the ticket's "smaller regions merge into their most similar neighbour".
   - On `fingers.png` the Patch boundaries follow the fingers' outline and the dark gaps. Weakness: on the smooth background gradient FH makes blocky, axis-aligned Patches.

   [src: Felzenszwalb & Huttenlocher 2004 §3–5; FH reference code `segment-image.h`; measured]
2. **Do not depend on `opencv_contrib`'s `ximgproc` for it.** It is installed on Ubuntu (`libopencv-dev` depends on `libopencv-contrib-dev`), and OF's Linux build links the system `opencv4` via pkg-config. But the OpenCV that ships inside OF's `ofxOpenCv` addon (used on the other platforms) has no `ximgproc` headers, and the app targets every OF platform. FH is short enough to own; FH's reference code is GPL-2, OpenCV's port is BSD-3, so write it from the paper. [src: `apt-cache depends libopencv-dev`; `ofxOpenCv/addon_config.mk`; local `ofxOpenCv/libs/opencv/include/opencv2` listing; FH `COPYING`; derived]
3. **Shared boundaries: trace crack edges on the pixel-corner lattice, with the outside of the Frame as label −1, split them into chains at junctions, and simplify each chain once.**
   - A crack edge separates two pixels with different labels. Junctions are corners where ≥ 3 crack edges meet, plus the 4 Frame corners. Each chain has exactly two Patches (or one Patch and the outside) on its sides.
   - Because the outside is a label, the Frame border is traced like any other boundary. Straight border runs collapse to their endpoints under Douglas–Peucker (DP), so the border stays exactly covered.
   - DP runs on each chain with both endpoints fixed (ε = 1.5 px). Every Patch sees the same simplified chain on both sides, so the mesh has no gaps and no T-junctions.
   - DP can still make two chains cross or touch. A crossing check with exact integer predicates finds those, and the offending chains are re-simplified with ε halved, down to the raw crack chain, which never crosses (the label map is a planar subdivision). 48–124 of the 139–339 chains needed it, in up to 5 rounds, and the whole simplification took 0.3–0.9 ms per Frame.

   [measured; derived; DP: Douglas & Peucker 1973; topology repair in the spirit of Saalfeld 1999]
4. **Triangulation: artem-ogre/CDT 2.0.2 (MPL-2.0, header-only), one constrained Delaunay triangulation of the whole Frame, not one per Patch.**
   - Insert all chain vertices and all simplified chain segments as constraint edges; the 4 Frame corners make the convex hull the Frame rectangle. One triangulation is conforming by construction, so a Patch with holes or a Patch touching itself at a corner needs no special case.
   - For `max tessel area`, rebuild with the centroids of the triangles above the max (snapped to a 1/8 px grid) until none is left. This never splits a Patch boundary, and needed 2–70 Steiner points.
   - CDT's own `refineTriangles(LargestArea)` works, but it is Ruppert refinement: it splits every encroached constraint edge, and added about 1 470 Steiner points whatever the max area (even 100 000 px²), which floods Patch boundaries with slivers.
   - Shewchuk's Triangle is not free for commercial distribution, and the repo has no licence yet. poly2tri needs one simple polygon per call and forbids holes that touch the outline, which Patches routinely do.

   [src: CDT `docs/README.md`, `Triangulation.h`; Triangle `README`; poly2tri `README.md`; measured]
5. **Cost: 11–16 ms per Frame for the whole pipeline with `-O3`** (i7-11370H, single thread), about 110 ms at `-O0`. The FH graph pass (edge weights, bucket sort, union-find over 307 000 8-connected edges) is half of it. The meshes were bit-identical across 15 runs, every pixel was covered exactly once, and the Tessel areas summed to 76 800. [measured]
6. **The alternatives were worse.**
   - **Ward agglomeration** (merge the pair with the smallest `n₁n₂/(n₁+n₂)·|μ₁−μ₂|²` until no Patch is below min) uses no κ at all and gives smooth bands that follow the contours well, but min barely controls it: 52 Patches at min 64, 32 at min 256. The last speck to reach min drives everything else to ΔE ≈ 20. It costs 19 ms, or 150 ms when started from single pixels.
   - **Small-into-most-similar only** (no tolerance for large regions) leaves the background as a patchwork of min-sized blocks.
   - **SLICO** superpixels have a size parameter, not a min area, ignore similar-colour neighbours (1 183 superpixels of about 64 px), and took 41 ms. **Mean-shift** (`pyrMeanShiftFiltering`) took 120 ms before any labelling.

   [measured; src: Achanta et al. 2012; OpenCV `imgproc.hpp`]

**Pictures** (rendered at 3×; Tessel colour = mean of the pixels whose centre lies in the triangle):
[FH min 64](patch-segmentation-bench/fh-min64-max1024-eps1.5-mosaic.png),
[FH min 64 wireframe with Patch boundaries in red](patch-segmentation-bench/fh-min64-max1024-eps1.5-wire.png),
[FH min 256](patch-segmentation-bench/fh-min256-max1024-eps1.5-mosaic.png),
[Ward min 64](patch-segmentation-bench/ward-min64-max1024-eps1.5-mosaic.png); Patch maps:
[FH](patch-segmentation-bench/fh-min64-max1024-eps1.5-patches.png),
[Ward](patch-segmentation-bench/ward-min64-max1024-eps1.5-patches.png),
[small-first](patch-segmentation-bench/small-min256-max1024-eps1.5-patches.png).
Unlike the rejected error-driven insertion of #10, the fingers' outline is reproduced in all of them.

## 1. Segmentation with a min-area control

### 1.1 Felzenszwalb–Huttenlocher graph segmentation

- **Algorithm.** Pixels are graph vertices, and 8-connected neighbours are joined by edges weighted by colour difference. Edges are processed in increasing weight. Components `C₁`, `C₂` merge when the edge weight is at most `min(Int(C₁) + τ(C₁), Int(C₂) + τ(C₂))`, where `Int(C)` is the largest MST edge inside `C` and `τ(C) = k/|C|`. Runtime is O(m log m) for m edges, nearly linear on the grid graph. [src: FH 2004 §3–4, eq. 4–5, §5]
- **k is not a min size.** "k is not a minimum component size. Smaller components are allowed when there is a sufficiently large difference between neighboring components." [src: FH 2004 §3.1] The min size comes from the reference code's post-pass, which walks the sorted edges again and joins any two components when either is smaller than `min_size`. [src: FH `segment-image.h` lines 124–130; same in OpenCV `graphsegmentation.cpp` `filterSmallAreas`]
- **Turning min area into the tolerance.** With `k = κ·min`, `τ(C) = κ·min/|C|`. The tolerance shrinks as Patches grow, and its scale is set by min alone. Raising min from 64 to 256 took the Patch count from 50 to 12 [measured]. κ is a constant of the module, playing the role of the paper's fixed `k = 300` for 320×240 images [src: FH 2004 §5].
- **Choices in the benchmark.** Lab instead of RGB, Gaussian σ = 0.8 as in the paper, one run on Lab distances instead of the paper's intersection of three per-channel runs (the paper allows either), κ = 2. Edges are bucket-sorted in 1/16 ΔE bins with ties by edge index, so the result is deterministic and the sort is O(m). [src: FH 2004 §5; benchmark]
- **On `fingers.png`.** Boundaries follow the outline of both fingers, the knuckle shadows and the dark gaps. On the smooth grey background gradient the components are blocky and axis-aligned, a known effect of the grid graph with a growing `Int(C)`. The leakage (pixels whose Patch differs from the majority Patch of their Tessel, caused by the simplification) was 0.8–1.8%. [measured; pictures above]
- **Implementations.**
  - `cv::ximgproc::segmentation::createGraphSegmentation(sigma, k, min_size)` in OpenCV contrib, BSD-3. It uses 4-connectivity (each edge twice) and `std::sort`. It took 23 ms on fingers. [src: `graphsegmentation.cpp` 4.10.0; measured]
  - The authors' reference C++ code is GPL-2. [src: `segment/COPYING`, file headers]
  - Own code: about 60 lines (union-find, edge list, bucket sort, two passes). The benchmark's version takes 5.5–6.5 ms including the edge build. [measured]

### 1.2 Region merging on the region adjacency graph (RAG)

Two variants were measured. Both start from FH with a tiny fixed `k₀ = 5` (≈ 1 900 regions), because starting from single pixels (74 000 regions) cost 140 ms of merging. [measured]

- **Ward.** Repeatedly merge the adjacent pair with the smallest Ward cost `n₁n₂/(n₁+n₂)·|μ₁−μ₂|²` (Lab means), and stop as soon as no Patch is smaller than min. It has no κ, and the boundaries are the best of the three: smooth bands along the shading of the fingers. But the stop rule is set by the hardest small region (a high-contrast speck must still merge), so the last merge was always around ΔE 15–22, and min controls the result weakly: 52 Patches at min 64, 32 at min 256. Merge cost 4–6 ms. [measured]
- **Small-first.** Only pairs that involve a Patch below min are merged, closest mean first. Large regions are never merged, so the background stays a patchwork of min-sized blocks (127 Patches at min 256, 530 at min 64). Not usable. [measured]

### 1.3 SLIC / SLICO

- SLIC clusters pixels in (Lab, x, y) with k-means limited to a 2S×2S window around each of the grid seeds, where S is the region size. SLICO is the "zero parameter" variant that adapts the compactness per cluster. [src: Achanta et al. 2012; OpenCV `slic.hpp`]
- Its control is the *average* superpixel size, not a minimum, and it never merges two similar neighbours: a flat background becomes a regular grid of superpixels. A min area could be had as region size `√min` plus a RAG merge afterwards, which brings back §1.2. [src; derived]
- `createSuperpixelSLIC(SLICO, 8, 10)` + 10 iterations + `enforceLabelConnectivity` took 41 ms for 1 183 superpixels. It is in `ximgproc`, with the same availability problem. [measured]

### 1.4 Mean-shift

- Comaniciu–Meer mean-shift filters each pixel towards the mode of the joint spatial–colour density, then regions are the connected components of nearby modes. Its controls are a spatial and a colour bandwidth (`sp`, `sr`), i.e. an explicit colour tolerance, which the ticket wants to avoid. [src: OpenCV `pyrMeanShiftFiltering` docs; Comaniciu & Meer 2002, not re-read]
- `pyrMeanShiftFiltering(sp = 8, sr = 16)` alone took 120 ms on fingers, before labelling and a min-size pass. Too slow for 30 fps. [measured]

### 1.5 k-means + connected components

- Quantise the Frame to K colours and take connected components. K is a global colour count, components of one cluster fragment across gradients into many tiny pieces, and the result changes discontinuously with K. It needs the same small-component merge as FH and adds nothing over it. Not measured. [derived]

## 2. Shared boundaries

### 2.1 Extraction

- **Lattice.** Put boundary vertices on pixel *corners* (`(W+1)×(H+1)` of them), not pixel centres. A horizontal crack edge `(x,y)–(x+1,y)` exists when pixel `(x,y−1)` and pixel `(x,y)` have different labels; vertical ones likewise. Pixels outside the Frame have label −1, so the 4 Frame sides are crack edges too. [benchmark design]
- **Junctions.** A corner where 3 or 4 crack edges meet is a junction, and so are the 4 Frame corners. A corner with 2 crack edges always has the same two labels on both, so chains never change their pair of Patches between junctions. Four crack edges with only two labels (a checkerboard corner) is also a junction, which lets a Patch touch itself at a point. [derived; benchmark]
- **Chains.** Walk from every junction along each unvisited crack edge until the next junction. Whatever is left are closed loops with no junction (a Patch entirely inside another); they get the first corner in scan order as anchor. Each chain is stored once and is shared by the two Patches on its sides. There were 139–339 chains on fingers, traced in under 1 ms. [measured]
- **Frame border.** Patch boundaries that reach the border end in a junction on it (two Patches plus the outside), so the border is split into chains between those junctions. All vertices are integers on the border lines, so after simplification every border chain is exactly its straight segment, and coverage of the Frame rectangle is exact. [derived; measured: covered area 76 800 and 0 uncovered pixels in every run]

### 2.2 Simplification without breaking topology

- **Per chain, not per Patch.** DP on each chain with its two junctions fixed gives the same polyline to both Patches, so neighbouring Tessels share vertices and there are no gaps or T-junctions. Simplifying each Patch's closed contour separately (e.g. `cv::approxPolyDP` on `findContours` output) would pick different points on the two sides of a shared border. [src: Douglas & Peucker 1973 (algorithm); derived]
- **Closed loops.** Split at the anchor and the point farthest from it, run DP on both halves, and keep a third point if the loop would otherwise collapse to a segment. [benchmark]
- **Crossings.** DP gives no topological guarantee: a simplified chain can cross another chain or pass on the wrong side of a vertex, and two chains between the same junctions can collapse onto the same segment. Saalfeld (1999) makes DP topology-preserving by refining until no conflict remains, and de Berg, van Kreveld & Schirra (1998) give a provably correct subdivision simplification. [src: titles and abstracts; papers not re-read] The benchmark uses the simplest form of the first idea:
  - Grid-bucket all simplified segments (16 px cells), and test every pair in a cell with exact integer orientation tests. Only a shared endpoint is allowed contact; crossings, touching, collinear overlap and duplicate segments are conflicts.
  - Re-simplify both chains of every conflict with ε halved; below 0.3 px use the raw crack chain. Raw chains cannot conflict, so this terminates.
  - At ε = 1.5: 48–124 chains repaired, up to 5 rounds, 0.3–0.9 ms in total. No crossing reached CDT in any run, so CDT's `IntersectingConstraintEdges::NotAllowed` never threw. [measured]
- **ε.** For Ward at min 64, leakage was 1.5% at ε = 1.0, 2.2% at 1.5 and 3.2% at 2.0, with 94, 124 and 129 chains repaired. FH at min 64 and ε = 1.5 leaked 1.8%. Small staircases remain on diagonal boundaries at every ε tried; they come from short chains between dense junctions, where DP has nothing to remove. [measured; pictures]

## 3. Constrained triangulation

### 3.1 Candidates

| | artem-ogre/CDT 2.0.2 | Triangle 1.6 (Shewchuk) | poly2tri (jhasse fork) | `cv::Subdiv2D` |
|---|---|---|---|---|
| Constrained edges | yes | yes (`-p`, PSLG) | yes, as polygon outline + holes | no |
| Input shape | any set of non-crossing segments; holes auto-detected; overlapping boundaries allowed | PSLG with holes | one simple polygon per call; holes must not touch each other or the outline | points only |
| Max-area Steiner points | `refineTriangles(LargestArea)` (Ruppert) | `-a` (with `-q` quality); `-Y` stops boundary splitting | user-supplied Steiner points only | — |
| Robustness | adaptive exact predicates (Lenthe); duplicate points must be removed first; `TryResolve` splits crossings | Shewchuk's adaptive exact predicates | no input validation; repeated points within ε not supported | absolute epsilon (see #14 research) |
| Determinism | yes: SplitMix64 with a fixed default seed for walks and shuffles | yes | yes | yes |
| Licence | MPL-2.0 (file-level copyleft; using the unmodified headers is fine in any project) | free for non-commercial use; "distribution of this code as part of a commercial system is permissible ONLY BY DIRECT ARRANGEMENT WITH THE AUTHOR" | BSD-3 | Apache-2.0 / BSD |
| Packaging | header-only, C++98+ | one C file | a few C++ files | already linked |

[src: CDT `docs/README.md`, `Triangulation.h/.hpp` 2.0.2 (`b4413ee`); Triangle 1.6 `README`, `triangle.c` usage text; poly2tri `README.md` and `LICENSE` (master `4732e46`); #14 research for `Subdiv2D`]

- **Why CDT.** It takes the chains as they are: no per-Patch polygons, no hole bookkeeping, and holes that touch outlines are just more segments. It is the only permissively licensed candidate with built-in refinement. The repo has no `LICENSE` file yet, so Triangle's non-commercial clause would decide the project's licence for it. [src; derived]
- **Why one triangulation for the whole Frame.** Per-Patch triangulation (e.g. poly2tri per Patch) is conforming only if both sides use exactly the same boundary vertices and no library adds Steiner points on boundaries (Triangle's `-Y`). One global CDT is conforming by construction, and each triangle lies in exactly one Patch because no triangle crosses a constraint edge. [derived; measured: 0 doubly covered pixels]

### 3.2 Steiner points for `max tessel area`

- **CDT's refinement is Ruppert's algorithm.** "Encroached constraint edges are split before a circumcenter is inserted." With `LargestArea` = 1024 px² it added 1 475 Steiner points, and with 100 000 px² still 1 471. The splits come from the many short, encroached boundary segments, not from area, and they fill Patch boundaries with slivers. [src: CDT README "Algorithm"; measured]
- **Centroid rounds instead.** Triangulate, collect the triangles above max, add their centroids (snapped to 1/8 px), rebuild, repeat until none is above max. A centroid is strictly inside its triangle, so it never lies on a constraint edge and boundaries are never split. On fingers, 2–20 Steiner points at max 1024 and 66–72 at max 256, in 1–3 rebuilds; CDT time rose from 2 ms to 4.5 ms at max 256. [measured] With an incremental `insertVertices` on the unfinalised triangulation the rebuild could be avoided; it was not needed at this size. [derived]
- **Shape.** Without a quality criterion, long Patch boundaries produce fans of thin triangles (visible in the wireframe). That is a look question for the prototype: a minimum-angle pass would cost Steiner points on boundaries again. [measured; derived]

## 4. Benchmark

Source: [`patch-segmentation-bench/patch_bench.cpp`](patch-segmentation-bench/patch_bench.cpp); raw output: [`results.txt`](patch-segmentation-bench/results.txt). Build line in the file header; it needs CDT 2.0.2 headers and OpenCV 4 with `ximgproc` (only for the reference timings).

- **Machine.** Intel Core i7-11370H (4 cores / 8 threads, frequency not pinned), Linux 7.0, g++ 15.2.0, OpenCV 4.10.0 (Ubuntu `4.10.0+dfsg-7ubuntu5`), `cv::setNumThreads(1)`.
- **Flags.** `-std=c++20 -O3 -DNDEBUG`, as OF's Linux Release; `-O0` for Debug.
- **Timing.** Median of 15 runs per configuration. Other work was running on the machine; a more loaded run (in `results.txt`) was 30–40% slower, so treat ±30% as the noise band.
- **Workload.** `tests/fixtures/fingers.png`, 320×240. ε = 1.5 px, κ = 2, k₀ = 5, Lab, blur σ = 0.8. Tessel colour = mean RGB of the pixels whose centre lies in the triangle (top-left tie rule), which also checks coverage.

| Segmentation | min | max | Patches | Steiner | Tessels | lab+blur | FH | merge | chains | simplify | CDT | colour | **total** |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FH, k = 2·min | 64 | 1024 | 50 | 2 | 1 761 | 0.8 | 6.3 | — | 0.9 | 0.7 | 2.1 | 1.8 | **12.7** |
| FH, k = 2·min | 128 | 1024 | 26 | 11 | 1 230 | 0.8 | 6.2 | — | 0.8 | 0.4 | 2.9 | 1.7 | **13.0** |
| FH, k = 2·min | 256 | 1024 | 12 | 20 | 859 | 0.7 | 5.5 | — | 0.7 | 0.3 | 1.8 | 1.5 | **10.8** |
| FH, k = 2·min | 64 | 256 | 50 | 66 | 1 889 | 0.8 | 6.7 | — | 0.9 | 0.8 | 4.5 | 2.0 | **15.8** |
| Ward from FH k₀ = 5 | 64 | 1024 | 52 | 3 | 2 113 | 0.8 | 6.2 | 3.9 | 0.9 | 0.9 | 3.9 | 2.1 | **19.1** |
| Ward from pixels | 64 | 1024 | 48 | 4 | 1 763 | 0.8 | 3.7 | 141.6 | 0.9 | 0.4 | 3.1 | 1.9 | **152** (loaded run) |
| FH, k = 2·min, `-O0` | 64 | 1024 | 50 | 2 | 1 761 | 0.8 | 66.4 | — | 11.5 | 3.8 | 13.1 | 11.7 | **108** |

All times in ms per Frame. Every run: Tessel areas summed to 76 800, 0 uncovered and 0 doubly covered pixels, identical meshes across all 15 repetitions. [measured]

Reference timings (single thread, loaded run): `ximgproc` FH(0.8, 300, 64) 23–24 ms; SLICO (8 px, 10 iterations) 41–43 ms; `pyrMeanShiftFiltering(8, 16)` 121–127 ms. [measured]

**Where the time goes.** The FH graph pass is about half of the total: 307 000 edges built with a `sqrt` each, bucket-sorted and run through union-find twice. 4-connectivity would halve it at some cost in diagonal boundaries (*estimate:* −2.5 ms). CDT and colouring scale with the number of vertices, so a small min with a small max is the expensive corner (15.8 ms). Including capture and drawing, 30 fps at 320×240 has room to spare on this CPU. [measured; estimate as marked]

**Not measured.** Temporal stability: FH, like the spline mode, recomputes from scratch each Frame, so Patch boundaries will flicker under camera noise. Whether that needs the cell-persistence idea of the stable mode is a question for the prototype. [derived]

## 5. Answers to the ticket

1. **Segmentation.** FH graph segmentation with `k = κ·min` (κ a fixed constant, about 2 ΔE in Lab) and `min_size = min`, written in `src/mosaic/`. Min area sets the tolerance through `τ(C) = κ·min/|C|` and the small-component merge. Ward region merging is the κ-free alternative with the nicest boundaries but weak control by min and a higher cost; SLIC(O) and mean-shift do not fit a min-area control and are slow; k-means + components adds nothing. `ximgproc` is on Ubuntu/Debian and in OF's Linux build, but not in OF's bundled OpenCV, and FH's reference code is GPL-2.
2. **Shared boundaries.** Crack edges on the pixel-corner lattice with the outside as label −1; chains between junctions (≥ 3 crack edges, Frame corners); DP once per chain with fixed ends; exact crossing check and ε-halving repair. The Frame border falls out as straight chains.
3. **Constrained triangulation.** artem-ogre/CDT 2.0.2, MPL-2.0, header-only, deterministic; one triangulation of the whole Frame with all chains as constraint edges; centroid rounds for `max tessel area` instead of its Ruppert refinement. Triangle is non-free for commercial use; poly2tri cannot take Patches with touching holes.
4. **Cost.** 11–16 ms per 320×240 Frame at `-O3`, about 110 ms at `-O0`, on an i7-11370H, single thread.

## Sources

- P. F. Felzenszwalb and D. P. Huttenlocher, "Efficient Graph-Based Image Segmentation", IJCV 59(2), 2004, cs.brown.edu/people/pfelzens/papers/seg-ijcv.pdf (§3 eq. 4–5, §5); reference code cs.brown.edu/people/pfelzens/segment/ (`segment-image.h`, `COPYING`, GPL-2).
- OpenCV contrib 4.10.0, `modules/ximgproc/src/graphsegmentation.cpp` (BSD-3 header) and `include/opencv2/ximgproc/slic.hpp`; local `/usr/include/opencv4/opencv2/ximgproc/segmentation.hpp`.
- OpenCV 4.10.0 `imgproc.hpp` (`pyrMeanShiftFiltering` documentation).
- R. Achanta et al., "SLIC Superpixels Compared to State-of-the-Art Superpixel Methods", IEEE TPAMI 34(11), 2012, as cited by `slic.hpp`; not re-read.
- D. Comaniciu and P. Meer, "Mean Shift: A Robust Approach Toward Feature Space Analysis", IEEE TPAMI 24(5), 2002; cited for attribution, not re-read.
- Ubuntu package metadata: `apt-cache depends libopencv-dev` → `libopencv-contrib-dev` (4.10.0+dfsg-7ubuntu5).
- openFrameworks 0.12: `addons/ofxOpenCv/addon_config.mk` (`linux64: ADDON_PKG_CONFIG_LIBRARIES = opencv4`, `ADDON_LIBS_EXCLUDE = libs/opencv/%`), local `addons/ofxOpenCv/libs/opencv/include/opencv2` (no `ximgproc`), `scripts/linux/ubuntu/install_dependencies.sh` (installs `libopencv-dev`).
- artem-ogre/CDT, tag 2.0.2 (`b4413ee`): `docs/README.md`, `CDT/include/Triangulation.h`, `Triangulation.hpp` (SplitMix64 PRNG), `LICENSE` (MPL-2.0).
- J. R. Shewchuk, Triangle 1.6, netlib.org/voronoi/triangle.zip: `README` (licence terms), `triangle.c` usage text (`-p`, `-a`, `-q`, `-Y`).
- poly2tri, github.com/jhasse/poly2tri master `4732e46`: `README.md`, `LICENSE` (BSD-3).
- D. H. Douglas and T. K. Peucker, "Algorithms for the reduction of the number of points required to represent a digitized line or its caricature", The Canadian Cartographer 10(2), 1973. A. Saalfeld, "Topologically consistent line simplification with the Douglas–Peucker algorithm", Cartography and GIS 26(1), 1999. M. de Berg, M. van Kreveld and S. Schirra, "Topologically correct subdivision simplification using the bandwidth criterion", Cartography and GIS 25(4), 1998. J. Ruppert, "A Delaunay refinement algorithm for quality 2-dimensional mesh generation", J. Algorithms 18(3), 1995. Cited for attribution only; not re-read.
- Earlier research on this map: `docs/research/incremental-delaunay.md` (branch `research/incremental-delaunay`, #14) and `docs/research/adaptive-triangle-subdivision.md` (branch `research/adaptive-subdivision`, #7).
