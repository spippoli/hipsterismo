#pragma once

// Adaptive vertex mode on the desktop. The pipeline itself is the
// openFrameworks-free core in core/AdaptiveCore.h (see there for its steps);
// MosaicMaker::make() is the adapter that converts ofPixels in and the
// Mosaic out. These are the core's size names under the desktop's namespace.

#include "AdaptiveCore.h"

namespace adaptive {

using mosaic::core::blurSigma;
using mosaic::core::boundaryEpsilon;
using mosaic::core::referenceHeight;
using mosaic::core::referenceWidth;
using mosaic::core::segmentationK;
using mosaic::core::Sizes;
using mosaic::core::sizesFor;

} // namespace adaptive
