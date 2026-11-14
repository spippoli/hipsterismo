# hipsterismo

Turns moving or still images into low-poly graphics made of flat-coloured triangles.

## Language

**Frame**:
A single image fed to the app, whether it comes from a video, a camera or a still picture.
_Avoid_: source, input, image (when the role is meant)

**Contour**:
An outline of an edge found in a Frame; the Tessels are built from points along the Contours.
_Avoid_: edge, outline, blob

**Patch**:
A connected region of a Frame whose pixels have similar colour; the Tessels cover it without crossing its boundary.
_Avoid_: region, segment, superpixel, blob

**Tessel**:
One triangle of a Mosaic together with its single flat colour, taken from the Frame where the triangle lies.
_Avoid_: face, triangle (when the colour is part of the meaning), tile

**Mosaic**:
The set of Tessels produced from one Frame, tiling the whole Frame rectangle, together with the Contours they were built from, if any; what the app shows in its "hipster" view.
_Avoid_: hipster, mesh, polygonization, low-poly
