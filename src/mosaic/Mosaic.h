#pragma once

#include <array>
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

// Turns Frames into Mosaics.
//
// make() behaves as a pure function of (frame, parameters): the same inputs
// always give the same Mosaic. The object only keeps scratch buffers so that
// video-rate Frames don't reallocate them every time.
//
// Frames may be grayscale, RGB or RGBA. The Tessels always tile the whole
// Frame rectangle, (0,0) to (width,height), even where there are no Contours;
// only an empty Frame gives an empty Mosaic.
class MosaicMaker {
public:
    MosaicMaker();

    Mosaic make(const ofPixels& frame);

    // Tunable pipeline parameters, named after their settings.xml tags.
    ofParameterGroup parameters;
    ofParameter<int> threshold1;
    ofParameter<int> threshold2;
    ofParameter<int> blurIterations;
    ofParameter<int> resolution;
    ofParameter<int> minArea;
    ofParameter<int> maxArea;
    ofParameter<bool> findHoles;

private:
    ofPixels rgb;
    cv::Mat gray, edges;
    ofxCv::ContourFinder finder;
    msa::Interpolator2D interpolator;
    ofxDelaunay delaunay;
};
