#pragma once

#include <vector>

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxCv.h"
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
#include "ofxDelaunay.h"

//#define LIVE_INPUT

typedef std::vector< std::vector<cv::Point> > ContourVector;

struct tessel {
    tessel(std::vector<ofPoint> t, ofColor c) : triangle(t),color(c) {};
    std::vector<ofPoint> triangle;
           ofColor color;
};

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
    
    bool geometric_center(const std::vector<ofPoint> &triangle, ofPoint &baricenter) {
        if(triangle.size() != 3)
            return false;
        else {
            baricenter.x = (triangle[0].x+triangle[1].x+triangle[2].x)/3;
            baricenter.y = (triangle[0].y+triangle[1].y+triangle[2].y)/3;
            baricenter.z = (triangle[0].z+triangle[1].z+triangle[2].z)/3;
            
            return true;
        }
        
    }
    
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
        ofxCv::ContourFinder finder;
        std::vector<tessel> mosaic;

        ofxDelaunay delaunay;
        ofMesh mesh;
    
        //canny parameters
        ofParameter<int> threshold1;
        ofParameter<int> threshold2;
        ofParameter<bool> gradient;
        ofParameter<int> max_it;
        
        //interpolation parameters
        ofParameter<int>  resolution;
    
    

};
