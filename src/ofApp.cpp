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

    // settings.xml keeps the Mosaic parameters under <panel>, so the group
    // is renamed for ofSerialize; the panel header has the same name.
    mosaicMaker.parameters.setName("panel");
    vertexModeName.set("mode", vertexModeNames[mosaicMaker.vertexMode]);
    vertexModeName.setSerializable(false);
    vertexModeListener = mosaicMaker.vertexMode.newListener([this](int& mode) {
        vertexModeName = vertexModeNames[ofClamp(mode, 0, VERTEX_MODE_COUNT - 1)];
        panelDirty = true;
    });
    gui.setup("panel", "settings.xml", 0, 0);
    gui.setTextColor(ofColor(255,255,255));
    settingsListeners.push(gui.savePressedE.newListener(this, &ofApp::saveSettings));
    settingsListeners.push(gui.loadPressedE.newListener(this, &ofApp::loadSettings));
    loadSettings();
    rebuildPanel();
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
    if (panelDirty) {
        rebuildPanel();
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
// `vertex mode` and its name first, then the active mode's parameters.
void ofApp::rebuildPanel(){
    panelDirty = false;
    gui.clear();
    for (auto& parameter : mosaicMaker.parametersFor(mosaicMaker.vertexMode)) {
        if (parameter->type() == typeid(ofParameter<int>).name()) {
            gui.add(parameter->cast<int>());
        } else if (parameter->type() == typeid(ofParameter<bool>).name()) {
            gui.add(parameter->cast<bool>());
        } else {
            ofLogWarning("ofApp") << "no control for parameter " << parameter->getName();
        }
        if (parameter->isReferenceTo(mosaicMaker.vertexMode)) {
            gui.add(vertexModeLabel.setup(vertexModeName));
        }
    }
}

//--------------------------------------------------------------
// Returning true stops the panel's own save, which would only write the
// parameters it shows.
bool ofApp::saveSettings(){
    ofXml xml;
    ofSerialize(xml, mosaicMaker.parameters);
    if (!xml.save("settings.xml")) {
        ofLogError("ofApp") << "could not save settings.xml";
    }
    return true;
}

//--------------------------------------------------------------
bool ofApp::loadSettings(){
    ofXml xml;
    if (xml.load("settings.xml")) {
        ofDeserialize(xml, mosaicMaker.parameters);
    } else {
        ofLogWarning("ofApp") << "could not load settings.xml, using the defaults";
    }
    return true;
}

//--------------------------------------------------------------
// Mosaic of one image with the parameters from settings.xml (the defaults
// for those it lacks) and the vertex mode from --mode, if given, drawn at the
// image's own size and saved.
bool ofApp::renderStill(){
    ofPixels image;
    if (!ofLoadImage(image, stillInput) || !image.isAllocated()) {
        ofLogError("ofApp") << "could not load image " << stillInput;
        return false;
    }
    // Read the settings here rather than rely on the panel having loaded them.
    ofXml settings;
    if (settings.load("settings.xml")) {
        ofXml panel = settings.getChild("panel");
        for (auto& parameter : mosaicMaker.parameters) {
            ofDeserialize(panel, *parameter);
        }
    }
    if (stillMode) {
        mosaicMaker.vertexMode = *stillMode;
    }
    mosaicMaker.vertexMode = ofClamp(mosaicMaker.vertexMode, 0, VERTEX_MODE_COUNT - 1);
    ofLogNotice("ofApp") << "vertex mode: " << vertexModeNames[mosaicMaker.vertexMode];
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
