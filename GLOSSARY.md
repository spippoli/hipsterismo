# hipsterismo

Turns moving or still images into low-poly graphics made of flat-coloured triangles.

## Language

### Images

**Frame**:
A single image fed to the app, whether it comes from a video, a camera or a still picture.
_Avoid_: source, input, image (when the role is meant)

**Photo**:
A picture the user takes with the shutter or picks from the gallery, at its own full resolution; the app makes one Frame from it, upright and possibly smaller.
_Avoid_: shot, picture, still

### Building a Mosaic

**Contour**:
An outline found in a Frame, either along an edge or between two Patches; the Tessels are built from points along the Contours.
_Avoid_: edge, outline, blob

**Patch**:
A connected region of a Frame whose pixels have similar colour; the Tessels cover it without crossing its boundary.
_Avoid_: region, segment, superpixel, blob

**Grain**:
How coarse the Patches are: the smallest area a Patch may have, measured as if the Frame were 320×240, so one Grain gives the same look at any Frame size.
_Avoid_: min patch area (outside the code), coarseness, detail, granularity

**Tessel**:
One triangle of a Mosaic together with its single flat colour, taken from the Frame where the triangle lies.
_Avoid_: face, triangle (when the colour is part of the meaning), tile

**Mosaic**:
The set of Tessels produced from one Frame, tiling the whole Frame rectangle, together with the Contours they were built from, if any; what the app shows in its "hipster" view.
_Avoid_: hipster, mesh, polygonization, low-poly

### Showing Mosaics

**Preview**:
The continuous stream of Mosaics made from the camera's Frames as they arrive, shown live and never kept.
_Avoid_: live view, live Mosaic, stream

**Result**:
The single Mosaic made from a Photo, final once made; the only Mosaic the user can save or share.
_Avoid_: final Mosaic, output, export
