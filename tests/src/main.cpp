#include "ofMain.h"
#include "ofAppNoWindow.h"
#include "ofxUnitTests.h"
#include "Mosaic.h"
#include "CommandLine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
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

// Triangulates `points` with ofxDelaunay and checks that the triangles tile
// the width x height rectangle exactly: every pixel's sample point (off the
// half-pixel grid, at offsets an edge between half-pixel points is unlikely
// to pass through) lies in exactly one triangle.
bool delaunayTilesRectangle(const std::vector<glm::vec2>& points, int width, int height) {
    ofxDelaunay delaunay;
    for (const glm::vec2& p : points) delaunay.addPoint(ofPoint(p.x, p.y));
    delaunay.triangulate();
    const ofMesh& mesh = delaunay.triangleMesh;
    std::vector<int> cover(width * height, 0);
    for (std::size_t i = 0; i + 2 < mesh.getNumIndices(); i += 3) {
        glm::dvec2 a(mesh.getVertex(mesh.getIndex(i))), b(mesh.getVertex(mesh.getIndex(i + 1))),
            c(mesh.getVertex(mesh.getIndex(i + 2)));
        auto side = [](glm::dvec2 p, glm::dvec2 q, glm::dvec2 r) {
            return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        };
        if (side(a, b, c) < 0) std::swap(b, c);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                glm::dvec2 s(x + 0.3713137, y + 0.6180339);
                if (side(a, b, s) > 0 && side(b, c, s) > 0 && side(c, a, s) > 0) cover[y * width + x]++;
            }
        }
    }
    int gaps = 0, overlaps = 0;
    for (int n : cover) {
        if (n == 0) gaps++;
        if (n > 1) overlaps++;
    }
    if (gaps || overlaps) {
        ofLogError("delaunayTilesRectangle") << points.size() << " points: " << gaps << " uncovered, "
                                             << overlaps << " overlapped pixels";
    }
    return gaps == 0 && overlaps == 0;
}

