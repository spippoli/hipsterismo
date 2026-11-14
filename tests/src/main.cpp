#include "ofMain.h"
#include "ofAppNoWindow.h"
#include "ofxUnitTests.h"
#include "Mosaic.h"

namespace {

const ofColor background(40, 80, 160);
const ofColor foreground(220, 60, 30);

ofPixels solidFrame(int width, int height, const ofColor& color) {
    ofPixels frame;
    frame.allocate(width, height, OF_IMAGE_COLOR);
    frame.setColor(color);
    return frame;
}

// A filled rectangle on a plain background: gives strong, well-defined Contours.
ofPixels rectangleFrame(const ofColor& bg, const ofColor& fg) {
    ofPixels frame = solidFrame(160, 120, bg);
    for (int y = 40; y < 100; y++) {
        for (int x = 40; x < 120; x++) {
            frame.setColor(x, y, fg);
        }
    }
    return frame;
}

glm::vec3 centroid(const Tessel& tessel) {
    return (tessel.triangle[0] + tessel.triangle[1] + tessel.triangle[2]) / 3.f;
}

bool sameMosaic(const Mosaic& a, const Mosaic& b) {
    if (a.tessels.size() != b.tessels.size()) return false;
    for (size_t i = 0; i < a.tessels.size(); i++) {
        if (a.tessels[i].color != b.tessels[i].color) return false;
        for (int v = 0; v < 3; v++) {
            if (a.tessels[i].triangle[v] != b.tessels[i].triangle[v]) return false;
        }
    }
    return true;
}

} // namespace

class ofApp : public ofxUnitTestsApp {
    void run() {
        testEmptyFrame();
        testFrameWithoutContours();
        testTesselColourComesFromFrame();
        testTesselsStayInsideFrame();
        testDeterminism();
        testContourAreaFilter();
        testPixelFormats();
        testToMesh();
    }

    void testEmptyFrame() {
        MosaicMaker maker;
        Mosaic mosaic = maker.make(ofPixels());
        ofxTest(mosaic.tessels.empty(), "empty Frame gives no Tessels");
        ofxTest(mosaic.contours.empty(), "empty Frame gives no Contours");
    }

    void testFrameWithoutContours() {
        MosaicMaker maker;
        Mosaic mosaic = maker.make(solidFrame(160, 120, ofColor::black));
        ofxTest(mosaic.tessels.empty(), "uniform Frame gives no Tessels");
        ofxTest(mosaic.contours.empty(), "uniform Frame gives no Contours");
    }

    void testTesselColourComesFromFrame() {
        MosaicMaker maker;
        ofPixels frame = rectangleFrame(background, foreground);
        Mosaic mosaic = maker.make(frame);

        ofxTest(!mosaic.contours.empty(), "rectangle Frame has Contours");
        ofxTest(!mosaic.tessels.empty(), "rectangle Frame has Tessels");

        bool allMatch = true;
        bool foundForeground = false;
        for (const Tessel& tessel : mosaic.tessels) {
            glm::vec3 c = centroid(tessel);
            ofColor expected = frame.getColor(int(c.x), int(c.y));
            allMatch = allMatch && tessel.color == expected;
            foundForeground = foundForeground || tessel.color == foreground;
        }
        ofxTest(allMatch, "every Tessel takes the Frame colour at its centroid");
        ofxTest(foundForeground, "some Tessels take the rectangle's colour");
    }

    void testTesselsStayInsideFrame() {
        // A rectangle close to every border: resampling the Contours overshoots
        // past the Frame's edges unless it is kept inside.
        ofPixels frame = solidFrame(160, 120, background);
        for (int y = 6; y < 114; y++) {
            for (int x = 6; x < 154; x++) {
                frame.setColor(x, y, foreground);
            }
        }
        Mosaic mosaic = MosaicMaker().make(frame);
        ofxTest(!mosaic.tessels.empty(), "near-border Frame has Tessels");

        bool inside = true;
        bool coloursMatch = true;
        for (const Tessel& tessel : mosaic.tessels) {
            for (const glm::vec3& v : tessel.triangle) {
                inside = inside && v.x >= 0 && v.y >= 0 && v.x <= 159 && v.y <= 119;
            }
            glm::vec3 c = centroid(tessel);
            coloursMatch = coloursMatch && tessel.color == frame.getColor(int(c.x), int(c.y));
        }
        ofxTest(inside, "every Tessel vertex lies inside the Frame");
        ofxTest(coloursMatch, "border Tessels take the Frame colour at their centroid");
    }

    void testDeterminism() {
        ofPixels frame = rectangleFrame(background, foreground);
        MosaicMaker maker;
        Mosaic first = maker.make(frame);
        Mosaic again = maker.make(frame);
        Mosaic fresh = MosaicMaker().make(frame);
        ofxTest(sameMosaic(first, again), "same maker, same Frame: same Mosaic");
        ofxTest(sameMosaic(first, fresh), "fresh maker, same Frame: same Mosaic");
    }

    void testContourAreaFilter() {
        ofPixels frame = rectangleFrame(background, foreground); // 80x60 rectangle
        MosaicMaker maker;
        maker.minArea = 10000;
        Mosaic mosaic = maker.make(frame);
        ofxTest(mosaic.contours.empty() && mosaic.tessels.empty(),
                "Contours smaller than min area are ignored");
    }

    void testPixelFormats() {
        ofPixels rgb = rectangleFrame(background, foreground);
        ofPixels rgba = rgb;
        rgba.setImageType(OF_IMAGE_COLOR_ALPHA);
        ofxTest(sameMosaic(MosaicMaker().make(rgb), MosaicMaker().make(rgba)),
                "RGBA Frame gives the same Mosaic as RGB");

        ofPixels gray = rgb;
        gray.setImageType(OF_IMAGE_GRAYSCALE);
        ofPixels grayAsRgb = gray;
        grayAsRgb.setImageType(OF_IMAGE_COLOR);
        Mosaic fromGray = MosaicMaker().make(gray);
        ofxTest(!fromGray.tessels.empty(), "grayscale Frame has Tessels");
        ofxTest(sameMosaic(fromGray, MosaicMaker().make(grayAsRgb)),
                "grayscale Frame gives the same Mosaic as its RGB equivalent");

        bool allGray = true;
        for (const Tessel& tessel : fromGray.tessels) {
            allGray = allGray && tessel.color.r == tessel.color.g && tessel.color.g == tessel.color.b;
        }
        ofxTest(allGray, "grayscale Frame gives gray Tessels");
    }

    void testToMesh() {
        Mosaic mosaic = MosaicMaker().make(rectangleFrame(background, foreground));
        ofMesh mesh = toMesh(mosaic);
        ofxTestEq(mesh.getNumVertices(), mosaic.tessels.size() * 3, "three vertices per Tessel");
        ofxTestEq(mesh.getNumColors(), mosaic.tessels.size() * 3, "one colour per vertex");

        bool coloursMatch = true;
        for (size_t i = 0; i < mosaic.tessels.size(); i++) {
            for (int v = 0; v < 3; v++) {
                coloursMatch = coloursMatch && ofColor(mesh.getColor(i * 3 + v)) == mosaic.tessels[i].color
                               && mesh.getVertex(i * 3 + v) == mosaic.tessels[i].triangle[v];
            }
        }
        ofxTest(coloursMatch, "mesh vertices and colours match the Tessels");
    }
};

int main() {
    ofInit();
    auto window = std::make_shared<ofAppNoWindow>();
    auto app = std::make_shared<ofApp>();
    ofRunApp(window, app);
    return ofRunMainLoop();
}
