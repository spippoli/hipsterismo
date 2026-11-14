# Adaptive triangle subdivision for image approximation

Research for [#7](https://github.com/spippoli/hipsterismo/issues/7), part of map [#6](https://github.com/spippoli/hipsterismo/issues/6).

**Question.** Which techniques approximate an image with flat-coloured triangles by adaptive subdivision? For each one: how splits are chosen, how conformity and T-junctions are handled, which uniformity metrics are used, how a split threshold can depend on triangle area, and what a Frame costs. Is there reusable C++ that works with openFrameworks and OpenCV?

**Constraints from #6.** The Frame starts as a few large triangles, and they split where colour is not uniform. Splitting is bounded by `min tessel area` and `max tessel area` (pixel²), and the colour tolerance comes only from those two areas. A Tessel's colour is the mean of the pixels inside it. Adaptive mode must run at 30 fps or better on 320×240 Frames, in C++ with OpenCV 4.

Claims are tagged **[src]** when the cited source states them and **[derived]** when they are my own reasoning or arithmetic from sourced facts. Treat [derived] items as proposals.

## TL;DR

1. **Binary triangle tree (bintree) bisection fits the constraints best.** It is the scheme used by BTTC and ROAM, and it is newest-vertex or longest-edge bisection on right isosceles triangles.
   - Every split halves the area exactly. `min tessel area` and `max tessel area` therefore become a minimum and maximum tree depth.
   - The tree is simple to recurse top-down in a single pass.
   - ROAM's *forced split* keeps the mesh free of T-junctions.

   [src: Duchaineau 1997; Galić 2008 §3.1; derived]
2. **Make the split test a sum of squared errors (SSE), not a plain variance.** The test is: split when `n·σ² > T`.
   - This is the same as a standard-deviation tolerance that scales like `A^(-1/2)`. Galić et al. found a per-level threshold growth of `α ≈ 1.35–1.5` per bisection level, which matches that exponent, since `√2 ≈ 1.41` per halving of area.
   - SSE is also *monotone*: a child's SSE is never more than its parent's. That is the condition ROAM needs for its greedy split order to be optimal.

   [src: Galić 2008 §4.2; Duchaineau 1997 §5; derived]
3. **Per-triangle mean and variance are cheap.** Build one integral image (or per-row prefix sums) for each channel's sum and sum of squares. The sum over a triangle is then one O(1) span lookup per pixel row it covers.
   - The whole subdivision of a 320×240 Frame costs on the order of 10⁵ lookups, well under 1 ms. [src: OpenCV `cv::integral`; derived]
   - Pixel ownership follows the pixel-centre top-left rule, so every pixel belongs to exactly one Tessel. [src: Direct3D rasterization rules]
4. **Error-driven point insertion plus Delaunay (Garland–Heckbert greedy insertion) is the main alternative.**
   - It gives irregular, non-grid-aligned triangles, which are closer to the look of the existing modes.
   - It is more complex: it needs a heap of candidates, an incremental Delaunay, and a rescan of the triangles each insertion changes.
   - Its stopping rule is a point budget or an error threshold, so min/max area is not a natural control.

   [src: Garland & Heckbert 1995]
5. **Red-green 4-split works, but it is a poor fit.**
   - Green closure triangles must be undone before refining further. [src: Mitchell 2016]
   - Area quarters at each level, so min/max area gives coarser control. [derived]
6. **There is no ready-made C++ library for image bintree subdivision.** It is about 200 lines of code to write.
   - For the insertion route: `cv::Subdiv2D` (incremental Delaunay in OpenCV), ofxDelaunay (already a dependency) and delaunator-cpp (MIT licence).
   - Garland's Scape (C++, 1997) shows how to do greedy insertion, but its licence is not stated.

## 1. Split rules

### 1.1 Longest-edge bisection (Rivara)

- **Split:** connect the midpoint of the longest edge to the opposite vertex. [src: Mitchell 2016, "Other Triangle Bisection Methods"]
- **Refinement in two steps:**
  1. Bisect the triangles you want refined. This usually leaves hanging nodes.
  2. Run *closure*: more bisections until the mesh conforms.

  Rivara proved that closure takes a finite number of steps, and that longest-edge bisection produces only a finite number of similarity classes, so angles do not degenerate. Unlike red-green, the closure refinements never have to be undone. [src: Mitchell 2016; Rivara 1984, SIAM J. Numer. Anal. 21:604–613 and IJNME 20:745–756 (abstract: "always yield a new conforming triangulation after finitely many steps, even when applied locally")]
- **Area:** a median splits any triangle into two triangles of equal area, so each split halves the area. [derived]
- **Base mesh:** works on any initial mesh. Two triangles over an arbitrary W×H Frame are enough, which suits still mode where Frames can be any size.

### 1.2 Newest-vertex bisection and binary triangle trees (bintree, ROAM, BTTC)

- **Newest-vertex bisection:**
  - The vertex created by a split becomes the *peak* of both children, and the next split is on the edge opposite the peak.
  - Two triangles that share a refinement edge are *mates*.
  - To refine a triangle that is not compatibly divisible, first refine the triangle across its refinement edge, recursively. This recursion is the *compatibility chain*.
  - It terminates, because the generation drops by one at each step.
  - There are only 4 similarity classes (Sewell).

  [src: Mitchell 2016]
- **Bintree:** start from right isosceles triangles. Every split bisects the hypotenuse, and both children are again right isosceles. This is BTTC's scheme. Its vertices fall on integer coordinates for a (2ᵐ+1)² image, and recursion stops at legs of length 1. [src: Galić 2008 §3.1 describing Distasi 1997]
- **ROAM** works on the same bintree:
  - A *diamond* is a triangle T and its base neighbour T_B at the same level. A split of a diamond adds one vertex at the diamond's centre and keeps the mesh continuous.
  - If T_B is coarser, T_B must be force-split first, and that "may require further splits in a recursive sequence".
  - Any bintree triangulation can be reached from any other through splits and merges.

  [src: Duchaineau 1997 §4]
- **Mapping min/max area to depth:** each level halves the area, so depth k has area `A₀/2ᵏ`. That gives `k_min = ⌈log₂(A₀/A_max)⌉` (split unconditionally above this depth) and `k_max = ⌊log₂(A₀/A_min)⌋` (never split below it). [derived]
- **Forced splits and the min area:** a forced split only refines a *coarser* neighbour, up to the level of the triangle that triggered it. Forced splits therefore never break `min tessel area`. [derived from Duchaineau's level relations: base neighbours are at level ℓ or ℓ−1]
- **Base mesh at 320×240:**
  - gcd(320, 240) = 80, so a 4×3 grid of 80×80 squares, each cut into two right isosceles triangles, gives 24 roots of area 3200 px². All later vertices are exact dyadic fractions.
  - If `max tessel area` > 3200, a coarser root does not tile a 4:3 Frame with isosceles triangles. Either clamp, or use two 4:3 root triangles with longest-edge bisection (§1.1). A 3-4-5 right triangle's first bisection gives isosceles children (200, 200, 320 and 200, 200, 240), which are not similar to the parent.

  [derived]

### 1.3 Red-green refinement (Bank & Sherman)

- **Red refinement:** split into 4 triangles by joining the edge midpoints.
- **Green refinement:** bisect from a vertex to the midpoint of the opposite edge, to remove hanging nodes.
- **Cycle:**
  1. Red-refine the marked triangles.
  2. Remove the hanging nodes with green refinements.
  3. Remove the greens before the next refinement pass.

  Newest-vertex bisection was invented to avoid that green refinement and unrefinement. [src: Mitchell 2016, citing Bank & Sherman, Computing 26:91–105, 1981]
- **Area:** each red level quarters the area, so min/max area quantise to powers of 4. [derived]
- **Verdict:** strictly more bookkeeping than the bintree, for no gain here.

### 1.4 Error-driven point insertion plus Delaunay (greedy insertion)

- **Algorithm** (Garland & Heckbert 1995):
  1. Start with the corner points.
  2. Repeatedly insert the input point with the highest local error, then update the Delaunay triangulation, or a data-dependent triangulation, which gives slightly better quality.

  [src: Garland & Heckbert §4]
- **Error measure:** the *local* L∞ measure, the largest error at any point. The authors tested global and curvature-based measures and found them worse. [src: Garland & Heckbert §3]
- **Fast variant ("Algorithm III"):**
  - Each triangle keeps at most one candidate point, the highest-error pixel inside it, found while scan-converting the triangle.
  - Candidates sit in a heap keyed on error.
  - After an insertion, only the triangles that changed are rescanned.
  - Expected cost is O((m+n) log m) for n input points and m inserted points.

  [src: Garland & Heckbert §4, abstract]
- **Colour:** for colour images, the error is extended as `(1−w)|Δz| + w·M(colour difference)`, and the paper shows Gouraud-shaded colour image approximations. [src: Garland & Heckbert §6, colour figures 23–25]
- **Other image-specific uses:**
  - Davoine et al. 1996 build an adaptive Delaunay partition "computed on an initial set of points obtained with a split and merge algorithm" that depends on grey levels. [src: abstract]
  - Gevers & Smeulders 1997 drive the splitting with an incremental Delaunay "guided" by pixel similarity and difference, so triangle edges follow image edges. [src: abstract; this matters for the #6 open item on using Contours to guide subdivision]
- **Conformity:** Delaunay output always conforms, so T-junctions cannot occur.
- **Fit to min/max area:** poor. There is no direct area control; you would have to stop inserting into triangles below `A_min` and force insertions into triangles above `A_max`. [derived]

## 2. Conformity and T-junctions

| Scheme | Conformity | Mechanism |
|---|---|---|
| BTTC (original) | not conforming | none. For a pixel on a shared side, "it is sufficient … to split one of the two adjacent triangles" [src: Galić §3.1] |
| ROAM bintree | conforming | recursive forced split of the base neighbour [src: Duchaineau §4] |
| Newest-vertex bisection | conforming | compatibility chain [src: Mitchell] |
| Rivara longest-edge bisection | conforming | closure bisections, finite, never undone [src: Mitchell; Rivara] |
| Red-green | conforming | temporary green closure [src: Mitchell] |
| Delaunay insertion | conforming | by construction |

**What T-junctions cost this app** [derived]:

- **Coverage:** with flat colours, a hanging vertex that lies *exactly* on its neighbour's edge leaves no geometric gap.
  - Bintree midpoints of integer or dyadic coordinates are exact in `float`, so GPU coverage stays watertight.
  - Rasterizers count a pixel once when its centre is inside, using the top-left rule for centres that lie on an edge. "The top-left rule ensures that adjacent triangles are drawn once." [src: Direct3D rasterization rules]
- **Why to conform anyway:**
  - The `d` wireframe overlay looks wrong with T-junctions.
  - A conforming mesh is what `toMesh()` and any later smoothing or merging steps expect.
  - Splitting the neighbours avoids large flat-coloured triangles sitting next to many small ones.
  - With ROAM forced splits the extra cost is small: a few triangles beyond what the colour test asked for.

## 3. Uniformity metrics

- **L∞ (maximum deviation).** Used by BTTC (`e ≤ ε` at every pixel) and by Garland & Heckbert, who measure error against an interpolated surface, not a flat mean. [src]
  - It is sensitive to noise: one noisy pixel forces a split.
  - It needs a pass over every pixel of every visited triangle, because it cannot be computed from prefix sums. [derived]
- **Variance and SSE.** Variance is `σ² = E[x²] − E[x]²` per channel. Summed over channels, it is the mean squared Euclidean distance to the mean colour. SSE is `n·σ²`. [derived]
  - Both come in O(1) per row span from sum and sum-of-squares tables, as the OpenCV docs state for "sum, mean, and standard deviation" over rectangles. [src: `cv::integral` doc]
  - SSE is monotone, because the children partition the parent and each child's mean minimises its own SSE: `SSE(c₁) + SSE(c₂) ≤ SSE(p)`. That is ROAM's optimality requirement: "a child's priority is not larger than its parent's". Plain σ² is *not* monotone. [src: Duchaineau §5; derived]
- **RGB vs CIE L\*a\*b\*:**
  - `cv::cvtColor(…, COLOR_RGB2Lab)` on 8-bit input gives `L·255/100`, `a+128`, `b+128`. On float input it gives L in 0–100 and a, b in about ±127. [src: OpenCV colors doc]
  - Euclidean distance in Lab (ΔE\*ab, CIE76) is closer to perceived difference than distance in RGB. A common rule of thumb puts a just-noticeable difference at about ΔE 2.3 (often credited to Mahy et al. 1994; I did not check that source).
  - Cost: one `cvtColor` per Frame, then integral images of the Lab channels for the split test. Keep RGB sums as well, because the Tessel colour is the mean of the *RGB* pixels. The mean of Lab values converted back to RGB is not the RGB mean, since the conversion is not linear. [derived]
  - Use float Lab (CV_32F input), or accept the 8-bit quantisation of the channels. [derived]

## 4. Area-dependent thresholds

- **Galić et al. (BTTC with an "adaptive error threshold"):**
  - "Since splitting a large triangle affects many more pixels than splitting a small one", use a stricter threshold on coarse levels.
  - The threshold is `ε_k = αᵏ·ε₀`, with α "typically, in the range between 1.35 and 1.5" per bisection level.
  - It improved the reconstructions they measured.

  [src: Galić 2008 §4.2, Fig. 6]
- **The same rule in terms of area** [derived]:
  - Area halves at each level, so `ε(A) = ε₀·(A₀/A)^(log₂ α)`. The exponent is 0.43–0.58, which is about `A^(-1/2)`.
  - A fixed SSE budget, splitting when `n·σ² > T` with `n ≈ A`, is the same as `σ_thr = √(T/A)`, which is exactly `A^(-1/2)`. The SSE test therefore reproduces Galić's empirically good schedule and is also monotone (§3).
- **Deriving T from the two areas only** [derived; the choice of constant is open for the spec]:
  - Above `A_max`: always split, whatever the colour.
  - At or below `A_min`: never split.
  - In between: split when `SSE > T`.
  - T needs one absolute colour scale c (for example a ΔE of a few units, or a fixed fraction of 255), because areas carry no colour units. One way to tie it to the areas: `T = c²·A_max`. Then a triangle just below `A_max` splits at σ > c, and one at `A_min` would need σ > c·√(A_max/A_min). The ratio `A_max/A_min` sets the dynamic range, and c stays a hidden constant.
  - Any parametrisation still needs some colour constant. The spec should fix it as a code constant, not a parameter, so the "two areas only" rule holds.

## 5. Fast per-triangle mean and variance

- **Summed-area tables** (Crow, SIGGRAPH 1984) give O(1) sums over axis-aligned rectangles.
- **OpenCV `cv::integral(src, sum, sqsum)`** gives (W+1)×(H+1) tables with `sum(X,Y) = Σ_{x<X, y<Y} image(x,y)`. Channels accumulate independently. `sqsum` is CV_64F; `sum` can be CV_32S, CV_32F or CV_64F. A third output, `tilted`, sums over 45°-rotated rectangles. [src: `opencv2/imgproc.hpp`, OpenCV 4.10]
- **Triangles: a row-span lookup** [derived]:
  - For each pixel row y the triangle covers, compute the span `[x₀, x₁)` of pixel centres inside it, using the top-left rule so neighbouring Tessels share no pixel.
  - The span's sum is `(S[y+1][x₁] − S[y][x₁]) − (S[y+1][x₀] − S[y][x₀])`, or a simpler per-row prefix sum, which uses half the memory.
  - That is O(height) per triangle, with no per-pixel work.
- **Cost for one 320×240 Frame** [derived, estimate]:
  - `cvtColor` to Lab, plus integrals of 3 channels' sums and squares: about 4·76,800 pixel operations, roughly 0.1–0.3 ms.
  - Top-down recursion: depth k visits about `WH/A_k` triangles of height about `√A_k`, so about `WH/√A_k` row lookups. The geometric sum is dominated by the finest level. For `A_min = 8`: about 76,800/2.8 × 3.4 ≈ 10⁵ row lookups × 6 channel tables, under 1 ms.
  - The worst case is about `WH/A_min` Tessels: 9,600 at `A_min = 8`, which is easy to draw at 30 fps.
  - Brute-force per-pixel accumulation (scan-convert each visited triangle) touches each pixel once per level: about 13 levels × 76,800 ≈ 10⁶ pixel reads. That probably still fits at 30 fps, but the prefix-sum method is about 10× cheaper and is the only one that makes L∞ the expensive choice.
- **Zero-pixel triangles:** a triangle whose area is near 1 can contain no pixel centre. Fall back to sampling the pixel at its centroid, or forbid `A_min < 2`. [derived]
- **Precision:** the 8-bit sum of squares over a whole Frame (255² × 76,800 ≈ 5·10⁹) overflows int32. Use CV_64F, as `sqsum` already does, or compute σ² from double sums. `E[x²]−E[x]²` cancellation is harmless in double precision at these magnitudes. [derived]

## 6. Cost per technique (summary)

| Technique | Per-Frame work at 320×240 | Complexity to implement |
|---|---|---|
| Bintree plus forced splits plus SSE from prefix sums | well under 1 ms of CPU [derived] | low: a tree or recursion, neighbour links for forced splits |
| Rivara LEPP plus SSE | similar [derived] | medium: general triangles, longest-edge propagation path |
| Red-green | similar | medium–high: green undo |
| Greedy insertion plus Delaunay (Garland III) | O((m+n) log m) expected [src]; ms-range likely for m of a few thousand [derived, unmeasured] | high: heap, incremental Delaunay, rescans |

**Temporal note** (input for the #6 open item on video stability): ROAM's second, *merge* queue lets the greedy algorithm "start from a previous optimal triangulation when the priorities have changed, and thus take advantage of frame-to-frame coherence". Its split and merge operations can also be animated with vertex morphing. [src: Duchaineau §4–5]

## 7. Reusable code

- **OpenCV 4 (already linked):**
  - `cv::integral` and `cv::cvtColor`.
  - `cv::Subdiv2D`: incremental Delaunay with `initDelaunay(rect)`, `insert(Point2f)`, `locate()` and `getTriangleList()`. Inserting a point outside the rect raises an error; a duplicate point is ignored. [src: `imgproc.hpp`] It suits the greedy-insertion route.
- **ofxDelaunay:** already used by spline and stable modes. It is batch-only (no incremental insert), so it does not suit greedy insertion. [from this repo]
- **delaunator-cpp:** MIT licence, a port of Mapbox Delaunator. Input is flat `std::vector<double>` coordinates; output is `triangles` as index triples. Its own benchmark gives about 63 ms for 100k points. [src: README]
- **Garland's Scape 1.2 (1997, C++, with `quadedge.C`) and Terra 0.7:** reference implementations of greedy insertion for height fields. The Scape page states no licence. Terra's page says "You're free to use it, but you use it at your own risk." Read them, don't vendor them. [src: cs.cmu.edu/~garland/scape]
- **Bintree for images:** I found no maintained C++ library. ROAM and BTTC are small enough to write from the papers: a node with 3 vertex indices, 2 children, and left, right and base neighbours (Duchaineau §4).

## Sources

- M. Garland, P. Heckbert. *Fast Polygonal Approximation of Terrains and Height Fields*. CMU-CS-95-181, 1995. <https://www.cs.cmu.edu/~garland/scape/scape.pdf> (read in full)
- M. Duchaineau et al. *ROAMing Terrain: Real-time Optimally Adapting Meshes*. IEEE Visualization 1997. <https://www.osti.gov/servlets/purl/632827> (read §4–5)
- W. F. Mitchell. *30 Years of Newest Vertex Bisection*. J. Numer. Anal. Ind. Appl. Math. (ICNAAM 2015). <https://math.nist.gov/~WMitchell/papers/wfmICNAAM15full.pdf> (read; source for red-green, NVB and Rivara)
- M. C. Rivara. *Algorithms for refining triangular grids suitable for adaptive and multigrid techniques*. IJNME 20(4):745–756, 1984. doi:10.1002/nme.1620200412 (abstract only)
- R. Distasi, M. Nappi, S. Vitulano. *Image compression by B-tree triangular coding*. IEEE Trans. Commun. 45(9):1095–1100, 1997. doi:10.1109/26.623074 (not read; algorithm taken from Galić's description)
- I. Galić, J. Weickert, M. Welk et al. *Image Compression with Anisotropic Diffusion*. J. Math. Imaging Vis., 2008. Preprint: <https://www.mia.uni-saarland.de/Publications/galic-pp203.pdf> (read §3–4)
- F. Davoine, M. Antonini, J.-M. Chassery, M. Barlaud. *Fractal image compression based on Delaunay triangulation and vector quantization*. IEEE TIP 5(2):338–346, 1996. <https://uni-konstanz.de/mmsp/fractal2/html/Publi/DaAnChBa96.html> (abstract only)
- T. Gevers, A. Smeulders. *Combining Region Splitting and Edge Detection Through Guided Delaunay Image Subdivision*. CVPR 1997. <https://mlanthology.org/cvpr/1997/gevers1997cvpr-combining> (abstract only)
- F. Crow. *Summed-area tables for texture mapping*. SIGGRAPH 1984 (cited from memory, not re-read)
- OpenCV 4.10 `opencv2/imgproc.hpp` (`cv::integral`, `cv::Subdiv2D`), and the colour conversion doc <https://github.com/opencv/opencv/blob/4.x/modules/imgproc/doc/colors.markdown>
- Microsoft. *Rasterization Rules (Direct3D 11)*. <https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules>
- delaunator-cpp. <https://github.com/delfrrr/delaunator-cpp>
