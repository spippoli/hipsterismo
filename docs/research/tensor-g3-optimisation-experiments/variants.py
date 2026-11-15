#!/usr/bin/env python3
"""Throwaway: write OUT/Adaptive.cpp = instrumented Adaptive.cpp plus optional
exact rewrites of the hot spots, each behind a macro so it can be measured on
its own:

  FAST_PAIRS   neighbourPairs() without the std::sort of all boundary pixel
               pairs: counting sort by the smaller label, de-duplicate with a
               stamp array, sort each (short) bucket. Same sorted unique list.
  FAST_EDGES   sortedEdges() computes the 4 weights per pixel once into a
               float array, histograms it, and scatters straight into the
               sorted array (no intermediate 12-byte edge vector). Same weights,
               same stable order.
  FAST_COLOUR  meanColour() with exact integer edge functions on the doubled
               lattice (vertices are lattice points, pixel centres half-integers)
               and per-row spans; same top-left decisions, same sums.
  THREADS=n    Tessel colour means on n std::threads (integer sums, so the
               result does not depend on the split).

usage: variants.py instrumented/Adaptive.cpp OUT/Adaptive.cpp
"""
import sys

src, out = sys.argv[1], sys.argv[2]
s = open(src).read()

def sub(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)

sub('#include <unordered_map>', '#include <unordered_map>\n#include <thread>')

# ---------------------------------------------------------------- pairs
sub("""std::vector<std::pair<int, int>> neighbourPairs(const std::vector<int>& label, int w, int h) {
    std::vector<uint64_t> pairs;""", """#ifdef FAST_PAIRS
std::vector<std::pair<int, int>> neighbourPairs(const std::vector<int>& label, int w, int h) {
    // Pairs (lo, hi) of 4-neighbouring labels, bucketed by lo with a counting
    // sort; a stamp per hi drops repeats inside a bucket; each bucket is then
    // sorted. The labels are 0..max, so the stamp and counts are dense.
    int count = 0;
    for (int l : label) count = std::max(count, l + 1);
    std::vector<int> start(count + 1, 0);
    auto each = [&](auto&& f) {
        for (int y = 0; y < h; y++) {
            const int* row = &label[size_t(y) * w];
            const int* below = y + 1 < h ? row + w : nullptr;
            for (int x = 0; x < w; x++) {
                const int a = row[x];
                if (x + 1 < w && row[x + 1] != a) f(std::min(a, row[x + 1]), std::max(a, row[x + 1]));
                if (below && below[x] != a) f(std::min(a, below[x]), std::max(a, below[x]));
            }
        }
    };
    each([&](int lo, int) { start[lo + 1]++; });
    for (int i = 0; i < count; i++) start[i + 1] += start[i];
    std::vector<int> his(start[count]), fill(start.begin(), start.end() - 1);
    each([&](int lo, int hi) { his[fill[lo]++] = hi; });
    std::vector<int> stamp(count, -1);
    std::vector<std::pair<int, int>> result;
    std::vector<int> bucket;
    for (int lo = 0; lo < count; lo++) {
        bucket.clear();
        for (int k = start[lo]; k < start[lo + 1]; k++) {
            if (stamp[his[k]] != lo) {
                stamp[his[k]] = lo;
                bucket.push_back(his[k]);
            }
        }
        std::sort(bucket.begin(), bucket.end());
        for (int hi : bucket) result.push_back({lo, hi});
    }
    return result;
}
#else
std::vector<std::pair<int, int>> neighbourPairs(const std::vector<int>& label, int w, int h) {
    std::vector<uint64_t> pairs;""")
sub("""    for (uint64_t p : pairs) result.push_back({int(p >> 32), int(p & 0xffffffffu)});
    return result;
}""", """    for (uint64_t p : pairs) result.push_back({int(p >> 32), int(p & 0xffffffffu)});
    return result;
}
#endif""")

