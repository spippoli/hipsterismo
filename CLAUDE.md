# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An openFrameworks app that turns a video file (`bin/data/fingers.mov`) or a webcam feed into low-poly "hipster" graphics. In the original modes each frame gets edge-detected, its contour points are resampled, Delaunay-triangulated, and each triangle is filled with the source colour around its centroid. In adaptive mode the frame is segmented into Patches of similar colour, and one constrained Delaunay triangulation follows their boundaries, so no triangle crosses two Patches. It can also turn a single image into a Mosaic image from the command line.

## Build and run

The project must live at `<OF_ROOT>/apps/myApps/hipsterismo`, because the `Makefile` sets `OF_ROOT` to `../../..`.

- Linux/macOS: `make` (or `make Debug` / `make Release`), then `make run`, or run the binary in `bin/` directly. The app loads assets from `bin/data/`.
- Still mode: `bin/hipsterismo [--mode spline|stable|adaptive] IMAGE [OUTPUT]` saves the Mosaic of `IMAGE` to `OUTPUT` (default `IMAGE-mosaic.png`, next to the input) and exits, without showing a window. `--mode` takes any name in `vertexModeNames`. Without it the vertex mode is `vertex_mode` from `bin/data/settings.xml`, or `MosaicMaker`'s default if the tag is missing. The other parameters always come from `settings.xml`. It renders at the image's own size, and still needs a display for the GL context. `-h`/`--help` prints the usage. Exit code: 0 on success, 1 if the image can't be loaded or saved, 2 on bad arguments (unknown mode or option, `--mode` without a value, missing image, extra arguments), with the error and the usage on stderr.
- IDE projects (Xcode, Visual Studio, VS Code) are not checked in, and `.gitignore` ignores them. Generate them with the openFrameworks projectGenerator: import this folder, and it reads `addons.make`.

Tests for the Mosaic module and the command-line parser live in `tests/`, a windowless OF app built with `ofxUnitTests`. Run them with `make -C tests && make -C tests run`; the process exit code is the number of failed tests. There is no way to run a single test; comment out calls in `run()` in `tests/src/main.cpp`. There is no linter.

### Addon dependencies

The app needs these addons in `<OF_ROOT>/addons/` (listed in `addons.make`): ofxCv, ofxDelaunay, ofxGui, ofxMSACore, ofxMSAInterpolator, ofxOpenCv, ofxXmlSettings. Only ofxGui, ofxOpenCv and ofxXmlSettings ship with openFrameworks. Clone the rest yourself:

```sh
cd <OF_ROOT>/addons
git clone https://github.com/kylemcdonald/ofxCv.git
git clone https://github.com/obviousjim/ofxDelaunay.git
git clone https://github.com/memo/ofxMSACore.git
git clone https://github.com/memo/ofxMSAInterpolator.git
git -C ofxDelaunay apply <path-to-this-app>/patches/ofxDelaunay-hull-triangles.patch
```

The patch fixes three bugs in ofxDelaunay's triangulator. Its supertriangle is too small, so flat triangles along the convex hull (the Frame border) are dropped and leave holes. Its in-circle test rounds a circumcentre, so a point on a circle through four or more half-pixel points (any axis-aligned rectangle's corners) can count as inside one of that circle's triangles and outside another; the new triangles then overlap (#39). The patch decides triangles of real points with the exact sign of the in-circle determinant (on the circle counts as inside), exact for half-pixel coordinates up to about 2600. And it compares a squared distance with the circumradius when retiring triangles; that one has not been seen to change a result with glibc's `qsort`, but it can with another tie order between equal-x points. The tests in `tests/` fail without the patch. Upstream is unmaintained and the code has no free licence, so the fix ships as a patch rather than as a fork or vendored copy.

### openFrameworks version

The app targets openFrameworks 0.12.x on every platform OF supports. It was originally written for OF 0.8 on macOS and has been built and run on Linux (OpenCV 4, C++20).

