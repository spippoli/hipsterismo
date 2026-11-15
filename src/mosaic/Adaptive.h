#pragma once

// Adaptive vertex mode, internal to the Mosaic module: MosaicMaker::make()
// calls it, nothing else should.

#include "Mosaic.h"

namespace adaptive {

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

// The Mosaic of an RGB Frame whose Tessels follow its Patches, with the
// sizes of sizesFor(width, height, minPatchArea):
//
// 1. Convert the Frame to Lab and blur it (σ).
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
// The Contours are the chains not on the Frame border: open between two
// junctions, closed for a junction-free loop. A pure function of the Frame.
Mosaic make(const ofPixels& rgb, int minPatchArea);

} // namespace adaptive
