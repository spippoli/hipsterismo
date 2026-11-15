#pragma once

// The adaptive vertex mode as a plain-C++ core, shared by the desktop app and
// the Android app. No openFrameworks, glm or OpenCV and no library: an RGB
// buffer goes in and Tessels and Contours come out, in plain types. Step 1
// (Lab and the blur) is in AdaptiveLab.h.
//
// On the desktop, MosaicMaker (src/mosaic/Mosaic.cpp) is a thin adapter:
// ofPixels in, the current Mosaic of glm/ofColor/ofPolyline out, and the log
// hook forwarded to ofLog. The core has its own tests in tests/core/, built
// with plain CMake.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mosaic::core {

// A view of 8-bit RGB pixels, row by row: pixel (x, y) is the three bytes at
// data + y * stride + 3 * x. `stride` is in bytes, at least 3 * width.
struct RgbImage {
    const uint8_t* data = nullptr;
    int width = 0, height = 0;
    int stride = 0;
};

struct Point {
    float x = 0, y = 0;
};

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

// One triangle of a Mosaic with its flat colour, in Frame pixel coordinates.
struct Tessel {
    std::array<Point, 3> triangle;
    Rgb color;
};

// A Patch boundary off the Frame border: open between two junctions, or a
// closed loop whose last point joins back to the first (not repeated).
struct Contour {
    std::vector<Point> points;
    bool closed = false;
};

struct Mosaic {
    std::vector<Tessel> tessels;
    std::vector<Contour> contours;
};

// The size constants, and the `min patch area` parameter, are measured on a
// reference Frame of 320x240 and scaled to the actual Frame by sizesFor(), so
// a still image gets the look of the app (#22).
constexpr int referenceWidth = 320, referenceHeight = 240;
constexpr float blurSigma = 0.8f; // Gaussian blur of the Lab Frame, in px at 320x240
constexpr double boundaryEpsilon = 3.0; // Douglas–Peucker tolerance of the Patch boundaries, in px at 320x240
// Felzenszwalb–Huttenlocher k, in ΔE (Lab units), min size 1. A colour
// difference, so it does not scale: it only seeds the over-segmentation,
// and the Ward merge to the scaled min patch area sets the Patch sizes.
constexpr float segmentationK = 5.0f;

// The pipeline's sizes for one Frame.
struct Sizes {
    int minPatchArea; // px², at least 1
    float blurSigma; // px
    double boundaryEpsilon; // px
};

// The sizes for a width x height Frame, from `minPatchArea` in px² at
// 320x240: the area scales with the Frame area (rounded to the nearest
// pixel, at least 1), σ and ε with the shorter side over 240. A 320x240
// Frame (or 240x320) gets the constants exactly.
Sizes sizesFor(int width, int height, int minPatchArea);

// The Patches of a Frame: a label per pixel, row by row, 0..count-1 in scan
// order. Steps 1 and 2 of makeAdaptive().
struct Segmentation {
    std::vector<int> label;
    int count = 0;
};

Segmentation segment(const RgbImage& frame, int minPatchArea);

// The Mosaic of an RGB Frame whose Tessels follow its Patches, with the
// sizes of sizesFor(width, height, minPatchArea):
//
// 1. Convert the Frame to Lab (exactly OpenCV's float conversion) and blur
//    it (σ) with deterministic fused multiply-adds.
// 2. Over-segment it with Felzenszwalb–Huttenlocher (segmentationK, min size 1)
//    on the 8-connected pixel grid. Join neighbouring Patches whose mean
//    colours in the unblurred Frame are exactly equal (strips the blur alone
//    split off a sharp edge). Then merge 4-neighbouring Patches, cheapest Ward
//    cost first, until every Patch has at least the scaled min patch area.
//    A Patch can still be pieces that touch only at a pixel corner: split
//    every Patch into its 4-connected pieces and absorb each piece under the
//    min into a neighbour, cheapest Ward cost first. Every Patch is then
//    4-connected and has at least the min (unless the Frame is smaller).
// 3. Trace the Patch boundaries along pixel edges ("cracks"), as chains
//    between junctions shared by the Patches on either side. The outside of
//    the Frame counts as a Patch, so the Frame border is traced too.
// 4. Simplify each chain once with Douglas–Peucker (ε), its
//    ends kept. Where simplified chains cross or touch, fall back to the exact
//    chains involved, every corner kept, until no conflict remains.
// 5. Triangulate the Frame rectangle once with CDT, the simplified chains as
//    constraint edges and no Steiner points.
// 6. Colour each Tessel with the mean of the Frame pixels whose centre lies
//    inside it (top-left rule on edges; the pixel at the centroid if none).
//
// The Contours are the chains not on the Frame border. A pure function of
// the Frame; an empty Frame gives an empty Mosaic. Never throws on a bad
// triangulation: it logs through the log hook and falls back (see
// setLogHook()).
Mosaic makeAdaptive(const RgbImage& frame, int minPatchArea);

// FNV-1a over the Tessels in order: each vertex's x and y as IEEE float bits
// (little-endian), then the colour's r, g, b bytes. The golden hashes of the
// tests are this hash.
uint64_t hash(const Mosaic& mosaic);

enum class LogLevel { Warning, Error };
using LogHook = std::function<void(LogLevel, const std::string& message)>;

// Where the core's warnings and errors go (today only the triangulation's
// fallbacks). By default they are written to stderr; an empty hook drops
// them. Not thread-safe: set it before calling makeAdaptive().
void setLogHook(LogHook hook);

} // namespace mosaic::core
