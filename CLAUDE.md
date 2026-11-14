# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An openFrameworks app that turns a video file (`bin/data/fingers.mov`) or a webcam feed into low-poly "hipster" graphics: each frame gets edge-detected, its contour points are resampled, Delaunay-triangulated, and each triangle is filled with the source pixel colour at its centroid. Still-image input is a planned feature (an earlier attempt was reverted in git history).

## Build and run

The project must live at `<OF_ROOT>/apps/myApps/hipsterismo`: both `Makefile` and `Project.xcconfig` set `OF_ROOT`/`OF_PATH` to `../../..`.

- `make` (or `make Debug` / `make Release`), then `make run`, or run the binary in `bin/` directly. The app loads assets from `bin/data/`.

There are no tests and no linter.

### Addon dependencies

The app needs these addons in `<OF_ROOT>/addons/` (listed in `addons.make`): ofxCv, ofxDelaunay, ofxGui, ofxMSACore, ofxMSAInterpolator, ofxOpenCv, ofxXmlSettings. Only ofxGui, ofxOpenCv and ofxXmlSettings ship with openFrameworks. Clone the rest yourself:

```sh
cd <OF_ROOT>/addons
git clone https://github.com/kylemcdonald/ofxCv.git
git clone https://github.com/obviousjim/ofxDelaunay.git
git clone https://github.com/memo/ofxMSACore.git
git clone https://github.com/memo/ofxMSAInterpolator.git
```

### openFrameworks version

The app targets openFrameworks 0.12.x on Linux (OpenCV 4, C++20). It was originally written for OF 0.8 on macOS. The Xcode project files are left over from that version and probably won't work with current OF.

- `ofApp.h` wraps `#include "MSAInterpolator.h"` in a `#pragma GCC diagnostic` block. `InterpolatorT::sampleAt()` has no trailing return, and OF's `-Werror=return-type` would otherwise fail the build. OF adds that flag after the project flags, so it can't be overridden in `config.make`.
- ofxCv's `cvtColor`/`GaussianBlur` wrappers changed, so the app calls `cv::cvtColor` and `cv::GaussianBlur` directly.
- Expect a lot of `-Wdeprecated-enum-enum-conversion` warnings from the OpenCV 4 headers under C++20. They are harmless.

## Architecture

All the logic is in `src/ofApp.{h,cpp}`.

- **Input selection is compile-time.** Uncomment `#define LIVE_INPUT` in `ofApp.h` to switch from `ofVideoPlayer` (playing `fingers.mov`) to `ofVideoGrabber` (device 0, 320x240, mirrored horizontally). New input sources (such as still images) need to fit into the same `#ifdef` branches in `ofApp.h`, `setup()` and `update()`.
- **Per-frame pipeline in `update()`** runs only when a new frame arrives:
  1. Convert to grayscale, then apply repeated Gaussian blurs with odd kernel sizes `1, 3, …, < max_it`.
  2. Run Canny with `threshold1`/`threshold2`, then `ofxCv::ContourFinder`.
  3. Push every contour point into a global `msa::Interpolator2D`, then sample it `resolution` times uniformly in `[0,1)`. This decouples triangle density from contour point count.
  4. Feed the samples to `ofxDelaunay`, triangulate, and rebuild `mesh` (`OF_PRIMITIVE_TRIANGLES`) with flat per-face colours sampled from `source` at each centroid.
- **Globals:** the `cv::Mat`s (`source`, `sourceBW`, `canny`) and the interpolator are file-scope globals in `ofApp.cpp`, not members.
- **Drawing:** `draw()` scales from source-image space (`source.cols` × `source.rows`) to the window size.
- **GUI/parameters:** `ofxPanel` holds the `ofParameter`s and loads/saves them from `bin/data/settings.xml`. The XML tags are the parameter names with spaces replaced by underscores. The `gradient` parameter is declared but never added to the panel.
- **Unused code:** the `tessel` struct and `geometric_center()` in `ofApp.h` are not used.

### Keyboard controls

| Key | Action |
|-----|--------|
| `f` | Toggle fullscreen |
| `g` | Toggle GUI and FPS display |
| `h` | Toggle between the polygon mesh and the raw source |
| `c` | Toggle contour overlay |
| `d` | Toggle Delaunay wireframe overlay |
| `p` | Save a screenshot to `bin/data/screenshot.png` |
