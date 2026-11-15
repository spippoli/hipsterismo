#include "ofMain.h"
#include "ofAppNoWindow.h"
#include "ofxUnitTests.h"
#include "Mosaic.h"
#include "Adaptive.h"
#include "CommandLine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <numeric>
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

// Adaptive is the default vertex mode, so tests of another mode set it
// explicitly.
struct SplineMaker : MosaicMaker {
    SplineMaker() { vertexMode = VERTEX_MODE_SPLINE; }
};

struct StableMaker : MosaicMaker {
    StableMaker() { vertexMode = VERTEX_MODE_STABLE; }
};

struct AdaptiveMaker : MosaicMaker {
    explicit AdaptiveMaker(int minPatch = 32) {
        vertexMode = VERTEX_MODE_ADAPTIVE;
        minPatchArea = minPatch;
    }
};

namespace {

// Left half `left`, right half (x >= 80) `right`.
ofPixels halvesFrame(const ofColor& left, const ofColor& right) {
    ofPixels frame = solidFrame(160, 120, left);
    for (int y = 0; y < 120; y++) {
        for (int x = 80; x < 160; x++) frame.setColor(x, y, right);
    }
    return frame;
}

// A 4x4 square of `fg`, from (78, 58) to (82, 62), in the middle of `bg`.
ofPixels smallSquareFrame(const ofColor& bg, const ofColor& fg) {
    ofPixels frame = solidFrame(160, 120, bg);
    for (int y = 58; y < 62; y++) {
        for (int x = 78; x < 82; x++) frame.setColor(x, y, fg);
    }
    return frame;
}

// The segments of a Contour, including the closing one of a closed Contour.
std::vector<std::pair<glm::vec2, glm::vec2>> segments(const ofPolyline& contour) {
    std::vector<std::pair<glm::vec2, glm::vec2>> result;
    const auto& v = contour.getVertices();
    for (size_t i = 0; i + 1 < v.size(); i++) result.push_back({v[i], v[i + 1]});
    if (contour.isClosed() && v.size() > 2) result.push_back({v.back(), v.front()});
    return result;
}

// A segment lying on the border line x = 0, x = width, y = 0 or y = height.
bool onBorder(glm::vec2 a, glm::vec2 b, float width, float height) {
    return (a.x == b.x && (a.x == 0 || a.x == width)) || (a.y == b.y && (a.y == 0 || a.y == height));
}

bool anyContourOnBorder(const Mosaic& mosaic, float width, float height) {
    for (const ofPolyline& contour : mosaic.contours) {
        for (auto [a, b] : segments(contour)) {
            if (onBorder(a, b, width, height)) return true;
        }
    }
    return false;
}

// Whether segments ab and cd cross at a point inside both (touching at an
// end point or along a shared line does not count).
bool properlyCross(glm::dvec2 a, glm::dvec2 b, glm::dvec2 c, glm::dvec2 d) {
    auto side = [](glm::dvec2 p, glm::dvec2 q, glm::dvec2 r) {
        double v = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        return (v > 0) - (v < 0);
    };
    return side(a, b, c) * side(a, b, d) < 0 && side(c, d, a) * side(c, d, b) < 0;
}

// How many (Tessel edge, Contour segment) pairs properly cross.
int contourCrossings(const Mosaic& mosaic) {
    int crossings = 0;
    for (const Tessel& tessel : mosaic.tessels) {
        for (int e = 0; e < 3; e++) {
            glm::dvec2 a(tessel.triangle[e]), b(tessel.triangle[(e + 1) % 3]);
            for (const ofPolyline& contour : mosaic.contours) {
                for (auto [c, d] : segments(contour)) crossings += properlyCross(a, b, c, d);
            }
        }
    }
    return crossings;
}

// How many Patches a Mosaic shows: Tessels joined across every shared edge
// that is not a Contour segment, counted as connected groups of at least
// `minArea` px². Adaptive's Tessels never cross a Contour and its Contour
// segments are Tessel edges, so each group is one 4-connected region of a
// Patch. (A Patch whose pixels only touch at a corner counts once per
// region; such regions are often a pixel or two, hence `minArea`.)
int patchCount(const Mosaic& mosaic, double minArea = 0) {
    using Point = std::pair<int, int>;
    using Edge = std::pair<Point, Point>;
    auto point = [](glm::vec2 v) { return Point(int(std::lround(v.x)), int(std::lround(v.y))); };
    auto edge = [&](glm::vec2 a, glm::vec2 b) {
        Point p = point(a), q = point(b);
        return p < q ? Edge(p, q) : Edge(q, p);
    };
    std::set<Edge> walls;
    for (const ofPolyline& contour : mosaic.contours) {
        for (auto [a, b] : segments(contour)) walls.insert(edge(a, b));
    }
    std::vector<int> parent(mosaic.tessels.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    std::map<Edge, int> seen;
    for (size_t t = 0; t < mosaic.tessels.size(); t++) {
        const auto& v = mosaic.tessels[t].triangle;
        for (int e = 0; e < 3; e++) {
            Edge key = edge(v[e], v[(e + 1) % 3]);
            if (walls.count(key)) continue;
            auto [it, fresh] = seen.insert({key, int(t)});
            if (!fresh) parent[find(int(t))] = find(it->second);
        }
    }
    std::map<int, double> groupArea;
    for (size_t t = 0; t < parent.size(); t++) groupArea[find(int(t))] += area(mosaic.tessels[t]);
    int groups = 0;
    for (auto [root, total] : groupArea) groups += total >= minArea;
    return groups;
}

// `top` above the line from (0, 30) to (160, 90), `bottom` below it: a
// slanted Patch boundary, which the pixels turn into a staircase.
ofPixels slantedFrame(const ofColor& top, const ofColor& bottom) {
    ofPixels frame = solidFrame(160, 120, top);
    for (int y = 0; y < 120; y++) {
        for (int x = 0; x < 160; x++) {
            if (8 * (2 * y + 1) > 3 * (2 * x + 1) + 480) frame.setColor(x, y, bottom);
        }
    }
    return frame;
}

// Once installed, counts the warnings and errors the Mosaic module logs (the
// constrained triangulation's fallback logs one), and passes every message on
// to the previous logger channel.
class MosaicWarnings : public ofBaseLoggerChannel {
public:
    static std::shared_ptr<MosaicWarnings> install() {
        auto channel = std::make_shared<MosaicWarnings>();
        channel->previous = ofGetLoggerChannel();
        ofSetLoggerChannel(channel);
        return channel;
    }
    void uninstall() { ofSetLoggerChannel(previous); }
    void log(ofLogLevel level, const std::string& module, const std::string& message) override {
        if (module == "Mosaic" && level >= OF_LOG_WARNING) count++;
        if (previous) previous->log(level, module, message);
    }
    int count = 0;

private:
    std::shared_ptr<ofBaseLoggerChannel> previous;
};

} // namespace

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
        testDefaults();
        testSplineMode();
        testSplineTilesGrayscaleFrame();
        testSplineGolden();
        testVertexMode();
        testParametersPerMode();
        testSettingsRoundTrip();
        testContourAreaFilter();
        testPixelFormats();
        testToMesh();
        testAdaptiveTiling();
        testAdaptiveUniformFrame();
        testAdaptiveHalves();
        testAdaptiveMinPatchArea();
        testAdaptiveNoBorderContour();
        testAdaptiveTesselsDoNotCrossContours();
        testAdaptiveSimplifiesBoundaries();
        testAdaptiveCrossingRepairRegressions();
        testAdaptiveDeterminism();
        testAdaptivePixelFormats();
        testAdaptiveGolden();
        testAdaptiveSizes();
        testAdaptiveScalesWithFrame();
        testDelaunayKeepsFlatBorderTriangles();
        testDelaunayTilesRandomPointSets();
        testDelaunayTilesCocircularClusters();
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

