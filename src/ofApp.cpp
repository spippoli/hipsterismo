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
    if (!stillInput.empty()) {
        ofExit(renderStill() ? 0 : 1);
        return;
    }
#ifdef LIVE_INPUT
#ifdef TARGET_LINUX
    // No pixel format in the caps: GStreamer negotiates a system-memory one
    // (e.g. YUY2) and videoconvert turns it into the RGB the appsink asks for.
    if (!cam.setPipeline("v4l2src device=/dev/video0 ! video/x-raw,width=320,height=240,framerate=30/1 ! videoconvert",
                         OF_PIXELS_RGB, false, 320, 240)
        || !cam.startPipeline()) {
        ofLogError("ofApp") << "could not open /dev/video0";
    }
#else
    cam.setDeviceID(0);
    cam.initGrabber(320, 240);
#endif
#else
    player.load("fingers.mov");
    player.play();
#endif
}

//--------------------------------------------------------------
void ofApp::update(){
    if (!stillInput.empty()) {
        return;
    }
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
// Mosaic of one image with the spline (original) vertices and the thresholds
// from settings.xml, drawn at the image's own size and saved.
bool ofApp::renderStill(){
    ofPixels image;
    if (!ofLoadImage(image, stillInput) || !image.isAllocated()) {
        ofLogError("ofApp") << "could not load image " << stillInput;
        return false;
    }
    mosaicMaker.vertexMode = VERTEX_MODE_SPLINE;
    ofMesh stillMesh = toMesh(mosaicMaker.make(image));

    ofFbo fbo;
    fbo.allocate(image.getWidth(), image.getHeight(), GL_RGB, 4);
    fbo.begin();
    ofClear(0, 0, 0, 255);
    ofSetColor(255, 255, 255);
    stillMesh.draw();
    fbo.end();

    ofPixels result;
    fbo.readToPixels(result);
    if (!ofSaveImage(result, stillOutput)) {
        ofLogError("ofApp") << "could not save " << stillOutput;
        return false;
    }
    ofLogNotice("ofApp") << "saved " << stillOutput;
    return true;
}

//--------------------------------------------------------------
void ofApp::draw(){
    ofBackground(0,0,0);
    if (!frame.isAllocated()) {
        return;
    }

    ofPushStyle();
    ofPushMatrix();
    // Fit the Frame inside the window keeping its aspect ratio, centred
    // (letterboxed or pillarboxed on the black background).
    ofRectangle placement(0, 0, frame.getWidth(), frame.getHeight());
    placement.scaleTo(ofRectangle(0, 0, ofGetWindowWidth(), ofGetWindowHeight()), OF_SCALEMODE_FIT);
    ofTranslate(placement.x, placement.y);
    ofScale(placement.width / frame.getWidth());
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
        case 's':
        {
            // Cycle through the vertex modes, wrapping after the last.
            mosaicMaker.vertexMode = nextVertexMode(mosaicMaker.vertexMode);
            ofLogNotice("ofApp") << "vertex mode: " << vertexModeNames[mosaicMaker.vertexMode];
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
