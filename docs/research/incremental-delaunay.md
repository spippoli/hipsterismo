# Incremental Delaunay and per-Frame cost of greedy insertion at 320×240

Research for [#14](https://github.com/spippoli/hipsterismo/issues/14), part of map [#6](https://github.com/spippoli/hipsterismo/issues/6).

**Question.** Adaptive mode inserts vertices one at a time (global greedy: triangles above `max tessel area` first, then highest error) into a Delaunay triangulation that starts from the 4 Frame corners, up to a cap of about 5000 vertices ([#9](https://github.com/spippoli/hipsterismo/issues/9)). Which incremental Delaunay should it use: `cv::Subdiv2D`, delaunator-cpp rebuilt in batches, ofxDelaunay, or a hand-written Bowyer–Watson / Lawson implementation? For each one: how to find the triangles an insertion changes, how it handles points on the border and near-duplicates, whether it is deterministic, and its licence. Can the whole loop reach 30 fps at the cap?

Claims are tagged **[src]** when the cited source states them, **[measured]** when they come from the benchmark below, and **[derived]** when they are my own reasoning. Figures marked *estimate* were not measured.

## TL;DR

1. **Write the Delaunay by hand: Bowyer–Watson on a triangle and neighbour array, about 150 lines, inside `src/mosaic/`.** It is the only candidate that met every requirement in the benchmark:
   - It tiled the whole Frame in every run, and every edge passed the empty-circumcircle test.
   - Point location costs O(1), because the candidate pixel lies in the triangle that was popped from the heap.
   - It returns exactly the triangles it deleted and created, so the rescan list and heap invalidation come for free.
   - It is deterministic, has no licence question, and its topology work was the fastest (2–3.4 ms per Frame).

   [measured; derived]
2. **Predicates can be exact without Shewchuk's adaptive arithmetic.** Snap every vertex to the half-pixel grid, as the app already does. With coordinates in multiples of ½ up to 320, every product in the orientation and incircle tests is a multiple of 1/16 and well below 2⁵³, so plain `double` arithmetic gives the exact sign. [derived]
3. **`cv::Subdiv2D` is not usable as shipped: it leaves holes along the Frame border.**
   - Its super-triangle sits only `3·max(w,h)` away. A point within about 1 px of the border makes a Frame edge non-Delaunay against a virtual vertex, so a sliver of Frame is left without a triangle.
   - In the greedy loop, 9–12% of the Frame area stayed uncovered.
   - Growing the rect by 100 000 px hid the holes in these runs, but nothing guarantees it.
   - It also exposes no list of deleted triangles, and its topology work was about 2× slower than the hand-written one.

   [src: OpenCV 4.10.0 `subdivision2d.cpp`; measured]
4. **ofxDelaunay is unusable for this, and it has a bug that probably affects the app today.**
   - It can only triangulate in batch. One triangulation of 5000 points takes 20–25 ms, against 1.5 ms for delaunator.
   - Its C++ port returns the circumradius `r` but keeps Bourke's `(xp−xc)² > r` early-out, which in the original C compares against `r²`. Triangles are therefore marked complete too early. This produced overlapping and missing triangles (up to 271 uncovered pixels and 16 doubly covered pixels) on point sets that are valid Delaunay input.

   [src: ofxDelaunay `Delaunay.cpp`, Bourke `triangulate.c`; measured]
5. **delaunator-cpp in batches is correct but changes the algorithm.**
   - A rebuild is cheap: about 1.5 ms at 5000 points.
   - Rebuilding after every insertion would take 5000 × 1.5 ms ≈ 7 s per Frame. Batches small enough to keep the order close to greedy (1–5% of the triangles per batch) cost 45–190 ms per Frame.
   - Batches of 20% run as fast as Bowyer–Watson, but they are no longer the "global greedy" order decided in #9.

   [measured]
6. **Budget: about 30 ms per Frame at the cap, on an i7-11370H with `-O3`.** That is right at the 33 ms that 30 fps allows, before capture and drawing.
   - Topology takes 2–3.5 ms. Rescanning triangles takes 15–23 ms, and the max-deviation pass is most of that.
   - An unoptimised Debug build (`-O0`) runs at 90–130 ms.
   - The main remaining lever belongs to the error-measure ticket #10: an error key computable from prefix sums would let the max-deviation scan run only for triangles that actually get a vertex. That is 0.9M pixels instead of 2.3M. *Estimate:* 10–15 ms per Frame.

   [measured; estimate as marked]
7. **#9's "no extra border points are needed" has a side effect.**
   - Colour candidates are pixel centres, so they are never on the border. In every run only the 4 corners stayed on the border, and each Frame side ended up as one 320 or 240 px edge under a 0.5 px-tall sliver Tessel.
   - Fix: snap candidates that lie in the outer pixel ring onto the border. This was measured as `BORDER_SNAP=1`: no extra cost, full coverage, valid Delaunay, 80–129 border vertices.

   [measured]

## 1. Candidates

### 1.1 `cv::Subdiv2D` (OpenCV 4.10.0, already linked)

- **Algorithm.**
  - It is a quad-edge structure (`QuadEdge::next[4]`, `pt[4]`).
  - `locate()` walks edges starting from `recentEdge`, the last edge it touched, using `isRightOf` tests.
  - `insert()` connects the new point to the vertices of the containing facet (or of the two facets, when the point lies on an edge). It then swaps suspect edges while `isPtInCircle3(...) < 0`.

  This is Guibas & Stolfi's incremental InsertSite on quad-edges, i.e. Lawson flips. [src: `subdivision2d.cpp` lines 276–490; Guibas & Stolfi, ACM TOG 4(2), 1985, not re-read]
- **Initialisation.** `initDelaunay(rect)` builds a super-triangle at `(x+3m, y)`, `(x, y+3m)` and `(x−3m, y−3m)`, where `m = max(width, height)`. Vertex 0 is a dummy and vertices 1–3 are virtual. [src: lines 492–536]
- **Rect bounds are half-open.** `locate()` raises `StsOutOfRange` when `pt.x >= bottomRight.x` or `pt.y >= bottomRight.y`. The corner `(W, H)` therefore needs `Rect(0, 0, W+1, H+1)`. `getTriangleList()` drops every triangle with a vertex outside the rect. [src: lines 287–288, 756–785]
- **Border holes.**
  - In a minimal check (`subdiv_hull.cpp`), the 4 corners plus `(160, 1)` give 3 triangles with area 76 640 instead of 76 800. The top edge has been flipped towards a virtual vertex.
  - At `(160, 20)`, or with the rect grown by 100 000 px, all 4 triangles are present.
  - In the greedy loop with the stock rect, uncovered area was 7 600–8 900 px (9–12%) in every scenario. With `SUBDIV_MARGIN=100000` coverage was complete in all three scenarios.

  [measured]

  The cause is that a finite super-triangle is inside the circumcircle of a thin border triangle, whose circumradius grows like `L²/(8h)`. A larger margin only moves the threshold, and with coordinates around 10⁵ the incircle test is no longer exact (it uses an absolute `eps = FLT_EPSILON/8`). [derived; src: line 402]
- **Changed triangles.**
  - After `insert()`, the new triangles are exactly the faces around the new vertex. Walk them with `getVertex(id, &e)`, `nextEdge` and `getEdge(e, NEXT_AROUND_LEFT)`, skipping faces that touch vertices 1–3.
  - The API does not report the deleted triangles. You have to keep your own triangle records and check each heap entry lazily ("does edge a→b still have c on its left?"). That costs O(degree) per pop, and 5 900–9 600 stale pops per Frame were measured.

  [src: header API; measured]
- **Duplicates.** `insert()` returns the existing id when the point lands on a vertex. The test is L1 distance `< FLT_EPSILON`, so it catches exact duplicates only. [src: lines 366–376, 425]
- **Determinism.** It uses no randomness, and the meshes were bit-identical across runs and across `-O0` and `-O3`. [src; measured]
- **Licence.** OpenCV 4.10.0 is Apache 2.0, and `subdivision2d.cpp` carries the older Intel BSD-style header. Either is fine. [src: OpenCV `LICENSE`, file header]

### 1.2 delaunator-cpp (`delfrrr/delaunator-cpp`, master `c1521f6`, MIT)

- **Algorithm.** It is a port of mapbox/delaunator: a sweep hull sorted by distance from a seed circumcentre, plus `legalize()` flips. It is batch only: the constructor triangulates `coords` and offers no insert. [src: README, `delaunator.hpp`]
- **Output.** It fills flat `triangles` and `halfedges` arrays. [src]
- **Duplicates.** It skips points within `EPSILON = DBL_EPSILON` of the previous point in sort order, and skips the seed points. It throws "not triangulation" on fully collinear input. [src: `delaunator.hpp` lines 149–154, 291, 346–354]
- **Border.** No coverage problem appeared with the collinear border points of `BORDER_SNAP`. [measured]
- **Changed triangles.** There is no direct report. You have to diff each rebuild against a cache keyed by vertex triple, and only triangles with new triples get rescanned. [measured design]
- **Cost.** One rebuild took 1.34–1.57 ms at 4 400–5 000 points. [measured] Its README claims about 1 ms for 2 000 uniform points on a 2.3 GHz CPU. [src]
- **Determinism.** It uses `std::sort` with no randomness, and its output was identical across runs. [measured]

### 1.3 ofxDelaunay (`obviousjim/ofxDelaunay` `d87a0b5`, already a dependency)

- **Algorithm.** Gilles Dumoulin's C++ port of Paul Bourke's batch Bowyer–Watson.
  - `triangulate()` sorts the points by x and adds a super-triangle at ±20·dmax.
  - For each point it linearly scans every triangle that is not yet "complete". It removes the triangles whose circumcircle contains the point (`drsqr <= rsqr`) and fans the new point to the unpaired edges.
  - `triangles` holds at most `3·nv` entries, and the loop silently stops at `4·nv`.

  There is no incremental API. [src: `ofxDelaunay.cpp`, `libs/Delaunay/src/Delaunay.cpp`]
- **Bug.**
  - The port's `CircumCircle()` returns `r = sqrt(rsqr)`, but `Triangulate()` still marks a triangle complete when `xc < xp && (xp−xc)² > r`.
  - In Bourke's original `triangulate.c`, that argument is `*rsqr`, the squared radius. The port's test fires too early whenever `r > 1`.
  - Triangles marked complete are never removed, so they end up overlapping the new ones.
  - Measured on the final 5 000-point sets: up to 271 uncovered pixels and 16 doubly covered pixels, and an area sum above the Frame area with `BORDER_SNAP` (76 832 against 76 800). The noise scenario was clean.

  [src: `Delaunay.cpp` `CircumCircle`, lines with `complete[j] = true`; Bourke `triangulate.c` lines 128–130, 223–272; measured]

  This bug probably also explains the overlapping triangles that CLAUDE.md attributes to near-coincident points in the current pipeline. That is worth its own ticket. [derived]
- **Cost.** 20–25 ms for one triangulation of 5 000 points. [measured]
- **Licence.** Neither the addon's GitHub repo nor Bourke's page states a licence. [src: GitHub API `license: null`; paulbourke.net/papers/triangulate]

### 1.4 Hand-written Bowyer–Watson (benchmark `bw`)

- **Data.** `P pts[]`, plus for each triangle `v[3]` (counter-clockwise), `n[3]` (`n[i]` is the neighbour opposite `v[i]`, or −1 on the hull) and `alive`. The start is the 4 corners, i.e. 2 triangles. [benchmark]
- **Insert(t, p).**
  1. Walk from `t` towards `p` with orientation tests (a visibility walk, which terminates on Delaunay triangulations). For greedy candidates `t` already contains `p`, so the walk does not move.
  2. Flood-fill the cavity: the triangles whose circumcircle strictly contains `p`.
  3. Fan `p` to the cavity boundary, linking each new triangle to its outer neighbour and to the other new triangles.
  4. When `p` lies on a hull edge, skip the zero-area face on that edge.

  This is Bowyer 1981 and Watson 1981 (Computer Journal 24(2)). Lawson flips give the same triangulation, apart from how ties between cocircular points are broken. [benchmark; references not re-read]
- **Changed triangles.** The cavity is the deleted set (`alive = false`), and the fan is the new set. Measured new triangles per insertion: mean 7.5, maximum 26 on fingers; mean 5.9, maximum 13 on noise. Each insertion deletes 2 fewer triangles than it creates in the interior, and 1 fewer on a hull edge. [measured; derived from Euler]
- **Robustness.** It uses exact predicates on the half-pixel grid (TL;DR 2). A cavity built with exact predicates is star-shaped from `p`, so the fan never overlaps. All runs showed 0 non-Delaunay edges, 0 uncovered pixels and 0 doubly covered pixels. [derived; measured]
- **Duplicates.** Keep a hash set of the vertices' half-grid coordinates, plus a bitmap of pixel centres already used as vertices. The max-deviation scan skips those pixels, so the same pixel is never picked twice. [benchmark]
- **Determinism.**
  - The heap is ordered by (class, key, triangle id), so ties always resolve the same way.
  - The jitter for max-area centroids is a hash of the 3 vertex ids.
  - Meshes were bit-identical across 15 repetitions, across `-O0` and `-O3`, and across the two scan modes.
  - Cocircular points are common on the pixel grid, so `bw` and `subdiv` legitimately produce different meshes.

  [measured]
- **Licence.** It is our own code.

## 2. Benchmark

Source: [`incremental-delaunay-bench/greedy_insertion_bench.cpp`](incremental-delaunay-bench/greedy_insertion_bench.cpp) and [`subdiv_hull.cpp`](incremental-delaunay-bench/subdiv_hull.cpp). The build line is in the file header. It needs `delaunator.hpp` from delaunator-cpp `c1521f6` and ofxDelaunay's `Delaunay.cpp`.

- **Machine.** Intel Core i7-11370H (4 cores / 8 threads, frequency not pinned), Linux 7.0, g++ 15.2.0, OpenCV 4.10.0, single thread.
- **Flags.** `-std=c++20 -O3`, the same as OF's Linux Release (`PLATFORM_OPTIMIZATION_CFLAGS_RELEASE = -O3`; Debug is `-g3`, i.e. `-O0`). [src: `config.linux.common.mk` lines 217, 228]
- **Timing.** Each figure is the median of 15 runs.
- **Workload.**
  - Frames: `tests/fixtures/fingers.png` (320×240), and seeded uniform RGB noise as the pathological case.
  - `max tessel area` 1024 px², `min tessel area` 4 px², cap 5000 vertices.
  - Error stand-in for #10: the maximum ΔE (CIE76, float Lab) from the triangle's mean. A triangle gets a colour vertex when that error exceeds `T`.
  - Per created triangle: the Lab mean of the pixels it owns (pixel-centre ownership with a top-left tie rule), then a pass to find the max-deviation pixel.
  - "span" mode computes the mean from per-row prefix sums over exact row spans. "bbox" mode tests every pixel of the bounding box, in two passes.
  - Lab conversion plus prefix sums cost 0.55–0.7 ms per Frame and are not included in the totals below.

### 2.1 Results (span scan, ms per Frame)

| Scenario | Method | Verts | Total | Topology | Scan | New tris/ins | Covered area |
|---|---|---|---|---|---|---|---|
| fingers T=8 | **bw** | 4580 | **28.4** | 3.0 | 21.2 | 7.52 | 76 800 ✓ |
| | subdiv | 4302 | 30.2 | 6.0 | 19.6 | 7.62 | 68 744 ✗ |
| | dlt-1% | 4621 | 185 | 101 | 23.0 | 7.36 | 76 800 ✓ |
| | dlt-5% | 4945 | 127 | 67 | 18.9 | 6.33 | 76 800 ✓ |
| | dlt-20% | 5000 | 105 | 56 | 14.7 | 5.03 | 76 800 ✓ |
| fingers T=0 (cap) | **bw** | 5000 | **31.5** | 3.4 | 22.8 | 7.60 | 76 800 ✓ |
| | subdiv | 5000 | 35.3 | 7.4 | 21.7 | 7.68 | 68 877 ✗ |
| | dlt-5% | 5000 | 50.7 | 17.6 | 18.7 | 6.10 | 76 800 ✓ |
| | dlt-20% | 5000 | 24.5 | 6.3 | 12.1 | 4.36 | 76 800 ✓ |
| noise T=0 (cap) | **bw** | 5000 | **22.3** | 2.2 | 16.6 | 5.92 | 76 800 ✓ |
| | subdiv | 5000 | 27.0 | 5.9 | 16.0 | 5.90 | 67 860 ✗ |
| | dlt-5% | 5000 | 45.5 | 16.9 | 15.0 | 5.43 | 76 800 ✓ |
| | dlt-20% | 5000 | 24.6 | 6.1 | 12.4 | 4.68 | 76 800 ✓ |

Notes on the table:

- "Total" also includes heap and hash-set operations and timer overhead, about 4–6 ms. [measured]
- For subdiv, the star walk after each insert counts in Total but not in Topology.
- Run to run, the same configuration varied by about ±1.5 ms.
- In dlt-K%, each batch inserts the top K% of triangles by priority, so its vertex order and final mesh differ from strict greedy. At fingers T=8, dlt-1% needed 282 rebuilds. [measured]
- The bbox scan was 3–4× slower: bw took 96–105 ms on fingers and 71 ms on noise. Row spans plus prefix sums are worth doing. [measured]
- With `-O0`: bw took 123–130 ms on fingers and 92 ms on noise. [measured]
- With `BORDER_SNAP=1`: bw took 25.0, 29.7 and 21.2 ms, with full coverage and 0 non-Delaunay edges. The longest border edge was 170 px on fingers (uniform regions) and 74 px on noise. [measured]
- With `SUBDIV_MARGIN=100000`: subdiv took 34.2, 37.3 and 25.6 ms, with full coverage. [measured]

### 2.2 Where the time goes (bw, fingers at the cap)

- **Rescans.** 37 980 triangles were scanned over 5 000 insertions. The max-deviation pass visited 2.31M pixels, 30× the Frame area. Greedy insertion revisits each pixel about once per level of refinement. [measured] This is the behaviour behind Garland & Heckbert's expected O((m+n) log m) bound. [src: Garland & Heckbert 1995, via the #7 research]
- **Lazy max-deviation (*estimate*).** The triangles that actually received a vertex owned only 0.93M pixels. With a heap key computed from prefix sums (SSE or σ from sum and sum-of-squares tables, see #7 §3), the max-deviation scan could run when a triangle is popped instead of when it is created. That would be about 2.5× fewer pixel visits, and the mean of each new triangle would stay O(rows). *Estimate:* total 10–15 ms per Frame. [derived from measured counters]
- **Topology.** At 3.4 ms, it is about 11% of the total. A faster Delaunay barely helps, so the choice between candidates should rest on correctness and on reporting the changed triangles. [measured]

## 3. Answers to the ticket

| | `cv::Subdiv2D` | delaunator-cpp (batches) | ofxDelaunay | hand-written Bowyer–Watson |
|---|---|---|---|---|
| Incremental | yes | no (rebuild) | no (rebuild, O(n·active)) | yes |
| Changed triangles | new: star of the vertex; deleted: not reported (lazy check) | diff against a triple cache | diff | cavity (deleted) + fan (new), exact |
| Point location | walk from the last edge | n/a | linear scan | O(1): the candidate's triangle |
| Frame border | **holes** (finite super-triangle); `W+1` rect needed for the corner | correct | **overlaps and holes** (complete-flag bug) | correct, including points on hull edges |
| Near-duplicates | L1 < FLT_EPSILON → same id | `DBL_EPSILON` skip | none | half-grid hash set + used-pixel bitmap |
| Determinism | yes | yes | yes | yes, with id tie-break |
| Licence | Apache 2.0 / BSD | MIT | none stated | ours |
| Topology, ms per Frame at the cap | 6–8 | 6–100 (by batch size) | 20–25 per rebuild | 2–3.4 |

**30 fps at the cap?** Only just, with `-O3` on this CPU: 22–31 ms against 33 ms. Capture, the Lab conversion (about 0.6 ms), `toMesh()` and drawing then leave no headroom on fingers. A realistic plan is about 30 ms on this machine, with the lazy max-deviation scan (an *estimated* 10–15 ms) as the way to get margin. A lower vertex cap is the fallback, since cost is roughly linear in the cap.

## Sources

- OpenCV 4.10.0, `modules/imgproc/include/opencv2/imgproc.hpp` (`class Subdiv2D`, local `/usr/include/opencv4`) and `modules/imgproc/src/subdivision2d.cpp` (github.com/opencv/opencv, tag 4.10.0); `LICENSE` (Apache 2.0).
- delaunator-cpp, github.com/delfrrr/delaunator-cpp, master `c1521f6`: `README.md`, `include/delaunator.hpp`, `LICENSE` (MIT).
- ofxDelaunay, github.com/obviousjim/ofxDelaunay `d87a0b5` (local `<OF_ROOT>/addons/ofxDelaunay`): `src/ofxDelaunay.cpp`, `libs/Delaunay/src/Delaunay.{h,cpp}`.
- P. Bourke, "Efficient Triangulation Algorithm Suitable for Terrain Modelling", paulbourke.net/papers/triangulate/ and `triangulate.c`.
- openFrameworks 0.12, `libs/openFrameworksCompiled/project/makefileCommon/config.linux.common.mk`.
- A. Bowyer, "Computing Dirichlet tessellations", and D. F. Watson, "Computing the n-dimensional Delaunay tessellation…", The Computer Journal 24(2), 1981. L. Guibas and J. Stolfi, "Primitives for the manipulation of general subdivisions and the computation of Voronoi diagrams", ACM TOG 4(2), 1985. These are cited for attribution only; I did not re-read them.
- M. Garland and P. Heckbert, "Fast Polygonal Approximation of Terrains and Height Fields", CMU-CS-95-181, 1995, as summarised in `docs/research/adaptive-triangle-subdivision.md` on `research/adaptive-subdivision`.
