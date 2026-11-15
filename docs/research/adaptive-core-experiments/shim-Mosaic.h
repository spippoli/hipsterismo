#pragma once
// Throwaway shim: just enough of OF for the unmodified Adaptive.cpp, with no
// openFrameworks header. glm is the standalone header-only library.
#include <algorithm>
#include <array>
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

// A view of a tightly packed 8-bit RGB buffer.
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
