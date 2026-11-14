#include "ofMain.h"
#include "ofAppNoWindow.h"
#include "ofxUnitTests.h"
#include "Mosaic.h"

#include <set>

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

double area(const Tessel& tessel) {
    glm::dvec3 ab = glm::dvec3(tessel.triangle[1]) - glm::dvec3(tessel.triangle[0]);
    glm::dvec3 ac = glm::dvec3(tessel.triangle[2]) - glm::dvec3(tessel.triangle[0]);
    return std::abs(ab.x * ac.y - ab.y * ac.x) / 2.0;
}

double coveredArea(const Mosaic& mosaic) {
    double total = 0;
    for (const Tessel& tessel : mosaic.tessels) total += area(tessel);
    return total;
}

// Tessels tile the Frame exactly: no gaps (area too small) and no overlaps
// (area too large). The tolerance only absorbs float vertex rounding.
bool coversFrame(const Mosaic& mosaic, double width, double height) {
    double covered = coveredArea(mosaic);
    if (std::abs(covered - width * height) < 0.1) return true;
    ofLogError("coversFrame") << "covered " << ofToString(covered, 3) << " of " << width * height;
    return false;
}

// The colour a Tessel should have: mean of the 5x5 pixels around its centroid.
ofColor expectedColour(const ofPixels& frame, const Tessel& tessel) {
    glm::vec3 c = centroid(tessel);
    int cx = ofClamp(int(c.x), 0, frame.getWidth() - 1);
    int cy = ofClamp(int(c.y), 0, frame.getHeight() - 1);
    int r = 0, g = 0, b = 0, n = 0;
    for (int y = cy - 2; y <= cy + 2; y++) {
        for (int x = cx - 2; x <= cx + 2; x++) {
            if (x < 0 || y < 0 || x >= int(frame.getWidth()) || y >= int(frame.getHeight())) continue;
            ofColor p = frame.getColor(x, y);
            r += p.r; g += p.g; b += p.b; n++;
        }
    }
    return ofColor(r / n, g / n, b / n);
}

std::set<std::pair<float, float>> vertices(const Mosaic& mosaic) {
    std::set<std::pair<float, float>> result;
    for (const Tessel& tessel : mosaic.tessels) {
        for (const glm::vec3& v : tessel.triangle) result.insert({v.x, v.y});
    }
    return result;
}

// Fraction of the vertices of `before` found at exactly the same place in `after`.
double keptVertices(const Mosaic& before, const Mosaic& after) {
    auto a = vertices(before), b = vertices(after);
    size_t kept = 0;
    for (const auto& v : a) kept += b.count(v);
    return a.empty() ? 1.0 : double(kept) / a.size();
}

bool containsVertices(const Mosaic& outer, const Mosaic& inner) {
    auto o = vertices(outer);
    for (const auto& v : vertices(inner)) {
        if (!o.count(v)) return false;
    }
    return true;
}