# ---------------------------------------------------------------- edges
sub("""std::vector<PixelEdge> sortedEdges(const cv::Mat& lab) {
    const int w = lab.cols, h = lab.rows;""", """#ifdef FAST_EDGES
std::vector<PixelEdge> sortedEdges(const cv::Mat& lab) {
    Tick tk;
    const int w = lab.cols, h = lab.rows, n = w * h;
    // weight[4 i + d]: d 0 right, 1 down, 2 down-right, 3 down-left; -1 if none.
    std::vector<float> weight(size_t(n) * 4, -1.f);
    auto distance = [](const cv::Vec3f& p, const cv::Vec3f& q) {
        cv::Vec3f d = p - q;
        return std::sqrt(d.dot(d));
    };
    const int buckets = 4096;
    auto bucket = [&](float weight) { return std::min(buckets - 1, int(weight * 16.f)); };
    std::vector<int> start(buckets + 1, 0);
    for (int y = 0; y < h; y++) {
        const cv::Vec3f* row = lab.ptr<cv::Vec3f>(y);
        const cv::Vec3f* below = y + 1 < h ? lab.ptr<cv::Vec3f>(y + 1) : nullptr;
        float* wt = &weight[size_t(y) * w * 4];
        for (int x = 0; x < w; x++, wt += 4) {
            if (x + 1 < w) wt[0] = distance(row[x], row[x + 1]);
            if (below) {
                wt[1] = distance(row[x], below[x]);
                if (x + 1 < w) wt[2] = distance(row[x], below[x + 1]);
                if (x > 0) wt[3] = distance(row[x], below[x - 1]);
            }
        }
    }
    for (float v : weight) if (v >= 0) start[bucket(v) + 1]++;
    tk.lap(S_EDGES);
    for (int i = 0; i < buckets; i++) start[i + 1] += start[i];
    std::vector<PixelEdge> sorted(start[buckets]);
    const int offset[4] = {1, w, w + 1, w - 1};
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < 4; d++) {
            const float v = weight[size_t(i) * 4 + d];
            if (v >= 0) sorted[start[bucket(v)]++] = {i, i + offset[d], v};
        }
    }
    tk.lap(S_SORT);
    return sorted;
}
#else
std::vector<PixelEdge> sortedEdges(const cv::Mat& lab) {
    const int w = lab.cols, h = lab.rows;""")
sub("""    for (const PixelEdge& e : edges) sorted[start[bucket(e.weight)]++] = e;
    tk.lap(S_SORT);
    return sorted;
}
""", """    for (const PixelEdge& e : edges) sorted[start[bucket(e.weight)]++] = e;
    tk.lap(S_SORT);
    return sorted;
}
#endif
""")

