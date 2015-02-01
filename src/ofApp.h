#pragma once

#include <vector>

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxCv.h"
#include "MSAInterpolator.h"
#include "ofxDelaunay.h"

typedef vector< vector<cv::Point> > ContourVector;

struct tessel {
    tessel(vector<ofPoint> t, ofColor c) : triangle(t),color(c) {};
    vector<ofPoint> triangle;
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
    
    bool geometric_center(const vector<ofPoint> &triangle, ofPoint &baricenter) {
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
    
        ofVideoGrabber cam;
        ofxCv::ContourFinder finder;
        vector<tessel> mosaic;

        ofxDelaunay delaunay;
    
        //canny parameters
        ofParameter<int> threshold1;
        ofParameter<int> threshold2;
        ofParameter<bool> gradient;
        ofParameter<int> max_it;
    
        //interpolation parameters
        ofParameter<int>  resolution;
    
    

};

//inline ofPoint getCenter(const ITRIANGLE & tri) { return ofPoint( tri.p )
