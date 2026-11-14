# hipsterismo

Turns moving or still images into low-poly graphics made of flat-coloured triangles.

## Language

**Frame**:
A single image fed to the app, whether it comes from a video, a camera or a still picture.
_Avoid_: source, input, image (when the role is meant)

**Contour**:
An outline of an edge found in a Frame; the Tessels are built from points along the Contours.
_Avoid_: edge, outline, blob

**Tessel**:
One triangle of a Mosaic together with its single flat colour, taken from the Frame at the triangle's centre.
_Avoid_: face, triangle (when the colour is part of the meaning), tile

**Mosaic**:
The set of Tessels produced from one Frame, together with the Contours they were built from; what the app shows in its "hipster" view.
_Avoid_: hipster, mesh, polygonization, low-poly
