# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An openFrameworks app that turns a video file (`bin/data/fingers.mov`) or a webcam feed into low-poly "hipster" graphics: each frame gets edge-detected, its contour points are resampled, Delaunay-triangulated, and each triangle is filled with the source pixel colour at its centroid. Still-image input is a planned feature (an earlier attempt was reverted in git history).

## Build and run

The project must live at `<OF_ROOT>/apps/myApps/hipsterismo`, because the `Makefile` sets `OF_ROOT` to `../../..`.

- Linux/macOS: `make` (or `make Debug` / `make Release`), then `make run`, or run the binary in `bin/` directly. The app loads assets from `bin/data/`.
- IDE projects (Xcode, Visual Studio, VS Code) are not checked in, and `.gitignore` ignores them. Generate them with the openFrameworks projectGenerator: import this folder, and it reads `addons.make`.

Tests for the Mosaic module live in `tests/`, a windowless OF app built with `ofxUnitTests`. Run them with `make -C tests && make -C tests run`; the process exit code is the number of failed tests. There is no way to run a single test; comment out calls in `run()` in `tests/src/main.cpp`. There is no linter.

### Addon dependencies

The app needs these addons in `<OF_ROOT>/addons/` (listed in `addons.make`): ofxCv, ofxDelaunay, ofxGui, ofxOpenCv, ofxXmlSettings. Only ofxGui, ofxOpenCv and ofxXmlSettings ship with openFrameworks. Clone the rest yourself:

```sh
cd <OF_ROOT>/addons
git clone https://github.com/kylemcdonald/ofxCv.git
git clone https://github.com/obviousjim/ofxDelaunay.git
```

### openFrameworks version

The app targets openFrameworks 0.12.x on every platform OF supports. It was originally written for OF 0.8 on macOS and has been built and run on Linux (OpenCV 4, C++20).

- ofxCv's `cvtColor`/`GaussianBlur` wrappers changed, so the app calls `cv::cvtColor` and `cv::GaussianBlur` directly.
- Expect a lot of `-Wdeprecated-enum-enum-conversion` warnings from the OpenCV 4 headers under C++20. They are harmless.

## Architecture

Domain terms (Frame, Contour, Tessel, Mosaic) are defined in `GLOSSARY.md`.

- **`src/mosaic/` is the Mosaic module**, and all image processing lives there. `MosaicMaker::make(const ofPixels&)` turns a Frame into a `Mosaic`: the Tessels plus the Contours they were built from. `toMesh()` turns a Mosaic into a flat-coloured `ofMesh`. `make()` depends on the Frames seen so far, only through which cells are active (see below): the same sequence gives the same Mosaics, `reset()` starts over, and a change of Frame or cell size resets automatically. Grayscale and RGBA Frames are converted to RGB. The Tessels always tile the whole Frame rectangle (only an empty Frame gives an empty Mosaic); the tests check this by summing Tessel areas. The tests project compiles this folder directly through `PROJECT_EXTERNAL_SOURCE_PATHS`, so keep the module self-contained in it.
- **The pipeline inside `make()`:**
  1. Convert to grayscale and blur repeatedly, with odd kernel sizes `1, 3, …, < blur iteration`.
  2. Run Canny, then `ofxCv::ContourFinder`.
  3. Every `cell size` × `cell size` cell a Contour passes through gives one vertex, at a fixed pseudo-random offset inside the cell (hides the grid). A cell stays active for `persistence` Frames after its Contour left. This is what keeps vertices still under camera noise: the stability test on `tests/fixtures/fingers.png` expects ≥ 90% of vertices to stay put between noisy Frames.
  4. Add points along the Frame border (corners included), snap every point to a half-pixel grid without duplicates (ofxDelaunay produces overlapping triangles on near-coincident points), run Delaunay, drop zero-area triangles, then colour each Tessel with the mean of the 5×5 pixels around its centroid.
- **Parameters** are the `ofParameterGroup` in `MosaicMaker`. `ofApp` renames it to `panel` and builds the `ofxPanel` from it, so the XML in `bin/data/settings.xml` stays `<panel><first_threshold>…`. A tag's name is the parameter name with spaces replaced by underscores, so renaming a parameter breaks existing settings files.
- **`ofApp`** only gets Frames, calls the Mosaic module, and draws, fitting the Frame in the window with its aspect ratio kept and centred (`OF_SCALEMODE_FIT`). Input selection is compile-time: uncomment `#define LIVE_INPUT` in `ofApp.h` to switch from `ofVideoPlayer` (playing `fingers.mov`) to the camera (320x240, mirrored horizontally). On Linux the camera is opened via `ofGstVideoUtils` with an explicit `v4l2src device=/dev/video0` pipeline, because OF 0.12.1's `ofVideoGrabber` picks the DMABuf caps that GStreamer ≥ 1.24 advertises and fails to link. Other platforms use `ofVideoGrabber` (device 0). The contour overlay draws `mosaic.contours`, and the Delaunay overlay draws the Mosaic mesh as a wireframe.

### Keyboard controls

| Key | Action |
|-----|--------|
| `f` | Toggle fullscreen |
| `g` | Toggle GUI and FPS display |
| `h` | Toggle between the polygon mesh and the raw source |
| `c` | Toggle contour overlay |
| `d` | Toggle Delaunay wireframe overlay |
| `p` | Save a screenshot to `bin/data/screenshot.png` |