// Static scene + camera-like sensor noise (+-4 per channel), reproducible per seed.
ofPixels withNoise(const ofPixels& frame, int seed) {
    ofPixels noisy = frame;
    ofSeedRandom(seed);
    for (auto px : noisy.getPixelsIter()) {
        for (int c = 0; c < 3; c++) px[c] = ofClamp(px[c] + ofRandom(-4, 4), 0, 255);
    }
    return noisy;
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

// The tests below that are not about spline mode exercise stable mode, which
// is not the default.
struct StableMaker : MosaicMaker {
    StableMaker() { stableVertices = true; }
};

class ofApp : public ofxUnitTestsApp {
    void run() {
        testEmptyFrame();
        testFrameWithoutContours();
        testMosaicCoversFrame();
        testTesselColourComesFromFrame();
        testTesselsStayInsideFrame();
        testDeterminism();
        testPersistence();
        testStabilityUnderNoise();
        testSplineMode();
        testContourAreaFilter();
        testPixelFormats();
        testToMesh();
    }

    void testEmptyFrame() {
        StableMaker maker;
        Mosaic mosaic = maker.make(ofPixels());
        ofxTest(mosaic.tessels.empty(), "empty Frame gives no Tessels");
        ofxTest(mosaic.contours.empty(), "empty Frame gives no Contours");
    }

    void testFrameWithoutContours() {
        StableMaker maker;
        Mosaic mosaic = maker.make(solidFrame(160, 120, background));
        ofxTest(mosaic.contours.empty(), "uniform Frame gives no Contours");
        ofxTest(coversFrame(mosaic, 160, 120), "uniform Frame is still covered by Tessels");

        bool allBackground = true;
        for (const Tessel& tessel : mosaic.tessels) allBackground = allBackground && tessel.color == background;
        ofxTest(allBackground, "uniform Frame gives Tessels of its colour");
    }

    void testMosaicCoversFrame() {
        // Contours well inside the Frame: without help their hull leaves the borders uncovered.
        ofPixels frame = rectangleFrame(background, foreground);
        Mosaic mosaic = StableMaker().make(frame);
        ofxTest(coversFrame(mosaic, 160, 120), "Tessels cover the whole Frame");

        ofPixels nearBorder = solidFrame(160, 120, background);
        for (int y = 6; y < 114; y++) for (int x = 6; x < 154; x++) nearBorder.setColor(x, y, foreground);
        ofxTest(coversFrame(StableMaker().make(nearBorder), 160, 120),
                  "Tessels cover the whole Frame near the borders too");
    }

    void testTesselColourComesFromFrame() {
        StableMaker maker;
        ofPixels frame = rectangleFrame(background, foreground);
        Mosaic mosaic = maker.make(frame);

        ofxTest(!mosaic.contours.empty(), "rectangle Frame has Contours");
        ofxTest(!mosaic.tessels.empty(), "rectangle Frame has Tessels");

        bool allMatch = true;
        bool foundForeground = false;
        for (const Tessel& tessel : mosaic.tessels) {
            allMatch = allMatch && tessel.color == expectedColour(frame, tessel);
            foundForeground = foundForeground || tessel.color == foreground;
        }
        ofxTest(allMatch, "every Tessel takes the mean Frame colour around its centroid");
        ofxTest(foundForeground, "some Tessels take the rectangle's colour");
    }

    void testTesselsStayInsideFrame() {
        // A rectangle close to every border: its Contour cells reach the border.
        ofPixels frame = solidFrame(160, 120, background);
        for (int y = 6; y < 114; y++) {
            for (int x = 6; x < 154; x++) {
                frame.setColor(x, y, foreground);
            }
        }
        Mosaic mosaic = StableMaker().make(frame);
        ofxTest(!mosaic.tessels.empty(), "near-border Frame has Tessels");

        bool inside = true;
        bool coloursMatch = true;
        for (const Tessel& tessel : mosaic.tessels) {
            for (const glm::vec3& v : tessel.triangle) {
                inside = inside && v.x >= 0 && v.y >= 0 && v.x <= 160 && v.y <= 120;
            }
            coloursMatch = coloursMatch && tessel.color == expectedColour(frame, tessel);
        }
        ofxTest(inside, "every Tessel vertex lies inside the Frame");
        ofxTest(coloursMatch, "border Tessels take the mean Frame colour around their centroid");
    }

    void testDeterminism() {
        ofPixels rectangle = rectangleFrame(background, foreground);
        ofPixels uniform = solidFrame(160, 120, background);
        std::vector<ofPixels> sequence = {rectangle, uniform, rectangle, withNoise(rectangle, 1)};

        StableMaker a, b;
        bool same = true;
        for (const ofPixels& frame : sequence) same = same && sameMosaic(a.make(frame), b.make(frame));
        ofxTest(same, "same sequence of Frames: same Mosaics");

        a.reset();
        ofxTest(sameMosaic(a.make(uniform), StableMaker().make(uniform)),
                "after reset() the maker behaves like a fresh one");
    }

    void testPersistence() {
        ofPixels rectangle = rectangleFrame(background, foreground);
        ofPixels uniform = solidFrame(160, 120, background);
        Mosaic uniformOnly = StableMaker().make(uniform);

        StableMaker maker; // persistence 2 by default
        ofxTestEq(maker.persistence.get(), 2, "default persistence is 2 Frames");
        Mosaic withRectangle = maker.make(rectangle);
        Mosaic gone1 = maker.make(uniform);
        Mosaic gone2 = maker.make(uniform);
        Mosaic gone3 = maker.make(uniform);
        ofxTest(containsVertices(gone1, withRectangle), "vertices survive 1 Frame after their Contour left");
        ofxTest(containsVertices(gone2, withRectangle), "vertices survive 2 Frames after their Contour left");
        ofxTest(sameMosaic(gone3, uniformOnly), "vertices are dropped after `persistence` Frames");

        StableMaker memoryless;
        memoryless.persistence = 0;
        memoryless.make(rectangle);
        ofxTest(sameMosaic(memoryless.make(uniform), uniformOnly),
                "with persistence 0 the Mosaic depends only on the current Frame");
    }

    void testStabilityUnderNoise() {
        // A real camera-like scene that does not move: only sensor noise changes.
        ofPixels scene;
        ofLoadImage(scene, ofFilePath::join(ofFilePath::getCurrentExeDir(), "../fixtures/fingers.png"));
        ofxTest(scene.isAllocated(), "fixture fingers.png loads");
        if (!scene.isAllocated()) return;

        StableMaker maker;
        Mosaic previous = maker.make(withNoise(scene, 0));
        double kept = 0;
        const int frames = 10;
        for (int i = 1; i <= frames; i++) {
            Mosaic current = maker.make(withNoise(scene, i));
            kept += keptVertices(previous, current);
            previous = current;
        }
        kept /= frames;
        ofLogNotice("stability") << ofToString(kept * 100, 1) << "% of vertices kept between noisy Frames";
        ofxTest(kept >= 0.9, "a static noisy scene keeps at least 90% of its vertices Frame to Frame");
    }

    void testSplineMode() {
        ofPixels rectangle = rectangleFrame(background, foreground);
        ofPixels uniform = solidFrame(160, 120, background);
        MosaicMaker spline;
        ofxTest(!spline.stableVertices, "spline mode is the default");
        Mosaic mosaic = spline.make(rectangle);

        ofxTest(coversFrame(mosaic, 160, 120), "spline mode: Tessels cover the whole Frame");
        bool pixelColours = true;
        for (const Tessel& tessel : mosaic.tessels) {
            glm::vec3 c = centroid(tessel);
            pixelColours = pixelColours && tessel.color == rectangle.getColor(
                ofClamp(int(c.x), 0, 159), ofClamp(int(c.y), 0, 119));
        }
        ofxTest(pixelColours, "spline mode: every Tessel takes the Frame pixel at its centroid");
        ofxTest(!sameMosaic(mosaic, StableMaker().make(rectangle)), "spline and stable modes give different Mosaics");

        spline.make(rectangle);
        MosaicMaker freshSpline;
        ofxTest(sameMosaic(spline.make(uniform), freshSpline.make(uniform)),
                "spline mode: the Mosaic depends only on the current Frame");
    }

    void testContourAreaFilter() {
        ofPixels frame = rectangleFrame(background, foreground); // 80x60 rectangle
        StableMaker maker;
        maker.minArea = 10000;
        Mosaic mosaic = maker.make(frame);
        ofxTest(mosaic.contours.empty(), "Contours smaller than min area are ignored");
        ofxTest(coversFrame(mosaic, 160, 120), "Frame without Contours is still covered");
    }

    void testPixelFormats() {
        ofPixels rgb = rectangleFrame(background, foreground);
        ofPixels rgba = rgb;
        rgba.setImageType(OF_IMAGE_COLOR_ALPHA);
        ofxTest(sameMosaic(StableMaker().make(rgb), StableMaker().make(rgba)),
                "RGBA Frame gives the same Mosaic as RGB");

        ofPixels gray = rgb;
        gray.setImageType(OF_IMAGE_GRAYSCALE);
        ofPixels grayAsRgb = gray;
        grayAsRgb.setImageType(OF_IMAGE_COLOR);
        Mosaic fromGray = StableMaker().make(gray);
        ofxTest(!fromGray.tessels.empty(), "grayscale Frame has Tessels");
        ofxTest(sameMosaic(fromGray, StableMaker().make(grayAsRgb)),
                "grayscale Frame gives the same Mosaic as its RGB equivalent");

        bool allGray = true;
        for (const Tessel& tessel : fromGray.tessels) {
            allGray = allGray && tessel.color.r == tessel.color.g && tessel.color.g == tessel.color.b;
        }
        ofxTest(allGray, "grayscale Frame gives gray Tessels");
    }

    void testToMesh() {
        Mosaic mosaic = StableMaker().make(rectangleFrame(background, foreground));
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