// The corners of a width x height rectangle, plus `extra`.
std::vector<glm::vec2> withCorners(int width, int height, std::vector<glm::vec2> extra) {
    std::vector<glm::vec2> points = {{0, 0}, {width, 0}, {0, height}, {width, height}};
    points.insert(points.end(), extra.begin(), extra.end());
    return points;
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

// FNV-1a over the Tessels in order: each vertex's x and y as IEEE float
// bits (little-endian), then the colour's r, g, b bytes.
uint64_t mosaicHash(const Mosaic& mosaic) {
    uint64_t h = 14695981039346656037ull;
    auto byte = [&](uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    auto word = [&](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        for (int i = 0; i < 4; i++) byte(uint8_t(bits >> (8 * i)));
    };
    for (const Tessel& tessel : mosaic.tessels) {
        for (const glm::vec3& v : tessel.triangle) {
            word(v.x);
            word(v.y);
        }
        byte(tessel.color.r);
        byte(tessel.color.g);
        byte(tessel.color.b);
    }
    return h;
}

ofPixels loadFixture(const std::string& name) {
    ofPixels pixels;
    ofLoadImage(pixels, ofFilePath::join(ofFilePath::getCurrentExeDir(), "../fixtures/" + name));
    return pixels;
}

} // namespace

// The tests below that are not about spline mode exercise stable mode, which
// is not the default.
struct StableMaker : MosaicMaker {
    StableMaker() { vertexMode = VERTEX_MODE_STABLE; }
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
        testSplineGolden();
        testVertexMode();
        testParametersPerMode();
        testContourAreaFilter();
        testPixelFormats();
        testToMesh();
        testDelaunayKeepsFlatBorderTriangles();
        testDelaunayTilesRandomPointSets();
        testCommandLineWithoutArguments();
        testCommandLineWithoutMode();
        testCommandLineModes();
        testCommandLineErrors();
    }

    CommandLine parse(const std::vector<std::string>& args) {
        return parseCommandLine(args, vertexModeNames);
    }

    void testCommandLineWithoutArguments() {
        CommandLine none = parse({});
        ofxTestEq(int(none.action), int(CommandLine::WINDOW), "no arguments play in a window");
        ofxTestEq(none.exitCode(), 0, "no arguments are not an error");
        ofxTestEq(int(parse({"--help"}).action), int(CommandLine::HELP), "--help asks for the usage");
        ofxTestEq(int(parse({"-h"}).action), int(CommandLine::HELP), "-h asks for the usage");
        ofxTestEq(parse({"-h"}).exitCode(), 0, "the usage exits with 0");
    }

    void testCommandLineWithoutMode() {
        CommandLine image = parse({"dir/photo.jpg"});
        ofxTestEq(int(image.action), int(CommandLine::STILL), "an image gives still mode");
        ofxTest(!image.mode.has_value(), "without --mode no mode is chosen");
        ofxTestEq(image.image, std::string("dir/photo.jpg"), "the image is the first argument");
        ofxTestEq(image.output, std::string("dir/photo-mosaic.png"), "the output defaults to IMAGE-mosaic.png next to it");

        CommandLine output = parse({"photo.jpg", "out.png"});
        ofxTestEq(int(output.action), int(CommandLine::STILL), "image and output give still mode");
        ofxTestEq(output.output, std::string("out.png"), "the output is the second argument");
        ofxTest(!output.mode.has_value(), "image and output without --mode choose no mode");
    }

    void testCommandLineModes() {
        for (int mode = 0; mode < VERTEX_MODE_COUNT; mode++) {
            std::string name = vertexModeNames[mode];
            CommandLine before = parse({"--mode", name, "photo.jpg", "out.png"});
            ofxTestEq(int(before.action), int(CommandLine::STILL), "--mode " + name + " gives still mode");
            ofxTestEq(before.mode.value_or(-1), mode, "--mode " + name + " chooses mode " + ofToString(mode));
            ofxTestEq(before.image, std::string("photo.jpg"), "--mode " + name + " keeps the image");
            ofxTestEq(before.output, std::string("out.png"), "--mode " + name + " keeps the output");

            CommandLine after = parse({"photo.jpg", "--mode", name});
            ofxTestEq(after.mode.value_or(-1), mode, "--mode " + name + " after the image chooses it too");
            ofxTestEq(after.output, std::string("photo-mosaic.png"), "--mode after the image is not the output");
        }

        // A mode is accepted as soon as it is in the list of names.
        std::array<const char*, 3> withAdaptive = {"spline", "stable", "adaptive"};
        CommandLine adaptive = parseCommandLine({"--mode", "adaptive", "photo.jpg"}, withAdaptive);
        ofxTestEq(adaptive.mode.value_or(-1), 2, "--mode adaptive chooses the third listed mode");
        ofxTestEq(int(parse({"--mode", "adaptive", "photo.jpg"}).action), int(CommandLine::ERROR),
                  "--mode adaptive is unknown while adaptive is not a vertex mode");
    }

    void testCommandLineErrors() {
        auto isError = [this](const std::vector<std::string>& args, const std::string& what) {
            CommandLine result = parse(args);
            ofxTestEq(int(result.action), int(CommandLine::ERROR), what + " is an error");
            ofxTestEq(result.exitCode(), 2, what + " exits with 2");
            ofxTest(!result.error.empty(), what + " says what is wrong");
        };
        isError({"--mode", "bogus", "photo.jpg"}, "an unknown mode");
        isError({"--mode", "Spline", "photo.jpg"}, "a mode name in the wrong case");
        isError({"--mode"}, "--mode without a value or image");
        isError({"photo.jpg", "--mode"}, "--mode without a value");
        isError({"--mode", "stable"}, "--mode without an image");
        isError({"photo.jpg", "out.png", "extra.png"}, "an extra argument");
        isError({"--mode", "stable", "photo.jpg", "out.png", "extra.png"}, "an extra argument after --mode");
        isError({"--mode", "stable", "--mode", "spline", "photo.jpg"}, "--mode given twice");
        isError({"--bogus", "photo.jpg"}, "an unknown option");
        isError({""}, "an empty image name");
    }

    // A point half a pixel from the border makes a flat triangle whose
    // circumcircle is far larger than the Frame.
    void testDelaunayKeepsFlatBorderTriangles() {
        ofxTest(delaunayTilesRectangle(withCorners(160, 120, {{67, 119.5}}), 160, 120),
                "Delaunay keeps the flat triangle along the border");
    }

    // Seeded point sets on the half-pixel grid, like the Mosaic's, with
    // points on the border and many equal x coordinates.
    void testDelaunayTilesRandomPointSets() {
        std::mt19937 rng(17);
        int failures = 0;
        for (int set = 0; set < 200; set++) {
            int columns = 2 + set % 40;
            std::uniform_int_distribution<int> column(0, columns), row(0, 240), count(1, 150);
            std::vector<glm::vec2> extra;
            for (int i = count(rng); i > 0; i--) {
                float x = std::round(column(rng) * 320.f / columns) / 2.f;
                extra.push_back({x, row(rng) / 2.f});
            }
            std::sort(extra.begin(), extra.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            extra.erase(std::unique(extra.begin(), extra.end()), extra.end());
            std::shuffle(extra.begin(), extra.end(), rng);
            std::vector<glm::vec2> points = withCorners(160, 120, {});
            for (glm::vec2 p : extra) {
                if (std::find(points.begin(), points.end(), p) == points.end()) points.push_back(p);
            }
            if (!delaunayTilesRectangle(points, 160, 120)) failures++;
        }
        ofxTestEq(failures, 0, "Delaunay tiles the rectangle for 200 random point sets");
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
        ofxTestEq(spline.vertexMode.get(), int(VERTEX_MODE_SPLINE), "spline mode is the default");
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

    // Spline mode must keep reproducing its output exactly. The hash was
    // recorded from the spline Mosaic of fingers.png before vertex mode
    // replaced stable vertices (#27); a change here changes the look.
    void testSplineGolden() {
        ofPixels scene = loadFixture("fingers.png");
        ofxTest(scene.isAllocated(), "fixture fingers.png loads");
        if (!scene.isAllocated()) return;

        Mosaic mosaic = MosaicMaker().make(scene);
        const uint64_t hash = mosaicHash(mosaic);
        ofLogNotice("golden") << mosaic.tessels.size() << " Tessels, hash 0x" << ofToHex(hash);
        ofxTestEq(hash, uint64_t(0x6fa697f1ffa56c72ull), "spline Mosaic of fingers.png matches the golden hash");
    }

    void testVertexMode() {
        MosaicMaker maker;
        ofxTestEq(maker.vertexMode.getMin(), 0, "vertex mode starts at 0");
        ofxTestEq(maker.vertexMode.getMax(), VERTEX_MODE_COUNT - 1, "vertex mode ends at the last mode");
        ofxTestEq(std::string(vertexModeNames[VERTEX_MODE_SPLINE]), std::string("spline"), "mode 0 is spline");
        ofxTestEq(std::string(vertexModeNames[VERTEX_MODE_STABLE]), std::string("stable"), "mode 1 is stable");

        ofxTestEq(nextVertexMode(VERTEX_MODE_SPLINE), int(VERTEX_MODE_STABLE), "spline is followed by stable");
        ofxTestEq(nextVertexMode(VERTEX_MODE_COUNT - 1), int(VERTEX_MODE_SPLINE), "the last mode wraps to spline");

        ofXml xml;
        xml.parse("<mosaic><vertex_mode>1</vertex_mode></mosaic>");
        ofDeserialize(xml, maker.parameters);
        ofxTestEq(maker.vertexMode.get(), int(VERTEX_MODE_STABLE), "vertex_mode in settings selects the mode");

        MosaicMaker old;
        xml.parse("<mosaic><stable_vertices>1</stable_vertices></mosaic>");
        ofDeserialize(xml, old.parameters);
        ofxTestEq(old.vertexMode.get(), int(VERTEX_MODE_SPLINE), "an old stable_vertices tag is ignored");
    }

    static std::vector<std::string> names(const std::vector<std::shared_ptr<ofAbstractParameter>>& parameters) {
        std::vector<std::string> result;
        for (const auto& parameter : parameters) result.push_back(parameter->getName());
        return result;
    }

    void testParametersPerMode() {
        MosaicMaker maker;
        const std::vector<std::string> spline = {
            "vertex mode", "first threshold", "second threshold", "blur iteration",
            "interpolation resolution", "min area", "max area", "find holes"};
        const std::vector<std::string> stable = {
            "vertex mode", "first threshold", "second threshold", "blur iteration",
            "cell size", "persistence", "min area", "max area", "find holes"};
        ofxTest(names(maker.parametersFor(VERTEX_MODE_SPLINE)) == spline,
                "spline mode shows its parameters, without cell size and persistence");
        ofxTest(names(maker.parametersFor(VERTEX_MODE_STABLE)) == stable,
                "stable mode shows its parameters, without interpolation resolution");

        auto shown = maker.parametersFor(VERTEX_MODE_STABLE);
        ofxTest(shown[0]->isReferenceTo(maker.vertexMode), "parametersFor() gives the maker's own vertex mode");

        bool allInGroup = true;
        for (int mode = 0; mode < VERTEX_MODE_COUNT; mode++) {
            for (const auto& parameter : maker.parametersFor(mode)) {
                bool found = false;
                for (const auto& own : maker.parameters) found = found || own->isReferenceTo(*parameter);
                allInGroup = allInGroup && found;
            }
        }
        ofxTest(allInGroup, "every shown parameter is one of the maker's parameters");
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