- `src/mosaic/Mosaic.h` wraps `#include "MSAInterpolator.h"` in a `#pragma GCC diagnostic` block, guarded by `__GNUC__` so it applies to GCC and Clang only. `InterpolatorT::sampleAt()` has no trailing return, and OF's `-Werror=return-type` would otherwise fail the build. OF adds that flag after the project flags, so it can't be overridden in `config.make`.
- ofxCv's `cvtColor`/`GaussianBlur` wrappers changed, so the app calls `cv::cvtColor` and `cv::GaussianBlur` directly.
- Expect a lot of `-Wdeprecated-enum-enum-conversion` warnings from the OpenCV 4 headers under C++20. They are harmless.

### Vendored CDT

Adaptive mode triangulates with [artem-ogre/CDT](https://github.com/artem-ogre/CDT) 2.0.2 (commit `b4413ee`), vendored header-only in `src/mosaic/CDT/`: the 11 headers of upstream's `CDT/include/`, its three licence files (MPL-2.0 for CDT, BSD-3 for `predicates.h`, Apache-2.0 with LLVM exception for `portable_nth_element.hpp`) and a README pinning the version. **Never edit these files**: a modified file would stay under the MPL and its source would have to be offered. To update, replace them all from a new upstream tag and update the README. OF's makefiles add every folder under `src/` to the include paths, so `#include "CDT.h"` works in the app and in the tests with no extra step; don't define `CDT_USE_AS_COMPILED_LIBRARY`. CDT compiles without warnings under OF's flags.

## Architecture

Domain terms (Frame, Contour, Patch, Tessel, Mosaic) are defined in `GLOSSARY.md`.

- **`src/mosaic/` is the Mosaic module**, and all image processing lives there. `MosaicMaker::make(const ofPixels&)` turns a Frame into a `Mosaic`: the Tessels plus the Contours they were built from. `toMesh()` turns a Mosaic into a flat-coloured `ofMesh`. In stable mode `make()` depends on the Frames seen so far, only through which cells are active (see below): the same sequence gives the same Mosaics, `reset()` starts over, and a change of Frame size, cell size or mode resets automatically. In spline and adaptive mode it is a pure function of the Frame. Grayscale and RGBA Frames are converted to RGB. The Tessels always tile the whole Frame rectangle (only an empty Frame gives an empty Mosaic); the tests check this by summing Tessel areas. The tests project compiles this folder directly, so keep the module self-contained in it.
- **Vertex modes.** The int parameter `vertex mode` (XML tag `vertex_mode`; key `s` cycles the modes) picks the pipeline. The values are the `VertexMode` enum in `Mosaic.h` (0 spline, 1 stable, 2 adaptive), and `vertexModeNames` holds their names in the same order, for the panel label, `--mode` and anything else that names a mode. Spline is still the default.
- **The spline and stable pipeline inside `make()`:**
  1. Convert to grayscale and blur repeatedly, with odd kernel sizes `1, 3, …, < blur iteration`.
  2. Run Canny, then `ofxCv::ContourFinder`.
  3. Choose vertices, by mode:
     - **Stable** (1): every `cell size` × `cell size` cell a Contour passes through gives one vertex, at a fixed pseudo-random offset inside the cell (hides the grid). A cell stays active for `persistence` Frames after its Contour left. This is what keeps vertices still under camera noise: the stability test on `tests/fixtures/fingers.png` expects ≥ 90% of vertices to stay put between noisy Frames. Tessels take the mean of the 5×5 pixels around their centroid.
     - **Spline** (0, default, the original look): all Contour points are concatenated into one `msa::Interpolator2D` spline, sampled `interpolation resolution` times (clamped to the Frame, since the spline overshoots where it bridges Contours). Vertices move with every bit of noise. Tessels take the centroid pixel. This mode must keep reproducing the pre-stable output exactly, except where the unpatched ofxDelaunay left holes or overlaps (#39 swapped the diagonal of one cocircular quad on `fingers.png`); the golden test hashes the spline Mosaic of `tests/fixtures/fingers.png` to catch any change.
  4. Add points along the Frame border (corners included), snap every point to a half-pixel grid without duplicates (the triangulation needs distinct points, and spline mode's look depends on the snapping), run Delaunay, drop zero-area triangles, then colour each Tessel as described for the mode.
- **The adaptive pipeline** (mode 2) lives in `src/mosaic/Adaptive.{h,cpp}`, called only by `make()`. It runs no Canny and no ContourFinder. Its fixed constants (blur σ 0.8, segmentation k 5, boundary ε 3 px) are named in `Adaptive.h`; they are absolute for now (#32 scales them with the Frame).
  1. Convert the Frame to Lab (OpenCV, float) and blur it with σ 0.8.
  2. Segment it into Patches: Felzenszwalb–Huttenlocher on the 8-connected pixel grid (k 5 in ΔE, min size 1, edges bucket-sorted so the order is deterministic) over-segments it. Neighbouring Patches whose mean colours in the unblurred Frame are exactly equal are joined: the blur turns a sharp edge into one-pixel strips that would otherwise pass `min patch area`, so two flat halves would give several Contours. Then Ward merging (cheapest `n1 n2 / (n1 + n2) |μ1 − μ2|²` first, on 4-neighbours) runs until every Patch has at least `min patch area` pixels. Above about 256 on `fingers.png` it stops changing: the hardest small Patch already reaches min.
  3. Trace the Patch boundaries as crack chains on the lattice of pixel corners, the outside of the Frame counting as one more Patch. Chains run between junctions (corners with three or more cracks, plus the four Frame corners); a junction-free loop is one closed chain. Points where a chain goes straight on are dropped, which keeps the geometry and the shared ends. These are the exact chains.
  4. Simplify each chain once with Douglas–Peucker at ε 3 px, its ends kept, so the two Patches either side of a chain see the same edge. A chain that ends where it starts (a loop, or a chain from a junction back to itself) is split at its point farthest from the start and keeps at least three distinct points. Then the crossing repair: two constraint segments may only meet at a shared end without overlapping, which an exact integer test checks on every pair of nearby segments (binned in 16 px cells; after the first round only pairs involving a chain changed in the last round). Each round, every chain with a conflicting segment, its own or another chain's, is simplified again at half its ε, and below 0.5 px it falls back to its exact chain with every corner, also for a closed loop. The rounds go on until no conflict remains, with no round cap: exact chains never conflict, so every round moves a chain down its finite ladder of ε (3, 1.5, 0.75, exact). The simplified chains are both the constraint edges and the Contours.
  5. Triangulate the whole Frame once with CDT, every simplified chain's segments as constraint edges (`IntersectingConstraintEdges::NotAllowed`, no Steiner points). The repair leaves no conflicts; should CDT still throw, the vertices are triangulated without constraints (still tiling the Frame) and a warning is logged, so `make()` never throws. The tests check that the warning is not logged on `fingers-060.png` and `fingers-003.png` (Frames 60 and 3, 0-based, of `fingers.mov`), where a repair that simplified closed loops (in the prototypes) or kept only an open chain's ends let CDT throw.
  6. Each Tessel takes the mean colour of the Frame pixels whose centre lies inside it (top-left rule on edges), or the pixel at its centroid if none.
  7. `mosaic.contours` holds the simplified chains off the Frame border: an open `ofPolyline` per chain between junctions, a closed one per loop. The `c` overlay shows them.

  On `fingers.png` (320×240) at `min patch area` 32 this gives 105 Patches, 513 Contours and 1 958 Tessels, in about 17 ms per Frame at -O3 (the exact chains gave 11 478 Tessels in 23 ms).
- **Parameters** are the `ofParameterGroup` in `MosaicMaker`. `ofApp` renames it to `panel`, so `bin/data/settings.xml` stays `<panel><first_threshold>…`. A tag's name is the parameter name with spaces replaced by underscores, so renaming a parameter breaks existing settings files: an old `stable_vertices` tag is now ignored.
  - **The panel shows only the active vertex mode's parameters.** `MosaicMaker::parametersFor(mode)` lists them (tested): `vertex mode` first, then the mode's own in group order. Spline hides `cell size` and `persistence`, stable hides `interpolation resolution`, and both hide `min patch area`. Adaptive shows only `vertex mode` and `min patch area` (8–1024 px², default 32, tag `min_patch_area`). Right after the `vertex mode` slider comes a read-only label with the mode's name; it is not serializable. The one `ofxPanel`, at (0,0), is rebuilt with `gui.clear()` + `gui.add()`: the `vertex mode` listener only sets `panelDirty`, and `update()` rebuilds, so the panel is never cleared during ofxGui's own event dispatch (a drag on the mode slider).
  - **Persistence** never goes through the panel's own `saveToFile`/`loadFromFile`, which only cover the parameters it shows. `ofApp::saveSettings()`/`loadSettings()` `ofSerialize`/`ofDeserialize` the whole group, hidden parameters included. Settings are loaded at startup (and again by `renderStill()`), and the panel header's save and load buttons call the same functions through `savePressedE`/`loadPressedE`, whose listeners return `true` to skip the default.
- **`src/cli/`** holds `parseCommandLine()`, a pure function (no OF) that turns the arguments into a `CommandLine`: window, help, still mode (image, output, and the `--mode` index if given) or an error with exit code 2. It takes the mode names as an argument; `main.cpp` passes `vertexModeNames`, so a new vertex mode is accepted by `--mode` once it is in that list.
- **The tests project** compiles every folder under `src/` (today `mosaic/` and `cli/`) but not `src/` itself, through `PROJECT_EXTERNAL_SOURCE_PATHS = ../src` plus `PROJECT_EXCLUSIONS = ../src` in `tests/config.make`. OF's makefile supports only one external source path, so don't list folders there; a new folder under `src/` must not depend on `ofApp` or ofxGui.
- **`main.cpp`** calls `parseCommandLine()`. On an error it prints the message and the usage and returns 2. For still mode it sets `ofApp::stillInput`/`stillOutput` (made absolute, because OF resolves relative paths against `bin/data`) and `stillMode`, and opens an invisible window. `ofApp::setup()` then calls `renderStill()`, which reads the parameters from `settings.xml` itself (`loadSettings()`), applies `stillMode` if set, draws the mesh into an `ofFbo`, saves it, and calls `ofExit()` with the exit code.
- **`ofApp`** only gets Frames, calls the Mosaic module, and draws, fitting the Frame in the window with its aspect ratio kept and centred (`OF_SCALEMODE_FIT`). Input selection is compile-time: uncomment `#define LIVE_INPUT` in `ofApp.h` to switch from `ofVideoPlayer` (playing `fingers.mov`) to the camera (320x240, mirrored horizontally). On Linux the camera is opened via `ofGstVideoUtils` with an explicit `v4l2src device=/dev/video0` pipeline, because OF 0.12.1's `ofVideoGrabber` picks the DMABuf caps that GStreamer ≥ 1.24 advertises and fails to link. Other platforms use `ofVideoGrabber` (device 0). The contour overlay draws `mosaic.contours`, and the Delaunay overlay draws the Mosaic mesh as a wireframe.

### Keyboard controls

| Key | Action |
|-----|--------|
| `f` | Toggle fullscreen |
| `g` | Toggle GUI and FPS display |
| `h` | Toggle between the polygon mesh and the raw source |
| `s` | Cycle `vertex mode`: spline (original) → stable (cells) → adaptive (Patches) → spline |
| `c` | Toggle contour overlay |
| `d` | Toggle Delaunay wireframe overlay |
| `p` | Save a screenshot to `bin/data/screenshot.png` |

## Agent skills

### Issue tracker

Issues live in this repo's GitHub Issues (`spippoli/hipsterismo`), via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

The five default labels: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Attribution

In this project, attribution lines use the clown emoji 🤡 in place of the robot emoji 🤖, in commit messages and in pull request descriptions alike: for example `🤡 Generated with [Claude Code](https://claude.com/claude-code)`.
