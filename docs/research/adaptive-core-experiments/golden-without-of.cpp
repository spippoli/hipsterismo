// Throwaway: the adaptive golden hash, with no openFrameworks in the process.
#include <cstdint>
#include <cstring>
#include <opencv2/imgcodecs.hpp>
#include "Adaptive.h"

static uint64_t mosaicHash(const Mosaic& mosaic) { // as in tests/src/main.cpp
    uint64_t h = 14695981039346656037ull;
    auto byte = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    auto word = [&](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        for (int i = 0; i < 4; i++) byte(uint8_t(bits >> (8 * i)));
    };
    for (const Tessel& t : mosaic.tessels) {
        for (const glm::vec3& v : t.triangle) { word(v.x); word(v.y); }
        byte(t.color.r); byte(t.color.g); byte(t.color.b);
    }
    return h;
}

int main(int argc, char** argv) {
    cv::Mat bgr = cv::imread(argv[1], cv::IMREAD_COLOR), rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    ofPixels frame{rgb.data, rgb.cols, rgb.rows};
    Mosaic m = adaptive::make(frame, 32);
    std::printf("%zu Tessels, %zu Contours, hash 0x%016llx\n", m.tessels.size(), m.contours.size(),
                (unsigned long long)mosaicHash(m));
}
