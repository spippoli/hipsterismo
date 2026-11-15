#pragma once

#include <array>
#include <map>
#include <utility>
#include <vector>

#include "ofMain.h"
#include "ofxCv.h"
#include "ofxDelaunay.h"
// ofxMSAInterpolator's sampleAt() ends in a switch with no trailing return,
// which OF's -Werror=return-type turns into a hard error.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#endif
#include "MSAInterpolator.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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

// The ways of choosing a Mosaic's vertices, as values of `vertex mode`.
enum VertexMode : int {
    VERTEX_MODE_SPLINE = 0,
    VERTEX_MODE_STABLE = 1,
    VERTEX_MODE_ADAPTIVE = 2,
    VERTEX_MODE_COUNT
};

// Name of each vertex mode, indexed by its value: for labels and the command line.
constexpr std::array<const char*, VERTEX_MODE_COUNT> vertexModeNames = {"spline", "stable", "adaptive"};

// The mode after `mode`, wrapping from the last back to the first.
inline int nextVertexMode(int mode) {
    return (mode + 1) % VERTEX_MODE_COUNT;
}

// Turns a sequence of Frames into Mosaics.
//
// Three ways of choosing the vertices, picked by `vertex mode` (a VertexMode):
//
// - Spline (0, the original look): all Contours are joined into one
//   spline and sampled `interpolation resolution` times. Vertices move with
//   every bit of noise. Each Tessel takes the Frame colour at its centroid.
// - Stable (1): the Mosaic stays still from one Frame to the next.
//   Vertices are not taken from the Contours directly: the Frame is divided into
//   cells of `cell size` pixels, every cell a Contour passes through contributes
//   one vertex at a fixed position inside it, and a cell stays active for
//   `persistence` Frames after its Contour has left. Sensor noise therefore
//   only moves a vertex when a Contour enters or leaves a cell. Each Tessel
//   takes the mean colour of the 5x5 pixels around its centroid.
// - Adaptive (2, default): no Canny and no ContourFinder. The Frame is
//   segmented into Patches of at least `min patch area` pixels, and one
//   constrained Delaunay triangulation follows their boundaries, so no
//   Tessel crosses two Patches. Each Tessel takes the mean colour of the
//   pixels inside it, and the Contours are the Patch boundaries off the
//   Frame border. See Adaptive.h for the pipeline.
//
// In stable mode make() depends on the Frames seen so far (only through the
// active cells): the same sequence of Frames and parameters always gives the
// same Mosaics. Call reset() when the next Frame does not follow the previous
// one (new source, seek); a change of Frame size, cell size or mode resets
// automatically. With persistence 0, or in spline or adaptive mode, make()
// is a pure function of the Frame.
//
// Frames may be grayscale, RGB or RGBA. The Tessels always tile the whole
// Frame rectangle, (0,0) to (width,height), even where there are no Contours;
// only an empty Frame gives an empty Mosaic.
class MosaicMaker {
public:
    MosaicMaker();

    Mosaic make(const ofPixels& frame);
    void reset();

    // The parameters that matter in vertex mode `mode`, for the panel:
    // `vertex mode` first, then the mode's own parameters in group order.
    // Spline hides `cell size` and `persistence`; stable hides
    // `interpolation resolution`; both hide `min patch area`, the only one
    // adaptive shows. Every element is one of `parameters`.
    std::vector<std::shared_ptr<ofAbstractParameter>> parametersFor(int mode);

    // Tunable pipeline parameters, named after their settings.xml tags.
    ofParameterGroup parameters;
    ofParameter<int> threshold1;
    ofParameter<int> threshold2;
    ofParameter<int> blurIterations;
    ofParameter<int> vertexMode; // a VertexMode
    ofParameter<int> cellSize;
    ofParameter<int> persistence;
    ofParameter<int> resolution;
    ofParameter<int> minArea;
    ofParameter<int> maxArea;
    ofParameter<bool> findHoles;
    ofParameter<int> minPatchArea; // px², adaptive only

private:
    using Cell = std::pair<int, int>;

    // Frames since a Contour last passed through each active cell.
    std::map<Cell, int> cellAge;
    int stateCellSize = 0;
    int stateMode = VERTEX_MODE_SPLINE;
    int stateWidth = 0, stateHeight = 0;

    ofPixels rgb;
    cv::Mat gray, edges;
    ofxCv::ContourFinder finder;
    msa::Interpolator2D interpolator;
    ofxDelaunay delaunay;
};
