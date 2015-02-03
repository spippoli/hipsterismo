#include "ofApp.h"

cv::Mat source,sourceBW, canny;
msa::Interpolator2D interpol;




//--------------------------------------------------------------
void ofApp::setup(){
    fullscreen = false;
    drawGui = false;
    drawContour = false;
    drawDelaunay = false;
    drawHipster = true;
    
    ofSetVerticalSync(true);
//    ofEnableAlphaBlending();
    ofEnableAntiAliasing();
    
    gui.setup("panel","settings.xml",0,0);
    gui.add(threshold1.set("first threshold",0,0,400));
    gui.add(threshold2.set("second threshold",20,0,400));
    gui.add(max_it.set("blur iteration",9,1,31));
    gui.add(resolution.set("interpolation resolution",1000,2,10000));
    gui.setTextColor(ofColor(255,255,255));
    gui.loadFromFile("settings.xml");
#ifdef LIVE_INPUT
    cam.setDeviceID(0);
    cam.initGrabber(320, 240);
#else
    player.loadMovie("fingers.mov");
    player.play();
#endif
    
    finder.setMinArea(2);
    finder.setMaxArea(400000);
    finder.setThreshold(128);
    finder.setFindHoles(true);

    mesh.setMode(OF_PRIMITIVE_TRIANGLES);
    
}

//--------------------------------------------------------------
void ofApp::update(){
    
#ifdef LIVE_INPUT
    cam.update();
    if (cam.isFrameNew()) {
        cv::flip(ofxCv::toCv(cam), source, 1);
#else
    player.update();
    if (player.isFrameNew()) {
        source = ofxCv::toCv(player);
#endif
        
        
        ofxCv::cvtColor(source, sourceBW, CV_BGR2GRAY); //make BW copy for canny edge detection
        for(int i = 1;i < max_it; i = i+2) //blur loop
        ofxCv::GaussianBlur(sourceBW, sourceBW, cv::Size_<int>(i,i), 1);
        ofxCv::Canny(sourceBW, canny, threshold1, threshold2,3,gradient); //edge detection
        finder.findContours(canny);
        ContourVector contours = finder.getContours(); //populate delaunay object with points interpolated from detected edges
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
        delaunay.triangulate(); // create mesh
        mesh.clear();
        const vector<ofMeshFace> faces = delaunay.triangleMesh.getUniqueFaces();
        for (int i = 0; i < faces.size(); i++) //for each face of the triangulation add a triangle to the mesh and set the color from source frame  
        {
            ofMeshFace face = faces[i];
            ofPoint center = (face.getVertex(0) + face.getVertex(1) + face.getVertex(2))/3;
            int r,g,b,a = 0;
            if (center.x >= 0 && center.y >= 0 && center.z >= 0) {
                r = source.at<cv::Vec3b>(center.y,center.x)[0];
                g = source.at<cv::Vec3b>(center.y,center.x)[1];
                b = source.at<cv::Vec3b>(center.y,center.x)[2];
//                a = (r+g+b)/3;
            }

            ofColor faceColor = ofColor(r, g, b);            
//            ofColor faceColor = ofColor(r, g, b,a);
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

    }

    
}

//--------------------------------------------------------------
void ofApp::draw(){
    ofBackground(0,0,0);

    if(drawHipster) {
        
        
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)source.rows, ofGetWindowHeight()/(float)source.cols);
        ofSetColor(255, 255, 255);
        mesh.draw();
        ofPopMatrix();
        ofPopStyle();
        
    }
    else {
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)source.rows, ofGetWindowHeight()/(float)source.cols);
        ofSetColor(255, 255, 255);
        ofxCv::drawMat(source, 0, 0);
        ofPopMatrix();
        ofPopStyle();
        
    }

    
    if(drawContour) {
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)source.rows, ofGetWindowHeight()/(float)source.cols);
        ofNoFill();
        ofSetColor(255, 255, 255);
        finder.draw();
        ofPopMatrix();
        ofPopStyle();
    }
    
    if (drawDelaunay) {
        ofPushStyle();
        ofPushMatrix();
        ofScale(ofGetWindowWidth()/(float)source.rows, ofGetWindowHeight()/(float)source.cols);
        ofNoFill();
        ofSetColor(180, 180, 180);
        delaunay.draw();
        ofPopMatrix();
        ofPopStyle();
    }


    


    
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
            snapshot.saveImage("screenshot.png");
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
