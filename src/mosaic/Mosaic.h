#pragma once

#include <array>
#include <map>
#include <utility>
#include <vector>

#include "ofMain.h"
#include "ofxCv.h"
#include "ofxDelaunay.h"

// One triangle of a Mosaic with its flat colour, in Frame pixel coordinates.
struct Tessel {
    std::array<glm::vec3, 3> triangle;
    ofColor color;
};

// The Tessels produced from one Frame, plus the Contours they were built from.
struct Mosaic {
    std::vector<Tessel> tessels;
    std::vector<ofPolyline> contours;
};

// Flat-coloured triangle mesh of the Mosaic, ready to draw in Frame space.
ofMesh toMesh(const Mosaic& mosaic);

// Turns a sequence of Frames into Mosaics that stay stable from one Frame to
// the next.
//
// Vertices are not taken from the Contours directly: the Frame is divided into
// cells of `cell size` pixels, every cell a Contour passes through contributes
// one vertex at a fixed position inside it, and a cell stays active for
// `persistence` Frames after its Contour has left. Sensor noise therefore only
// moves a vertex when a Contour enters or leaves a cell.
//
// make() depends on the Frames seen so far (only through the active cells):
// the same sequence of Frames and parameters always gives the same Mosaics.
// Call reset() when the next Frame does not follow the previous one (new
// source, seek); a change of Frame size or cell size resets automatically.
// With persistence 0, make() is a pure function of the Frame.
//
// Frames may be grayscale, RGB or RGBA. The Tessels always tile the whole
// Frame rectangle, (0,0) to (width,height), even where there are no Contours;
// only an empty Frame gives an empty Mosaic. Each Tessel takes the mean colour
// of the 5x5 pixels around its centroid.
class MosaicMaker {
public:
    MosaicMaker();

    Mosaic make(const ofPixels& frame);
    void reset();

    // Tunable pipeline parameters, named after their settings.xml tags.
    ofParameterGroup parameters;
    ofParameter<int> threshold1;
    ofParameter<int> threshold2;
    ofParameter<int> blurIterations;
    ofParameter<int> cellSize;
    ofParameter<int> persistence;
    ofParameter<int> minArea;
    ofParameter<int> maxArea;
    ofParameter<bool> findHoles;

private:
    using Cell = std::pair<int, int>;

    // Frames since a Contour last passed through each active cell.
    std::map<Cell, int> cellAge;
    int stateCellSize = 0;
    int stateWidth = 0, stateHeight = 0;

    ofPixels rgb;
    cv::Mat gray, edges;
    ofxCv::ContourFinder finder;
    ofxDelaunay delaunay;
};