# ---------------------------------------------------------------- colour
sub("""ofColor meanColour(const ofPixels& rgb, std::array<glm::dvec2, 3> t) {""", """#ifdef FAST_COLOUR
// Exact: the vertices are lattice points and the pixel centres half-integers,
// so on the doubled lattice every edge function is an integer. For each row,
// each edge bounds the span of inside centres with an integer division; the
// span is then summed with no per-pixel test.
ofColor meanColour(const ofPixels& rgb, std::array<glm::dvec2, 3> t) {
    const int w = rgb.getWidth(), h = rgb.getHeight();
    auto cross = [](glm::dvec2 a, glm::dvec2 b, glm::dvec2 p) {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    };
    if (cross(t[0], t[1], t[2]) < 0) std::swap(t[1], t[2]);
    int64_t vx[3], vy[3];
    for (int k = 0; k < 3; k++) { vx[k] = int64_t(t[k].x) * 2; vy[k] = int64_t(t[k].y) * 2; }
    const int y0 = std::max(0, int(std::floor(std::min({t[0].y, t[1].y, t[2].y}) - 0.5)));
    const int y1 = std::min(h - 1, int(std::ceil(std::max({t[0].y, t[1].y, t[2].y}))));
    const int bx0 = std::max(0, int(std::floor(std::min({t[0].x, t[1].x, t[2].x}) - 0.5)));
    const int bx1 = std::min(w - 1, int(std::ceil(std::max({t[0].x, t[1].x, t[2].x}))));
    const unsigned char* data = rgb.getData();
    uint64_t r = 0, g = 0, b = 0, count = 0;
    auto floorDiv = [](int64_t a, int64_t d) { int64_t q = a / d; return (a % d != 0 && ((a < 0) != (d < 0))) ? q - 1 : q; };
    for (int y = y0; y <= y1; y++) {
        const int64_t cy = 2 * y + 1;
        // Centre X = 2x+1. Edge k: side(X) = ex*(cy-ay) - ey*(X-ax) = A - ey*X.
        // Inside: side > 0, or side == 0 on a top-left edge.
        int64_t lo = bx0, hi = bx1;
        for (int k = 0; k < 3 && lo <= hi; k++) {
            const int64_t ax = vx[k], ay = vy[k], ex = vx[(k + 1) % 3] - ax, ey = vy[(k + 1) % 3] - ay;
            const bool topLeft = (ey == 0 && ex < 0) || ey > 0;
            const int64_t A = ex * (cy - ay) + ey * ax; // side = A - ey*X
            if (ey == 0) {
                if (!(A > 0 || (A == 0 && topLeft))) hi = lo - 1;
                continue;
            }
            // X = 2x+1. Need A - ey*X > 0 (or >= 0 if topLeft).
            if (ey > 0) { // X < A/ey  (X <= A/ey if topLeft)
                int64_t Xmax = topLeft ? floorDiv(A, ey) : floorDiv(A - 1, ey);
                hi = std::min(hi, floorDiv(Xmax - 1, 2));
            } else { // X > A/ey  (X >= A/ey if topLeft), ey < 0
                const int64_t ne = -ey, nA = -A; // -ey*X > -A  =>  X > nA/ne... careful: A - ey X = A + ne X
                // A + ne*X > 0  =>  X > -A/ne  => X >= floor(-A/ne) + 1 ; >= 0 => X >= ceil(-A/ne)
                int64_t Xmin = topLeft ? -floorDiv(A, ne) : floorDiv(nA, ne) + 1;
                lo = std::max(lo, -floorDiv(-(Xmin - 1), 2));
            }
        }
        if (lo > hi) continue;
        const unsigned char* p = data + (size_t(y) * w + lo) * 3;
        for (int64_t x = lo; x <= hi; x++, p += 3) {
            r += p[0];
            g += p[1];
            b += p[2];
        }
        count += hi - lo + 1;
    }
    if (count == 0) {
        glm::dvec2 c = (t[0] + t[1] + t[2]) / 3.0;
        return rgb.getColor(ofClamp(int(c.x), 0, w - 1), ofClamp(int(c.y), 0, h - 1));
    }
    auto mean = [&](uint64_t sum) { return (unsigned char)((sum + count / 2) / count); };
    return ofColor(mean(r), mean(g), mean(b));
}
#else
ofColor meanColour(const ofPixels& rgb, std::array<glm::dvec2, 3> t) {""")
sub("""    return ofColor(mean(r), mean(g), mean(b));
}

} // namespace""", """    return ofColor(mean(r), mean(g), mean(b));
}
#endif

} // namespace""")

sub("""    for (const auto& triangle : triangles) {
        Tessel tessel;
        for (int k = 0; k < 3; k++) tessel.triangle[k] = glm::vec3(triangle[k].x, triangle[k].y, 0);
        tessel.color = meanColour(rgb, triangle);
        mosaic.tessels.push_back(tessel);
    }""", """#ifndef THREADS
#define THREADS 1
#endif
    mosaic.tessels.resize(triangles.size());
    auto colourRange = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; i++) {
            Tessel& tessel = mosaic.tessels[i];
            for (int k = 0; k < 3; k++) tessel.triangle[k] = glm::vec3(triangles[i][0 + k].x, triangles[i][k].y, 0);
            tessel.color = meanColour(rgb, triangles[i]);
        }
    };
    if (THREADS <= 1) {
        colourRange(0, triangles.size());
    } else {
        std::vector<std::thread> pool;
        const size_t n = triangles.size();
        for (int t = 0; t < THREADS; t++) pool.emplace_back(colourRange, n * t / THREADS, n * (t + 1) / THREADS);
        for (auto& th : pool) th.join();
    }""")
open(out, "w").write(s)
