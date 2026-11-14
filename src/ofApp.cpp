#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
    fullscreen = false;
    drawGui = false;
    drawContour = false;
    drawDelaunay = false;
    drawHipster = true;

    ofSetVerticalSync(true);
    ofEnableAntiAliasing();

    // The panel's root group must stay named "panel" so settings.xml keeps loading.
    mosaicMaker.parameters.setName("panel");
    gui.setup(mosaicMaker.parameters, "settings.xml", 0, 0);
    gui.setTextColor(ofColor(255,255,255));
    gui.loadFromFile("settings.xml");
#ifdef LIVE_INPUT
    cam.setDeviceID(0);
    cam.initGrabber(320, 240);
#else
    player.load("fingers.mov");
    player.play();
#endif
}

//--------------------------------------------------------------
void ofApp::update(){
#ifdef LIVE_INPUT
    cam.update();
    if (cam.isFrameNew()) {
        frame = cam.getPixels();
        frame.mirror(false, true);
#else
    player.update();
    if (player.isFrameNew()) {
        frame = player.getPixels();
#endif
        frameImage.setFromPixels(frame);
        mosaic = mosaicMaker.make(frame);
        mesh = toMesh(mosaic);
    }
}

//--------------------------------------------------------------
void ofApp::draw(){
    ofBackground(0,0,0);
    if (!frame.isAllocated()) {
        return;
    }

    ofPushStyle();
    ofPushMatrix();
    ofScale(ofGetWindowWidth()/(float)frame.getWidth(), ofGetWindowHeight()/(float)frame.getHeight());
    ofSetColor(255, 255, 255);
    if (drawHipster) {
        mesh.draw();
    } else {
        frameImage.draw(0, 0);
    }

    if (drawContour) {
        ofNoFill();
        ofSetColor(255, 255, 255);
        for (const ofPolyline& contour : mosaic.contours) {
            contour.draw();
        }
    }

    if (drawDelaunay) {
        ofNoFill();
        ofSetColor(180, 180, 180);
        mesh.disableColors();
        mesh.drawWireframe();
        mesh.enableColors();
    }
    ofPopMatrix();
    ofPopStyle();

    if (drawGui) {
        ofPushStyle();
        ofPushMatrix();
        glDisable(GL_DEPTH_TEST);
        gui.draw();
        glEnable(GL_DEPTH_TEST);
        ofDrawBitmapString(ofToString(ofGetFrameRate()), 10,ofGetWindowHeight()-30);
        ofPopMatrix();
        ofPopStyle();
    }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    switch (key) {
        case 'f':
        {
            fullscreen = !fullscreen;
            ofSetFullscreen(fullscreen);
        }
            break;
        case 'g':
        {
            drawGui = !drawGui;
        }
            break;
        case 'c':
        {
            drawContour = !drawContour;
        }
            break;
        case 'd':
        {
            drawDelaunay = !drawDelaunay;
        }
            break;
        case 'h':
        {
            drawHipster = !drawHipster;
        }
            break;
        case 'p':
        {
            ofImage snapshot;
            snapshot.allocate(ofGetWindowWidth(), ofGetWindowHeight(), OF_IMAGE_COLOR);
            snapshot.grabScreen(0,0,ofGetWindowWidth(), ofGetWindowHeight());
            snapshot.save("screenshot.png");
        }
            break;
        default:
            break;
    }
    
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
