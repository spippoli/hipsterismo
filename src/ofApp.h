#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "mosaic/Mosaic.h"
#ifdef TARGET_LINUX
#include "ofGstUtils.h"
#endif

//#define LIVE_INPUT

class ofApp : public ofBaseApp{

	public:
		void setup();
		void update();
		void draw();

		void keyPressed(int key);
		void keyReleased(int key);
		void mouseMoved(int x, int y );
		void mouseDragged(int x, int y, int button);
		void mousePressed(int x, int y, int button);
		void mouseReleased(int x, int y, int button);
		void windowResized(int w, int h);
		void dragEvent(ofDragInfo dragInfo);
		void gotMessage(ofMessage msg);

        // Still mode (from the command line): render the Mosaic of
        // stillInput to stillOutput and exit, without showing a window.
        // stillMode is the vertex mode given with --mode; without it the
        // mode comes from settings.xml.
        std::string stillInput;
        std::string stillOutput;
        std::optional<int> stillMode;
        bool renderStill();

        bool fullscreen;
        bool drawGui;
        bool drawContour;
        bool drawDelaunay;
        bool drawHipster;

        ofxPanel gui;
        // Read-only name of the active `vertex mode`, shown under its slider.
        ofParameter<std::string> vertexModeName;
        ofxLabel vertexModeLabel;
        ofEventListener vertexModeListener;
#ifdef LIVE_INPUT
#ifdef TARGET_LINUX
        // ofVideoGrabber (OF 0.12.1) picks the DMABuf caps that GStreamer >= 1.24
        // advertises for v4l2 cameras and builds an unlinkable pipeline, so on
        // Linux the camera is opened with an explicit pipeline instead.
        ofGstVideoUtils cam;
#else
        ofVideoGrabber cam;
#endif
#else
        ofVideoPlayer player;
#endif
        ofPixels frame;
        ofImage frameImage;

        MosaicMaker mosaicMaker;
        Mosaic mosaic;
        ofMesh mesh;
};