        CommandLine adaptive = parse({"--mode", "adaptive", "photo.jpg"});
        ofxTestEq(adaptive.mode.value_or(-1), int(VERTEX_MODE_ADAPTIVE), "--mode adaptive chooses adaptive");

        // A mode is accepted only when it is in the list of names.
        std::array<const char*, 2> twoModes = {"spline", "stable"};
        ofxTestEq(int(parseCommandLine({"--mode", "adaptive", "photo.jpg"}, twoModes).action),
                  int(CommandLine::ERROR), "--mode adaptive is unknown when adaptive is not listed");
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

    // Dense clusters of half-pixel points, like the spline's samples around
    // a Contour corner: many groups of four or more lie on one circle. The
    // Mosaic's border points (8 segments a side) come first, as in make().
    void testDelaunayTilesCocircularClusters() {
        std::mt19937 rng(39);
        int failures = 0;
        for (int set = 0; set < 200; set++) {
            std::vector<glm::vec2> points;
            for (int i = 0; i < 8; i++) {
                float t = i / 8.f;
                points.insert(points.end(), {{t * 160, 0}, {160, t * 120}, {160 - t * 160, 120}, {0, 120 - t * 120}});
            }
            std::uniform_int_distribution<int> centreX(0, 320), centreY(0, 240), clusters(1, 6), spread(2, 12),
                count(5, 60);
            for (int c = clusters(rng); c > 0; c--) {
                glm::vec2 centre(centreX(rng) / 2.f, centreY(rng) / 2.f);
                int s = spread(rng);
                std::uniform_int_distribution<int> offset(-s, s);
                for (int i = count(rng); i > 0; i--) {
                    // Two statements: the x offset is drawn first on every compiler.
                    float x = ofClamp(centre.x + offset(rng) / 2.f, 0, 160);
                    glm::vec2 p(x, ofClamp(centre.y + offset(rng) / 2.f, 0, 120));
                    if (std::find(points.begin(), points.end(), p) == points.end()) points.push_back(p);
                }
            }
            if (!delaunayTilesRectangle(points, 160, 120)) failures++;
        }
        ofxTestEq(failures, 0, "Delaunay tiles the rectangle for 200 sets of cocircular clusters");
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

    void testDefaults() {
        MosaicMaker fresh;
        ofxTestEq(fresh.vertexMode.get(), int(VERTEX_MODE_ADAPTIVE), "adaptive is the default vertex mode");
        ofxTestEq(fresh.minPatchArea.get(), 32, "adaptive is the default with min patch area 32");
    }

    void testSplineMode() {
        ofPixels rectangle = rectangleFrame(background, foreground);
        ofPixels uniform = solidFrame(160, 120, background);
        SplineMaker spline;
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
        SplineMaker freshSpline;
        ofxTest(sameMosaic(spline.make(uniform), freshSpline.make(uniform)),
                "spline mode: the Mosaic depends only on the current Frame");
    }

    // The grayscale rectangle's Contours give spline samples on one circle
    // (#39): an unpatched in-circle test overlaps Tessels there.
    void testSplineTilesGrayscaleFrame() {
        ofPixels gray = rectangleFrame(background, foreground);
        gray.setImageType(OF_IMAGE_GRAYSCALE);
        ofxTest(coversFrame(SplineMaker().make(gray), 160, 120),
                "spline mode: Tessels of a grayscale Frame tile it without overlaps");
    }

    // Spline mode must keep reproducing its output exactly. The hash was
    // recorded from the spline Mosaic of fingers.png before vertex mode
    // replaced stable vertices (#27); a change here changes the look. It
    // was re-recorded once, for #39: the exact in-circle test picks the
    // other diagonal of one cocircular quad, (143.5,160) (144,160)
    // (143.5,166) (146,165).
    void testSplineGolden() {
        ofPixels scene = loadFixture("fingers.png");
        ofxTest(scene.isAllocated(), "fixture fingers.png loads");
        if (!scene.isAllocated()) return;

        Mosaic mosaic = SplineMaker().make(scene);
        const uint64_t hash = mosaicHash(mosaic);
        ofLogNotice("golden") << mosaic.tessels.size() << " Tessels, hash 0x" << ofToHex(hash);
        ofxTestEq(hash, uint64_t(0xcd1a4ddfcf14321bull), "spline Mosaic of fingers.png matches the golden hash");
    }

    void testVertexMode() {
        MosaicMaker maker;
        ofxTestEq(maker.vertexMode.getMin(), 0, "vertex mode starts at 0");
        ofxTestEq(maker.vertexMode.getMax(), VERTEX_MODE_COUNT - 1, "vertex mode ends at the last mode");
        ofxTestEq(std::string(vertexModeNames[VERTEX_MODE_SPLINE]), std::string("spline"), "mode 0 is spline");
        ofxTestEq(std::string(vertexModeNames[VERTEX_MODE_STABLE]), std::string("stable"), "mode 1 is stable");
        ofxTestEq(std::string(vertexModeNames[VERTEX_MODE_ADAPTIVE]), std::string("adaptive"), "mode 2 is adaptive");
        ofxTestEq(int(VERTEX_MODE_COUNT), 3, "there are three vertex modes");
        ofxTestEq(maker.vertexMode.getMax(), 2, "vertex mode goes up to 2");

        ofxTestEq(nextVertexMode(VERTEX_MODE_SPLINE), int(VERTEX_MODE_STABLE), "spline is followed by stable");
        ofxTestEq(nextVertexMode(VERTEX_MODE_STABLE), int(VERTEX_MODE_ADAPTIVE), "stable is followed by adaptive");
        ofxTestEq(nextVertexMode(VERTEX_MODE_ADAPTIVE), int(VERTEX_MODE_SPLINE), "adaptive wraps to spline");

        ofXml xml;
        xml.parse("<mosaic><vertex_mode>1</vertex_mode></mosaic>");
        ofDeserialize(xml, maker.parameters);
        ofxTestEq(maker.vertexMode.get(), int(VERTEX_MODE_STABLE), "vertex_mode in settings selects the mode");

        MosaicMaker old;
        xml.parse("<mosaic><stable_vertices>1</stable_vertices></mosaic>");
        ofDeserialize(xml, old.parameters);
        ofxTestEq(old.vertexMode.get(), MosaicMaker().vertexMode.get(), "an old stable_vertices tag is ignored");
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
        const std::vector<std::string> adaptive = {"vertex mode", "min patch area"};
        ofxTest(names(maker.parametersFor(VERTEX_MODE_ADAPTIVE)) == adaptive,
                "adaptive mode shows only vertex mode and min patch area");

        ofxTestEq(maker.minPatchArea.get(), 32, "min patch area defaults to 32");
        ofxTestEq(maker.minPatchArea.getMin(), 8, "min patch area starts at 8");
        ofxTestEq(maker.minPatchArea.getMax(), 1024, "min patch area ends at 1024");

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

    // The app saves and loads the whole group, named "panel", whatever the
    // panel shows.
    void testSettingsRoundTrip() {
        MosaicMaker saved;
        saved.parameters.setName("panel");
        saved.vertexMode = VERTEX_MODE_SPLINE;
        saved.threshold1 = 11;
        saved.cellSize = 17;
        saved.persistence = 7;
        saved.resolution = 1234;
        saved.findHoles = false;
        saved.minPatchArea = 77;
        ofXml xml;
        ofSerialize(xml, saved.parameters);
        ofxTestEq(xml.getChild("panel").getChild("cell_size").getIntValue(), 17,
                  "a parameter hidden in spline mode is saved under <panel>");
        ofxTestEq(xml.getChild("panel").getChild("first_threshold").getIntValue(), 11,
                  "settings keep their <panel><first_threshold> layout");

        MosaicMaker loaded;
        loaded.parameters.setName("panel");
        loaded.vertexMode = VERTEX_MODE_STABLE;
        ofXml reread;
        reread.parse(xml.toString());
        ofDeserialize(reread, loaded.parameters);
        bool same = true;
        for (size_t i = 0; i < saved.parameters.size(); i++) {
            same = same && saved.parameters.get(i).toString() == loaded.parameters.get(i).toString();
        }
        ofxTest(same, "loading restores every parameter, whatever mode was active");
        ofxTestEq(loaded.persistence.get(), 7, "persistence round-trips");
        ofxTestEq(loaded.resolution.get(), 1234, "interpolation resolution round-trips");
        ofxTestEq(xml.getChild("panel").getChild("min_patch_area").getIntValue(), 77,
                  "min patch area is saved as <min_patch_area>");
        ofxTestEq(loaded.minPatchArea.get(), 77, "min patch area round-trips");

        MosaicMaker old;
        old.parameters.setName("panel");
        xml.parse("<panel><first_threshold>3</first_threshold><second_threshold>40</second_threshold>"
                  "<blur_iteration>6</blur_iteration><stable_vertices>1</stable_vertices></panel>");
        ofDeserialize(xml, old.parameters);
        ofxTest(old.threshold1 == 3 && old.threshold2 == 40 && old.blurIterations == 6,
                "a settings.xml from before vertex mode still loads");
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

    // Synthetic Frames plus fingers.png, for the tests that hold on any Frame.
    std::vector<std::pair<std::string, ofPixels>> adaptiveFrames() {
        std::vector<std::pair<std::string, ofPixels>> frames = {
            {"uniform", solidFrame(160, 120, background)},
            {"halves", halvesFrame(background, foreground)},
            {"small square", smallSquareFrame(background, foreground)},
            {"rectangle", rectangleFrame(background, foreground)},
            {"noisy rectangle", withNoise(rectangleFrame(background, foreground), 3)},
            {"odd size", solidFrame(37, 23, foreground)},
        };
        ofPixels nearBorder = solidFrame(160, 120, background);
        for (int y = 6; y < 114; y++) for (int x = 6; x < 154; x++) nearBorder.setColor(x, y, foreground);
        frames.push_back({"near border", nearBorder});
        ofPixels scene = loadFixture("fingers.png");
        if (scene.isAllocated()) frames.push_back({"fingers.png", scene});
        ofPixels frame60 = loadFixture("fingers-060.png");
        if (frame60.isAllocated()) frames.push_back({"fingers-060.png", frame60});
        frames.push_back({"slanted", slantedFrame(background, foreground)});
        return frames;
    }

    void testAdaptiveTiling() {
        for (auto& [name, frame] : adaptiveFrames()) {
            Mosaic mosaic = AdaptiveMaker().make(frame);
            ofLogNotice("adaptive") << name << ": " << mosaic.contours.size() << " Contours, "
                                    << mosaic.tessels.size() << " Tessels";
            ofxTest(coversFrame(mosaic, frame.getWidth(), frame.getHeight()),
                    "adaptive: Tessels tile the " + name + " Frame");
        }

        // Informational only: real time is checked by hand.
        ofPixels scene = loadFixture("fingers.png");
        if (!scene.isAllocated()) return;
        AdaptiveMaker maker;
        const int runs = 10;
        uint64_t start = ofGetElapsedTimeMicros();
        for (int i = 0; i < runs; i++) maker.make(scene);
        ofLogNotice("adaptive") << "fingers.png: " << ofToString((ofGetElapsedTimeMicros() - start) / 1000.0 / runs, 1)
                                << " ms per Frame";
    }

    void testAdaptiveUniformFrame() {
        Mosaic mosaic = AdaptiveMaker().make(solidFrame(160, 120, background));
        ofxTestEq(mosaic.tessels.size(), size_t(2), "adaptive: a uniform Frame gives 2 Tessels");
        ofxTest(mosaic.contours.empty(), "adaptive: a uniform Frame gives no Contours");
        bool allBackground = true;
        for (const Tessel& tessel : mosaic.tessels) allBackground = allBackground && tessel.color == background;
        ofxTest(allBackground, "adaptive: a uniform Frame gives Tessels of its colour");
    }

    void testAdaptiveHalves() {
        Mosaic mosaic = AdaptiveMaker().make(halvesFrame(background, foreground));
        ofxTestEq(mosaic.contours.size(), size_t(1), "adaptive: two halves give one Contour");
        if (mosaic.contours.size() == 1) {
            const ofPolyline& contour = mosaic.contours[0];
            bool vertical = !contour.isClosed() && contour.size() >= 2;
            float top = 120, bottom = 0;
            for (const auto& v : contour.getVertices()) {
                vertical = vertical && v.x == 80;
                top = std::min(top, v.y);
                bottom = std::max(bottom, v.y);
            }
            ofxTest(vertical && top == 0 && bottom == 120,
                    "adaptive: the Contour between two halves is the open segment x = 80");
        }

        bool sided = true, coloured = true;
        for (const Tessel& tessel : mosaic.tessels) {
            bool left = true, right = true;
            for (const glm::vec3& v : tessel.triangle) {
                left = left && v.x <= 80;
                right = right && v.x >= 80;
            }
            sided = sided && (left || right);
            coloured = coloured && tessel.color == (left ? background : foreground);
        }
        ofxTest(sided, "adaptive: every Tessel lies in one half");
        ofxTest(coloured, "adaptive: every Tessel takes exactly its half's colour");
    }

    // min patch area is in px² at 320x240, so on this 160x120 Frame 128
    // means 32 px² and 8 means 2 px².
    void testAdaptiveMinPatchArea() {
        ofPixels frame = smallSquareFrame(background, foreground);
        ofxTest(AdaptiveMaker(128).make(frame).contours.empty(),
                "adaptive: a 4x4 Patch is absorbed at min patch area 128 (32 px² at 160x120)");
        ofxTestEq(AdaptiveMaker(32).make(frame).contours.size(), size_t(1),
                  "adaptive: a 4x4 Patch survives min patch area 32 (8 px² at 160x120)");

        Mosaic kept = AdaptiveMaker(8).make(frame);
        ofxTestEq(kept.contours.size(), size_t(1), "adaptive: a 4x4 Patch has a Contour at min patch area 8");
        if (kept.contours.size() == 1) {
            const ofPolyline& contour = kept.contours[0];
            ofRectangle box = contour.getBoundingBox();
            ofxTest(contour.isClosed(), "adaptive: the Contour around a Patch inside another is closed");
            ofxTest(box.getLeft() <= 78 && box.getRight() >= 82 && box.getTop() <= 58 && box.getBottom() >= 62
                        && box.getWidth() <= 6 && box.getHeight() <= 6,
                    "adaptive: the closed Contour surrounds the 4x4 square");
        }
    }

    void testAdaptiveNoBorderContour() {
        bool none = true;
        for (auto& [name, frame] : adaptiveFrames()) {
            bool onIt = anyContourOnBorder(AdaptiveMaker(8).make(frame), frame.getWidth(), frame.getHeight());
            if (onIt) ofLogError("adaptive") << name << ": a Contour runs along the Frame border";
            none = none && !onIt;
        }
        ofxTest(none, "adaptive: no Contour runs along the Frame border");
    }

    void testAdaptiveTesselsDoNotCrossContours() {
        ofPixels scene = loadFixture("fingers.png");
        if (!scene.isAllocated()) return;
        Mosaic mosaic = AdaptiveMaker().make(scene);
        ofxTest(!mosaic.contours.empty(), "adaptive: fingers.png has Contours");
        ofxTestEq(contourCrossings(mosaic), 0, "adaptive: no Tessel edge crosses a Contour on fingers.png");
    }

    void testAdaptiveSimplifiesBoundaries() {
        Mosaic mosaic = AdaptiveMaker().make(slantedFrame(background, foreground));
        ofxTestEq(mosaic.contours.size(), size_t(1), "adaptive: a slanted boundary gives one Contour");
        if (mosaic.contours.size() == 1) {
            const ofPolyline& contour = mosaic.contours[0];
            ofLogNotice("adaptive") << "slanted boundary: " << contour.size() << " Contour vertices";
            ofxTest(!contour.isClosed() && contour.size() >= 2 && contour.size() <= 4,
                    "adaptive: a slanted staircase boundary is simplified to a few straight segments");
        }
        ofxTestEq(contourCrossings(mosaic), 0, "adaptive: no Tessel edge crosses the slanted Contour");
    }

    // Frames of fingers.mov (0-based) where a faulty crossing repair left
    // conflicting constraints, so CDT threw and make() fell back to an
    // unconstrained triangulation. Frame 60: the prototypes' repair simplified
    // closed loops. Frame 3: falling back to an open chain's two ends instead
    // of its exact corners.
    void testAdaptiveCrossingRepairRegressions() {
        for (std::string name : {"fingers-060.png", "fingers-003.png"}) {
            ofPixels frame = loadFixture(name);
            ofxTest(frame.isAllocated(), "fixture " + name + " loads");
            if (!frame.isAllocated()) continue;
            auto warnings = MosaicWarnings::install();
            Mosaic mosaic;
            bool threw = false;
            try {
                mosaic = AdaptiveMaker().make(frame);
            } catch (const std::exception& e) {
                threw = true;
                ofLogError("adaptive") << name << ": " << e.what();
            }
            warnings->uninstall();
            ofxTest(!threw, "adaptive: make() does not throw on " + name);
            ofxTestEq(warnings->count, 0,
                      "adaptive: " + name + " is triangulated with its Contours as constraints, no fallback");
            ofxTest(coversFrame(mosaic, frame.getWidth(), frame.getHeight()), "adaptive: Tessels tile " + name);
            ofxTestEq(contourCrossings(mosaic), 0, "adaptive: no Tessel edge crosses a Contour on " + name);
        }
    }

    void testAdaptiveDeterminism() {
        ofPixels scene = loadFixture("fingers.png");
        ofPixels rectangle = rectangleFrame(background, foreground);
        ofPixels halves = halvesFrame(background, foreground);
        AdaptiveMaker a;
        Mosaic first = a.make(rectangle);
        a.make(halves);
        if (scene.isAllocated()) a.make(scene);
        ofxTest(sameMosaic(first, a.make(rectangle)),
                "adaptive: the same Frame gives the same Mosaic, with other Frames in between");
        ofxTest(sameMosaic(first, AdaptiveMaker().make(rectangle)), "adaptive: a fresh maker gives the same Mosaic");
        if (scene.isAllocated()) {
            ofxTest(sameMosaic(AdaptiveMaker().make(scene), a.make(scene)),
                    "adaptive: fingers.png gives the same Mosaic every time");
        }
    }

    void testAdaptivePixelFormats() {
        ofPixels rgb = rectangleFrame(background, foreground);
        ofPixels rgba = rgb;
        rgba.setImageType(OF_IMAGE_COLOR_ALPHA);
        Mosaic fromRgb = AdaptiveMaker().make(rgb);
        ofxTest(!fromRgb.tessels.empty() && !fromRgb.contours.empty(),
                "adaptive: an RGB Frame has Tessels and Contours");
        ofxTest(sameMosaic(fromRgb, AdaptiveMaker().make(rgba)),
                "adaptive: an RGBA Frame gives the same Mosaic as RGB");

        ofPixels gray = rgb;
        gray.setImageType(OF_IMAGE_GRAYSCALE);
        ofPixels grayAsRgb = gray;
        grayAsRgb.setImageType(OF_IMAGE_COLOR);
        Mosaic fromGray = AdaptiveMaker().make(gray);
        ofxTest(!fromGray.contours.empty(), "adaptive: a grayscale Frame has Contours");
        ofxTest(sameMosaic(fromGray, AdaptiveMaker().make(grayAsRgb)),
                "adaptive: a grayscale Frame gives the same Mosaic as its RGB equivalent");
        ofxTest(coversFrame(fromGray, 160, 120), "adaptive: a grayscale Frame is tiled");
    }

    // At 320x240 adaptive must keep giving exactly the Mosaic it gave before
    // its size constants were referred to that Frame size (#32).
    void testAdaptiveGolden() {
        ofPixels scene = loadFixture("fingers.png");
        ofxTest(scene.isAllocated(), "fixture fingers.png loads");
        if (!scene.isAllocated()) return;
        Mosaic mosaic = AdaptiveMaker().make(scene);
        const uint64_t hash = mosaicHash(mosaic);
        ofLogNotice("adaptive golden") << mosaic.tessels.size() << " Tessels, " << patchCount(mosaic)
                                       << " Patches, hash 0x" << ofToHex(hash);
        ofxTestEq(hash, uint64_t(0x62e72339903fe092ull), "adaptive Mosaic of fingers.png matches the golden hash");
    }

    // The size constants are referred to 320x240: min patch area scales
    // with the Frame area, σ and ε with the shorter side.
    void testAdaptiveSizes() {
        using adaptive::sizesFor;
        adaptive::Sizes reference = sizesFor(320, 240, 32);
        ofxTest(reference.minPatchArea == 32 && reference.blurSigma == adaptive::blurSigma
                    && reference.boundaryEpsilon == adaptive::boundaryEpsilon,
                "sizes: 320x240 keeps the constants exactly");
        ofxTest(sizesFor(240, 320, 77).minPatchArea == 77 && sizesFor(240, 320, 77).boundaryEpsilon == 3.0,
                "sizes: 240x320 keeps them too");

        adaptive::Sizes big = sizesFor(1280, 960, 32);
        ofxTestEq(big.minPatchArea, 512, "sizes: 1280x960 has 16x the min patch area");
        ofxTestEq(big.blurSigma, 3.2f, "sizes: 1280x960 has 4x the blur sigma");
        ofxTestEq(big.boundaryEpsilon, 12.0, "sizes: 1280x960 has 4x the boundary epsilon");

        adaptive::Sizes strip = sizesFor(320, 120, 32);
        ofxTestEq(strip.minPatchArea, 16, "sizes: 320x120 has half the min patch area");
        ofxTestEq(strip.boundaryEpsilon, 1.5, "sizes: 320x120 scales epsilon by its shorter side");
        ofxTestEq(strip.blurSigma, 0.4f, "sizes: 320x120 scales sigma by its shorter side");

        ofxTestEq(sizesFor(1920, 1080, 32).minPatchArea, 864, "sizes: 1920x1080 has 27x the min patch area");
        ofxTestEq(sizesFor(160, 120, 9).minPatchArea, 2, "sizes: the min patch area is rounded to the nearest");
        ofxTestEq(sizesFor(37, 23, 8).minPatchArea, 1, "sizes: the min patch area is at least 1");
        ofxTest(sizesFor(1, 1, 8).blurSigma > 0 && sizesFor(1, 1, 8).boundaryEpsilon > 0,
                "sizes: sigma and epsilon stay positive on a 1x1 Frame");
    }

    // `frame` resized by `factor` with bilinear interpolation.
    static ofPixels scaled(const ofPixels& frame, int factor) {
        cv::Mat big;
        cv::resize(ofxCv::toCv(frame), big, cv::Size(), factor, factor, cv::INTER_LINEAR);
        ofPixels result;
        ofxCv::toOf(big, result);
        return result;
    }

    // The same picture at a larger size gives about as many Patches: the
    // size constants are referred to 320x240 and scale with the Frame.
    void testAdaptiveScalesWithFrame() {
        ofxTestEq(patchCount(AdaptiveMaker().make(solidFrame(160, 120, background))), 1,
                  "patchCount: a uniform Frame is one Patch");
        ofxTestEq(patchCount(AdaptiveMaker().make(halvesFrame(background, foreground))), 2,
                  "patchCount: two halves are two Patches");

        ofPixels scene = loadFixture("fingers.png");
        if (!scene.isAllocated()) return;
        ofPixels strip = scene;
        strip.crop(0, 60, 320, 120);
        const std::vector<std::pair<std::string, ofPixels>> frames = {{"fingers.png", scene},
                                                                      {"fingers.png 320x120", strip}};
        // Regions under half the min patch area (scaled) are not counted:
        // see patchCount().
        auto counted = [](const Mosaic& mosaic, const ofPixels& frame) {
            const int w = frame.getWidth(), h = frame.getHeight();
            return patchCount(mosaic, adaptive::sizesFor(w, h, 32).minPatchArea / 2.0);
        };
        for (const auto& [name, frame] : frames) {
            Mosaic small = AdaptiveMaker().make(frame);
            const int reference = counted(small, frame);
            ofPixels big = scaled(frame, 4);
            uint64_t start = ofGetElapsedTimeMicros();
            Mosaic mosaic = AdaptiveMaker().make(big);
            const double ms = (ofGetElapsedTimeMicros() - start) / 1000.0;
            const int patches = counted(mosaic, big);
            ofLogNotice("adaptive") << name << ": " << reference << " Patches (" << patchCount(small)
                                    << " regions), " << small.tessels.size() << " Tessels at " << frame.getWidth()
                                    << "x" << frame.getHeight() << "; " << patches << " Patches ("
                                    << patchCount(mosaic) << " regions), " << mosaic.tessels.size()
                                    << " Tessels at " << big.getWidth() << "x" << big.getHeight() << " in "
                                    << ofToString(ms, 0) << " ms";
            // Measured: +21% on fingers.png and +33% on the strip (the
            // upscaled Frame is smoother, and FH's k is not scaled); with
            // the absolute constants it was about 10x.
            ofxTest(std::abs(patches - reference) <= reference * 2 / 5,
                    "adaptive: " + name + " upscaled x4 gives about as many Patches, within 40%");
            ofxTest(coversFrame(mosaic, big.getWidth(), big.getHeight()), "adaptive: Tessels tile " + name + " x4");
            ofxTestEq(contourCrossings(mosaic), 0, "adaptive: no Tessel edge crosses a Contour on " + name + " x4");
        }
    }
};

int main() {
    ofInit();
    auto window = std::make_shared<ofAppNoWindow>();
    auto app = std::make_shared<ofApp>();
    ofRunApp(window, app);
    return ofRunMainLoop();
}
