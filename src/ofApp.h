#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "mosaic/Mosaic.h"

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

        bool fullscreen;
        bool drawGui;
        bool drawContour;
        bool drawDelaunay;
        bool drawHipster;

        ofxPanel gui;
#ifdef LIVE_INPUT
        ofVideoGrabber cam;
#else
        ofVideoPlayer player;
#endif
        ofPixels frame;
        ofImage frameImage;

        MosaicMaker mosaicMaker;
        Mosaic mosaic;
        ofMesh mesh;
};
