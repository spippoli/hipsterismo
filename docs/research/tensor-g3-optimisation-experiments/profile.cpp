// Throwaway: per-stage timing of adaptive::make on one or more images.
// usage: profile [-l LONGSIDE] [-r REPS] [-a MINAREA] IMAGE...
// Each image is optionally resized (INTER_AREA, or INTER_CUBIC when growing)
// so its long side is LONGSIDE, run once to warm up and then REPS times. Prints
// the hash of the last run (same FNV-1a as tests/src/main.cpp), the counts and
// the median ms per stage over all images and reps (medians resist other load
// on the machine; the total is the sum of the stage medians).
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <opencv2/imgcodecs.hpp>
#include "Adaptive.h"

static uint64_t mosaicHash(const Mosaic& mosaic) {
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
    int longSide = 0, reps = 20, minArea = 32, first = 1;
    for (; first + 1 < argc && argv[first][0] == '-'; first += 2) {
        int v = std::atoi(argv[first + 1]);
        if (argv[first][1] == 'l') longSide = v;
        if (argv[first][1] == 'r') reps = v;
        if (argv[first][1] == 'a') minArea = v;
    }
    int runs = 0;
    for (int i = first; i < argc; i++) {
        cv::Mat bgr = cv::imread(argv[i], cv::IMREAD_COLOR), rgb;
        if (bgr.empty()) { std::fprintf(stderr, "can't load %s\n", argv[i]); return 1; }
        if (longSide > 0) {
            double s = double(longSide) / std::max(bgr.cols, bgr.rows);
            cv::resize(bgr, bgr, cv::Size(), s, s, s < 1 ? cv::INTER_AREA : cv::INTER_CUBIC);
        }
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        ofPixels frame{rgb.data, rgb.cols, rgb.rows};
        Mosaic m = adaptive::make(frame, minArea); // warm-up
        std::fill(std::begin(g_count), std::end(g_count), 0L);
        static std::vector<double> samples[S_N];
        static long counts[8];
        for (int r = 0; r < reps; r++) {
            std::fill(std::begin(g_t), std::end(g_t), 0.0);
            m = adaptive::make(frame, minArea);
            for (int s = 0; s < S_N; s++) samples[s].push_back(g_t[s]);
        }
        for (int c = 0; c < 8; c++) counts[c] += g_count[c];
        runs += reps;
        std::printf("%s %dx%d: %zu Tessels, %zu Contours, hash 0x%016llx\n", argv[i], rgb.cols, rgb.rows,
                    m.tessels.size(), m.contours.size(), (unsigned long long)mosaicHash(m));
        if (i + 1 == argc) {
            double median[S_N], sum = 0;
            for (int s = 0; s < S_N; s++) {
                std::vector<double>& v = samples[s];
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                sum += median[s] = v[v.size() / 2];
            }
            std::printf("median over %d runs: %.2f ms/Frame\n", runs, sum);
            for (int s = 0; s < S_N; s++)
                std::printf("  %-16s %8.3f ms  %5.1f%%\n", stageName[s], median[s], 100 * median[s] / sum);
            std::printf("  counts/Frame: FH %ld, joined %ld, pieces %ld, chains %ld, Tessels %ld, repair rounds %.1f\n",
                        counts[0] / runs, counts[1] / runs, counts[2] / runs, counts[3] / runs, counts[4] / runs,
                        double(counts[5]) / runs);
        }
    }
}
