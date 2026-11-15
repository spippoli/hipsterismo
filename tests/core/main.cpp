// Tests of the openFrameworks-free adaptive core (src/mosaic/core/), built
// with plain CMake: see CMakeLists.txt. The exit code is the number of
// failed checks. An optional argument names the fixtures folder (default
// tests/fixtures/ of the source tree). Every hash is printed as a "hash <fixture> 0x…" line, so
// the outputs of two builds (GCC and clang, desktop and phone) can be
// diffed.

#include "AdaptiveCore.h"
#include "Png.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mosaic::core;

namespace {

int failures = 0, checks = 0;
std::string fixtures = FIXTURES; // the folder of the PNG fixtures, or argv[1]

void check(bool ok, const std::string& what) {
    checks++;
    if (!ok) failures++;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

std::string hex(uint64_t value) {
    char buffer[19];
    std::snprintf(buffer, sizeof buffer, "0x%016llx", (unsigned long long)value);
    return buffer;
}

RgbImage view(const RgbPixels& pixels) {
    return {pixels.data.data(), pixels.width, pixels.height, pixels.width * 3};
}

RgbPixels solid(int width, int height, Rgb colour) {
    RgbPixels pixels{width, height, std::vector<uint8_t>(size_t(width) * height * 3)};
    for (size_t i = 0; i < pixels.data.size(); i += 3) {
        pixels.data[i] = colour.r;
        pixels.data[i + 1] = colour.g;
        pixels.data[i + 2] = colour.b;
    }
    return pixels;
}

double signedArea(const Tessel& t) {
    const Point &a = t.triangle[0], &b = t.triangle[1], &c = t.triangle[2];
    return 0.5 * ((double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x));
}

// The Tessels tile the Frame: none has zero area, all lie inside it, and
// their areas add up to the Frame's (they cannot overlap without exceeding it,
// since every vertex is a lattice point and CDT gives a proper triangulation).
bool tiles(const Mosaic& mosaic, int width, int height) {
    double total = 0;
    for (const Tessel& t : mosaic.tessels) {
        const double area = std::abs(signedArea(t));
        if (area == 0) return false;
        for (const Point& p : t.triangle) {
            if (p.x < 0 || p.y < 0 || p.x > width || p.y > height) return false;
        }
        total += area;
    }
    return std::abs(total - double(width) * height) < 1e-6;
}

// Every Patch is 4-connected and has at least `minArea` pixels.
bool patchesWhole(const Segmentation& s, int width, int height, int minArea, std::string& why) {
    if (int(s.label.size()) != width * height) {
        why = "label count";
        return false;
    }
    std::vector<int> size(s.count, 0), pieces(s.count, 0);
    std::vector<char> seen(s.label.size(), 0);
    std::vector<int> stack;
    for (int start = 0; start < width * height; start++) {
        const int l = s.label[start];
        if (l < 0 || l >= s.count) {
            why = "label out of range";
            return false;
        }
        if (seen[start]) continue;
        pieces[l]++;
        seen[start] = 1;
        stack.push_back(start);
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            size[l]++;
            const int x = i % width, y = i / width;
            const int next[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
            for (const auto& n : next) {
                if (n[0] < 0 || n[1] < 0 || n[0] >= width || n[1] >= height) continue;
                const int j = n[1] * width + n[0];
                if (!seen[j] && s.label[j] == l) {
                    seen[j] = 1;
                    stack.push_back(j);
                }
            }
        }
    }
    for (int l = 0; l < s.count; l++) {
        if (pieces[l] != 1) {
            why = "Patch " + std::to_string(l) + " has " + std::to_string(pieces[l]) + " pieces";
            return false;
        }
        if (size[l] < minArea && s.count > 1) {
            why = "Patch " + std::to_string(l) + " has " + std::to_string(size[l]) + " px";
            return false;
        }
    }
    return true;
}

void testSizes() {
    const Sizes reference = sizesFor(320, 240, 32);
    check(reference.minPatchArea == 32 && reference.blurSigma == blurSigma
              && reference.boundaryEpsilon == boundaryEpsilon,
          "sizes: 320x240 keeps the constants exactly");
    check(sizesFor(240, 320, 77).minPatchArea == 77 && sizesFor(240, 320, 77).boundaryEpsilon == 3.0,
          "sizes: 240x320 keeps them too");
    const Sizes big = sizesFor(1280, 960, 32);
    check(big.minPatchArea == 512 && big.blurSigma == 3.2f && big.boundaryEpsilon == 12.0,
          "sizes: 1280x960 has 16x the area and 4x sigma and epsilon");
    const Sizes strip = sizesFor(320, 120, 32);
    check(strip.minPatchArea == 16 && strip.boundaryEpsilon == 1.5 && strip.blurSigma == 0.4f,
          "sizes: 320x120 scales the area, and sigma and epsilon by its shorter side");
    check(sizesFor(1920, 1080, 32).minPatchArea == 864, "sizes: 1920x1080 has 27x the min patch area");
    check(sizesFor(160, 120, 9).minPatchArea == 2, "sizes: the min patch area is rounded to the nearest");
    check(sizesFor(37, 23, 8).minPatchArea == 1, "sizes: the min patch area is at least 1");
}

void testEmptyFrame() {
    check(makeAdaptive(RgbImage{}, 32).tessels.empty(), "an empty Frame gives an empty Mosaic");
}

void testUniformFrame() {
    const RgbPixels frame = solid(64, 48, {40, 80, 160});
    const Mosaic mosaic = makeAdaptive(view(frame), 32);
    check(tiles(mosaic, 64, 48), "a uniform Frame is tiled");
    check(mosaic.contours.empty(), "a uniform Frame has no Contours");
    bool colours = true;
    for (const Tessel& t : mosaic.tessels) colours = colours && t.color.r == 40 && t.color.g == 80 && t.color.b == 160;
    check(colours, "a uniform Frame's Tessels have its colour");
    check(segment(view(frame), 32).count == 1, "a uniform Frame is one Patch");
}

// A padded stride gives the same Mosaic as the packed rows.
void testStride(const RgbPixels& frame) {
    const int stride = frame.width * 3 + 7;
    std::vector<uint8_t> padded(size_t(stride) * frame.height, 0xab);
    for (int y = 0; y < frame.height; y++) {
        std::copy_n(&frame.data[size_t(y) * frame.width * 3], frame.width * 3, &padded[size_t(y) * stride]);
    }
    const RgbImage image{padded.data(), frame.width, frame.height, stride};
    check(hash(makeAdaptive(image, 32)) == hash(makeAdaptive(view(frame), 32)),
          "a padded stride gives the same Mosaic");
}

struct Fixture {
    const char* name;
    uint64_t golden;
};

void testFixture(const Fixture& fixture) {
    const std::string name = fixture.name;
    RgbPixels frame;
    std::string error;
    if (!loadPng(fixtures + "/" + name, frame, error)) {
        check(false, name + " loads: " + error);
        return;
    }
    int warnings = 0;
    setLogHook([&](LogLevel, const std::string& message) {
        warnings++;
        std::printf("     log: %s\n", message.c_str());
    });
    const Mosaic mosaic = makeAdaptive(view(frame), 32);
    setLogHook(nullptr);
    const uint64_t h = hash(mosaic);
    const Segmentation patches = segment(view(frame), 32);
    std::printf("hash %s %s (%zu Tessels, %zu Contours, %d Patches)\n", name.c_str(), hex(h).c_str(),
                mosaic.tessels.size(), mosaic.contours.size(), patches.count);

    check(h == fixture.golden, name + ": matches the golden hash " + hex(fixture.golden));
    check(tiles(mosaic, frame.width, frame.height), name + ": the Tessels tile the Frame");
    check(warnings == 0, name + ": the constrained triangulation logs no warning");
    std::string why;
    const int minArea = sizesFor(frame.width, frame.height, 32).minPatchArea;
    check(patchesWhole(patches, frame.width, frame.height, minArea, why),
          name + ": every Patch is 4-connected and reaches the min" + (why.empty() ? "" : " (" + why + ")"));
    check(hash(makeAdaptive(view(frame), 32)) == h, name + ": the Mosaic is a pure function of the Frame");
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) fixtures = argv[1];
    testSizes();
    testEmptyFrame();
    testUniformFrame();
    // The adaptive goldens at min patch area 32. fingers.png's is the
    // openFrameworks suite's adaptive golden too.
    const Fixture goldens[] = {
        {"fingers.png", 0xe0d145733a499498ull},
        {"fingers-003.png", 0xf64474efb9eefad3ull},
        {"fingers-060.png", 0x3d0b480c385887c9ull},
    };
    for (const Fixture& fixture : goldens) testFixture(fixture);
    RgbPixels fingers;
    std::string error;
    if (loadPng(fixtures + "/fingers.png", fingers, error)) {
        testStride(fingers);
    } else {
        check(false, "fingers.png loads for the stride test: " + error);
    }
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures;
}
