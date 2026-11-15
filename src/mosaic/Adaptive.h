#pragma once

// Adaptive vertex mode, internal to the Mosaic module: MosaicMaker::make()
// calls it, nothing else should.

#include "Mosaic.h"

namespace adaptive {

// Fixed pipeline constants, absolute for now; #32 will scale them by Frame size.
constexpr float blurSigma = 0.8f; // Gaussian blur of the Lab Frame, in px
constexpr float segmentationK = 5.0f; // Felzenszwalb–Huttenlocher k, in ΔE (Lab units); min size 1

// The Mosaic of an RGB Frame whose Tessels follow its Patches:
//
// 1. Convert the Frame to Lab and blur it (blurSigma).
// 2. Over-segment it with Felzenszwalb–Huttenlocher (segmentationK, min size 1)
//    on the 8-connected pixel grid. Join neighbouring Patches whose mean
//    colours in the unblurred Frame are exactly equal (strips the blur alone
//    split off a sharp edge). Then merge 4-neighbouring Patches, cheapest Ward
//    cost first, until every Patch has at least `minPatchArea` pixels.
// 3. Trace the Patch boundaries along pixel edges ("cracks"), as chains
//    between junctions shared by the Patches on either side. The outside of
//    the Frame counts as a Patch, so the Frame border is traced too.
// 4. Triangulate the Frame rectangle once with CDT, the exact chains as
//    constraint edges and no Steiner points.
// 5. Colour each Tessel with the mean of the Frame pixels whose centre lies
//    inside it (top-left rule on edges; the pixel at the centroid if none).
//
// The Contours are the chains not on the Frame border: open between two
// junctions, closed for a junction-free loop. A pure function of the Frame.
Mosaic make(const ofPixels& rgb, int minPatchArea);

} // namespace adaptive
