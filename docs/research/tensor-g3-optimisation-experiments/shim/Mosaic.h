#pragma once
// Throwaway shim (after docs/research/adaptive-core-experiments/shim-Mosaic.h on
// research/adaptive-core): just enough of OF for Adaptive.cpp, plus per-stage
// timing hooks used by the instrumented copy made by instrument.py.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

struct ofColor {
    unsigned char r = 0, g = 0, b = 0;
    ofColor() = default;
    ofColor(int r_, int g_, int b_) : r(r_), g(g_), b(b_) {}
};

struct ofPixels {
    unsigned char* data;
    int width, height;
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    const unsigned char* getData() const { return data; }
    ofColor getColor(int x, int y) const {
        const unsigned char* p = data + (size_t(y) * width + x) * 3;
        return {p[0], p[1], p[2]};
    }
};

struct ofPolyline {
    std::vector<glm::vec3> v;
    bool closed = false;
    void addVertex(float x, float y) { v.push_back({x, y, 0}); }
    void setClosed(bool c) { closed = c; }
};

template <typename T> T ofClamp(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }
inline std::ostream& ofLogWarning(const char*) { return std::cerr; }
inline std::ostream& ofLogError(const char*) { return std::cerr; }

namespace ofxCv {
inline cv::Mat toCv(const ofPixels& p) { return cv::Mat(p.height, p.width, CV_8UC3, const_cast<unsigned char*>(p.data)); }
}

struct Tessel {
    std::array<glm::vec3, 3> triangle;
    ofColor color;
};

struct Mosaic {
    std::vector<Tessel> tessels;
    std::vector<ofPolyline> contours;
};

// ------------------------------------------------------------ timing hooks
enum Stage { S_LAB, S_BLUR, S_EDGES, S_SORT, S_UF, S_JOIN, S_WARD1, S_SPLIT, S_WARD2, S_TRACE, S_SIMPL, S_CDT, S_COLOUR, S_N };
inline const char* stageName[S_N] = {"lab", "blur", "fh edges", "fh sort", "fh union-find", "equal join", "ward 1",
                                      "piece split", "ward 2", "trace chains", "simplify+repair", "cdt", "tessel colour"};
inline double g_t[S_N];
// 0 FH Patches, 1 after join, 2 pieces, 3 chains, 4 Tessels, 5 repair rounds
inline long g_count[8];
struct Tick {
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    void lap(Stage s) {
        auto n = std::chrono::steady_clock::now();
        g_t[s] += std::chrono::duration<double, std::milli>(n - t).count();
        t = n;
    }
};
