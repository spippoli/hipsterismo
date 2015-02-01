#include "ofApp.h"

cv::Mat flipped, flippedBW, canny;
msa::Interpolator2D interpol;




//--------------------------------------------------------------
void ofApp::setup(){
    fullscreen = false;
    drawGui = false;
    drawContour = false;
    drawDelaunay = false;
    drawHipster = true;
    ofSetVerticalSync(true);
    gui.setup("panel","settings.xml",0,0);
    gui.add(threshold1.set("first threshold",0,0,400));
    gui.add(threshold2.set("second threshold",20,0,400));
    gui.add(max_it.set("blur iteration",9,1,31));
    gui.add(resolution.set("interpolation resolution",1000,2,10000));
    gui.setTextColor(ofColor(255,255,255));
    
    cam.initGrabber(320, 240);
    finder.setMinArea(2);
    finder.setMaxArea(400000);
    finder.setThreshold(128);
    finder.setFindHoles(true);
    
    glEnable(GL_DEPTH_TEST);
    
    //ofSetFrameRate(5);
}

//--------------------------------------------------------------
void ofApp::update(){
    cam.update();
    if (cam.isFrameNew()) {

        cv::flip(ofxCv::toCv(cam), flipped, 1);
        ofxCv::cvtColor(flipped, flippedBW, CV_BGR2GRAY);
        for(int i = 1;i < max_it; i = i+2)
        ofxCv::GaussianBlur(flippedBW, flippedBW, cv::Size_<int>(i,i), 1);
        ofxCv::Canny(flippedBW, canny, threshold1, threshold2,3,gradient);
        
        finder.findContours(canny);
        ContourVector contours = finder.getContours();
        interpol.clear();
        delaunay.reset();
        for (ContourVector::iterator contour = contours.begin(); contour != contours.end(); contour++) {
            for(vector<cv::Point>::iterator point = contour->begin(); point != contour->end(); point++) {
                interpol.push_back(ofxCv::toOf(*point));
            }
        }
        
        for (float step = 0.0; step < 1.0 ; step+=1/(float)resolution) {
            delaunay.addPoint(interpol.sampleAt(step));
        }
        delaunay.triangulate();
    }

}

//--------------------------------------------------------------
void ofApp::draw(){

    if (drawGui) {
        ofPushStyle();
        ofPushMatrix();
        gui.draw();
        ofPopMatrix();
        ofPopStyle();
    }
    
    if(drawContour) {
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)cam.width, ofGetWindowHeight()/(float)cam.height);
        ofNoFill();
        ofSetColor(255, 255, 255);
        finder.draw();
        ofPopMatrix();
        ofPopStyle();
    }
    
    if (drawDelaunay) {
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)cam.width, ofGetWindowHeight()/(float)cam.height);
        ofNoFill();
        ofSetColor(180, 180, 180);
        delaunay.draw();
        ofPopMatrix();
        ofPopStyle();
    }
    
    if(drawHipster) {
        
        ofMesh mesh;
        mesh.setMode(OF_PRIMITIVE_TRIANGLES);
        const vector<ofMeshFace> faces = delaunay.triangleMesh.getUniqueFaces();
        for (int i = 0; i < faces.size(); i++)
        {
            ofMeshFace face = faces[i];
            ofPoint center = (face.getVertex(0) + face.getVertex(1) + face.getVertex(2))/3;
            int r,g,b = 0;
            if (center.x >= 0 && center.y >= 0 && center.z >= 0) {
                r = flipped.at<cv::Vec3b>(center.y,center.x)[0];
                g = flipped.at<cv::Vec3b>(center.y,center.x)[1];
                b = flipped.at<cv::Vec3b>(center.y,center.x)[2];
            }
            ofColor faceColor = ofColor(r, g, b);
            mesh.addVertex(face.getVertex(0));
            mesh.addTexCoord(face.getVertex(0));
            mesh.addColor(faceColor);
                             mesh.addVertex(face.getVertex(1));
                             mesh.addTexCoord(face.getVertex(1));
                             mesh.addColor(faceColor);
                                              mesh.addVertex(face.getVertex(2));
                                              mesh.addTexCoord(face.getVertex(2));
                                              mesh.addColor(faceColor);
        }
        
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)cam.width, ofGetWindowHeight()/(float)cam.height);
        //ofEnableAlphaBlending();
        //ofEnableBlendMode(OF_BLEN);
        ofSetColor(255, 255, 255);
        mesh.draw();
        ofPopMatrix();
        ofPopStyle();
        
    }
    
    else {
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)cam.width, ofGetWindowHeight()/(float)cam.height);
        ofSetColor(255, 255, 255);
        ofxCv::drawMat(flipped, 0, 0);
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
