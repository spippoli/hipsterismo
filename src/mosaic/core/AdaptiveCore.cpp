#include "AdaptiveCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <unordered_map>

#include "AdaptiveLab.h"
#include "CDT.h"

namespace mosaic::core {
namespace {

// ------------------------------------------------------------ plain vectors

// A point of the lattice of pixel corners.
struct IVec {
    int x = 0, y = 0;
    bool operator==(const IVec& o) const { return x == o.x && y == o.y; }
    bool operator!=(const IVec& o) const { return !(*this == o); }
    IVec operator-(const IVec& o) const { return {x - o.x, y - o.y}; }
};

struct I64Vec {
    int64_t x = 0, y = 0;
    I64Vec() = default;
    I64Vec(int64_t x_, int64_t y_) : x(x_), y(y_) {}
    explicit I64Vec(IVec p) : x(p.x), y(p.y) {}
    I64Vec operator-(const I64Vec& o) const { return {x - o.x, y - o.y}; }
};

struct DVec {
    double x = 0, y = 0;
    DVec() = default;
    DVec(double x_, double y_) : x(x_), y(y_) {}
    explicit DVec(IVec p) : x(p.x), y(p.y) {}
    DVec operator+(const DVec& o) const { return {x + o.x, y + o.y}; }
    DVec operator-(const DVec& o) const { return {x - o.x, y - o.y}; }
    DVec operator/(double s) const { return {x / s, y / s}; }
};

double dot(const DVec& a, const DVec& b) {
    // The products first, then their sum, as glm does.
    const double px = a.x * b.x, py = a.y * b.y;
    return px + py;
}

// --------------------------------------------------------------- logging

LogHook& logHook() {
    static LogHook hook = [](LogLevel level, const std::string& message) {
        std::cerr << (level == LogLevel::Error ? "[error] " : "[warning] ") << "Mosaic: " << message << "\n";
    };
    return hook;
}

void log(LogLevel level, const std::string& message) {
    if (logHook()) logHook()(level, message);
}

// ------------------------------------------------------------ union-find

// Union-find over pixels or Patches, with union by size.
struct DisjointSets {
    std::vector<int> parent, size;

    explicit DisjointSets(int n) : parent(n), size(n, 1) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    int find(int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    // Joins the sets of two roots and returns the new root.
    int unite(int a, int b) {
        if (size[a] < size[b] || (size[a] == size[b] && b < a)) std::swap(a, b);
        parent[b] = a;
        size[a] += size[b];
        return a;
    }
};

// Relabels 0..count-1 in scan order after merging `sets` of labels.
int relabel(std::vector<int>& label, DisjointSets& sets, int count) {
    std::vector<int> compact(count, -1);
    int next = 0;
    for (int& l : label) {
        int root = sets.find(l);
        if (compact[root] < 0) compact[root] = next++;
        l = compact[root];
    }
    return next;
}

const uint8_t* pixel(const RgbImage& rgb, int x, int y) {
    return rgb.data + size_t(y) * rgb.stride + size_t(x) * 3;
}

// ------------------------------------------------------------ Lab Frame

// A float Lab Frame, three floats (L, a, b) per pixel, row by row.
struct LabFrame {
    int width = 0, height = 0;
    std::unique_ptr<float[]> values;
    const float* at(int i) const { return &values[size_t(i) * 3]; }
};

// The Frame in Lab (exactly OpenCV's float conversion, see labOf()),
// blurred with σ by gaussianBlur(), whose fused multiply-adds give the same
// floats on every platform. labFrame() writes every value, so the array is
// left uninitialised.
LabFrame blurredLab(const RgbImage& rgb, float sigma) {
    LabFrame frame{rgb.width, rgb.height, std::unique_ptr<float[]>(new float[size_t(rgb.width) * rgb.height * 3])};
    labFrame(rgb.data, rgb.width, rgb.height, rgb.stride, frame.values.get());
    gaussianBlur(frame.values.get(), rgb.width, rgb.height, double(sigma));
    return frame;
}

// ------------------------------------------- Felzenszwalb–Huttenlocher

// The edges from each pixel to its right, lower, lower-right and lower-left
// neighbours (directions 0 to 3), numbered 4 · pixel + direction, which is
// the order the bucket sort keeps within a bucket.
struct SortedEdge {
    uint32_t edge;
    float weight; // Lab distance
};

// The edges sorted by weight with a bucket sort (1/16 ΔE per bucket, stable,
// so the order is deterministic). The weights of a row go into `weight`
// (-1 where the neighbour is outside the Frame) and are counted per bucket
// while the row is in the cache; then every edge is scattered straight to its
// place, eight bytes each. Both arrays are written before they are read, so
// they are left uninitialised (zeroing them cost as much as writing them).
struct SortedEdges {
    std::unique_ptr<SortedEdge[]> edges;
    size_t count = 0;
    const SortedEdge* begin() const { return edges.get(); }
    const SortedEdge* end() const { return edges.get() + count; }
};

SortedEdges sortedEdges(const LabFrame& lab) {
    const int w = lab.width, h = lab.height;
    const size_t n = size_t(w) * h;
    std::unique_ptr<float[]> weight(new float[n * 4]);
    // The Euclidean distance, summed from 0 in channel order.
    auto distance = [](const float* p, const float* q) {
        float s = 0;
        for (int c = 0; c < 3; c++) {
            const float d = p[c] - q[c];
            s += d * d;
        }
        return std::sqrt(s);
    };
    const int buckets = 4096;
    auto bucket = [&](float weight) { return std::min(buckets - 1, int(weight * 16.f)); };
    std::vector<size_t> start(buckets + 1, 0);
    for (int y = 0; y < h; y++) {
        const float* row = lab.at(y * w);
        float* out = &weight[size_t(y) * w * 4];
        if (y + 1 < h) {
            const float* below = row + size_t(w) * 3;
            for (int x = 0; x < w; x++) {
                const float* p = row + size_t(x) * 3;
                const float* q = below + size_t(x) * 3;
                out[4 * x + 0] = x + 1 < w ? distance(p, p + 3) : -1.f;
                out[4 * x + 1] = distance(p, q);
                out[4 * x + 2] = x + 1 < w ? distance(p, q + 3) : -1.f;
                out[4 * x + 3] = x > 0 ? distance(p, q - 3) : -1.f;
            }
        } else {
            for (int x = 0; x < w; x++) {
                const float* p = row + size_t(x) * 3;
                out[4 * x + 0] = x + 1 < w ? distance(p, p + 3) : -1.f;
                out[4 * x + 1] = out[4 * x + 2] = out[4 * x + 3] = -1.f;
            }
        }
        for (int k = 0; k < 4 * w; k++) {
            if (out[k] >= 0) start[bucket(out[k]) + 1]++;
        }
    }
    for (int i = 0; i < buckets; i++) start[i + 1] += start[i];
    SortedEdges sorted{std::unique_ptr<SortedEdge[]>(new SortedEdge[start[buckets]]), start[buckets]};
    for (size_t e = 0; e < n * 4; e++) {
        if (weight[e] >= 0) sorted.edges[start[bucket(weight[e])]++] = {uint32_t(e), weight[e]};
    }
    return sorted;
}

// Over-segmentation: joins two components when the edge between them is no
// heavier than either one's internal difference plus k / size. Min size 1,
// so there is no small-component pass. Returns a label per pixel, 0..count-1.
std::vector<int> felzenszwalb(const LabFrame& lab, int& count) {
    const int w = lab.width, n = lab.width * lab.height;
    DisjointSets sets(n);
    std::vector<float> threshold(n, segmentationK);
    const int offset[4] = {1, w, w + 1, w - 1};
    for (const SortedEdge& e : sortedEdges(lab)) {
        const int i = int(e.edge >> 2);
        int a = sets.find(i), b = sets.find(i + offset[e.edge & 3]);
        if (a != b && e.weight <= threshold[a] && e.weight <= threshold[b]) {
            int root = sets.unite(a, b);
            threshold[root] = e.weight + segmentationK / sets.size[root];
        }
    }
    std::vector<int> label(n);
    std::iota(label.begin(), label.end(), 0);
    count = relabel(label, sets, n);
    return label;
}

// The pairs of 4-neighbouring labels (labels 0..count-1), each once, sorted.
// The pairs found along the boundaries are bucketed by their smaller label
// (a counting sort), a stamp per larger label drops the repeats within a
// bucket, and each short bucket is sorted.
std::vector<std::pair<int, int>> neighbourPairs(const std::vector<int>& label, int w, int h, int count) {
    std::vector<std::pair<int, int>> found;
    for (int y = 0; y < h; y++) {
        const int* row = &label[size_t(y) * w];
        const int* below = y + 1 < h ? row + w : nullptr;
        for (int x = 0; x < w; x++) {
            const int a = row[x];
            if (x + 1 < w && row[x + 1] != a) found.push_back({std::min(a, row[x + 1]), std::max(a, row[x + 1])});
            if (below && below[x] != a) found.push_back({std::min(a, below[x]), std::max(a, below[x])});
        }
    }
    std::vector<int> start(size_t(count) + 1, 0);
    for (const auto& pair : found) start[pair.first + 1]++;
    for (int i = 0; i < count; i++) start[i + 1] += start[i];
    std::vector<int> his(found.size()), fill(start.begin(), start.end() - 1);
    for (const auto& pair : found) his[fill[pair.first]++] = pair.second;
    std::vector<int> stamp(count, -1);
    std::vector<std::pair<int, int>> result;
    for (int lo = 0; lo < count; lo++) {
        const size_t first = result.size();
        for (int k = start[lo]; k < start[lo + 1]; k++) {
            if (stamp[his[k]] != lo) {
                stamp[his[k]] = lo;
                result.push_back({lo, his[k]});
            }
        }
        std::sort(result.begin() + first, result.end());
    }
    return result;
}

// ------------------------------------------- equal-colour joining

// Joins neighbouring Patches whose mean colours in the unblurred Frame are
// exactly equal. The blur turns a sharp edge into strips of in-between
// colours one pixel wide, and the segmentation keeps them as Patches long
// enough to pass `min patch area`; in the Frame itself they have the colour
// of the side they belong to. Returns the new count.
int joinEqualColours(const RgbImage& rgb, std::vector<int>& label, int count) {
    const int w = rgb.width, h = rgb.height;
    std::vector<std::array<uint64_t, 4>> sum(count, {0, 0, 0, 0}); // r, g, b, pixels
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* p = pixel(rgb, x, y);
            auto& s = sum[label[y * w + x]];
            s[0] += p[0];
            s[1] += p[1];
            s[2] += p[2];
            s[3]++;
        }
    }
    // Equal means are an equivalence, and joining two Patches keeps their
    // mean, so one pass over the original neighbours is enough.
    DisjointSets sets(count);
    for (auto [a, b] : neighbourPairs(label, w, h, count)) {
        const auto &p = sum[a], &q = sum[b];
        bool equal = true;
        for (int c = 0; c < 3; c++) equal = equal && p[c] * q[3] == q[c] * p[3];
        int ra = sets.find(a), rb = sets.find(b);
        if (equal && ra != rb) sets.unite(ra, rb);
    }
    return relabel(label, sets, count);
}

// ------------------------------------------------- 4-connected pieces

// Splits every Patch into its 4-connected pieces, each a Patch of its own.
// The segmentation links diagonal neighbours, so a Patch can be pieces that
// touch only at a pixel corner. The boundaries are traced along pixel sides,
// where each piece is on its own, so a small piece would get its own
// boundary and Tessels (#45). Relabels in scan order and returns the count.
//
// The pieces are found on runs (a row's stretches of one label): a run joins
// the runs of the row above that it overlaps with the same label. Runs are in
// scan order, so numbering the pieces by their first run numbers them by
// their first pixel, as relabel() would.
int splitIntoPieces(std::vector<int>& label, int w, int h) {
    struct Run {
        int x0, x1, label; // pixels x0..x1-1
    };
    std::vector<Run> runs;
    std::vector<int> rowStart(size_t(h) + 1);
    for (int y = 0; y < h; y++) {
        rowStart[y] = int(runs.size());
        const int* row = &label[size_t(y) * w];
        for (int x = 0; x < w;) {
            int end = x + 1;
            while (end < w && row[end] == row[x]) end++;
            runs.push_back({x, end, row[x]});
            x = end;
        }
    }
    rowStart[h] = int(runs.size());
    DisjointSets pieces(int(runs.size()));
    for (int y = 1; y < h; y++) {
        int k = rowStart[y - 1];
        for (int r = rowStart[y]; r < rowStart[y + 1]; r++) {
            const Run& run = runs[r];
            while (runs[k].x1 <= run.x0) k++;
            for (int u = k; u < rowStart[y] && runs[u].x0 < run.x1; u++) {
                if (runs[u].label != run.label) continue;
                int a = pieces.find(r), b = pieces.find(u);
                if (a != b) pieces.unite(a, b);
            }
        }
    }
    std::vector<int> compact(runs.size(), -1);
    int next = 0;
    for (int y = 0; y < h; y++) {
        int* row = &label[size_t(y) * w];
        for (int r = rowStart[y]; r < rowStart[y + 1]; r++) {
            const int root = pieces.find(r);
            if (compact[root] < 0) compact[root] = next++;
            std::fill(row + runs[r].x0, row + runs[r].x1, compact[root]);
        }
    }
    return next;
}

// ------------------------------------------------------- Ward merging

// Merges 4-neighbouring Patches, cheapest Ward cost n1 n2 / (n1 + n2) |μ1 - μ2|²
// first, until no Patch has fewer than `minArea` pixels. With `onlySmall`,
// only merges that involve a Patch under `minArea` count: the small ones are
// absorbed into a neighbour and the others stay as they are. Relabels in
// place, 0..count-1 in scan order, and returns the new count.
int wardMerge(const LabFrame& lab, std::vector<int>& label, int count, int minArea, bool onlySmall) {
    const int w = lab.width, h = lab.height;
    std::vector<double> n(count, 0);
    std::vector<std::array<double, 3>> sum(count, {0, 0, 0});
    for (int i = 0; i < w * h; i++) {
        const int r = label[i];
        n[r] += 1;
        for (int c = 0; c < 3; c++) sum[r][c] += double(lab.at(i)[c]);
    }

    std::vector<std::vector<int>> neighbours(count);
    for (auto [a, b] : neighbourPairs(label, w, h, count)) {
        neighbours[a].push_back(b);
        neighbours[b].push_back(a);
    }

    // A candidate merge, valid while both Patches are roots at these versions.
    struct Candidate {
        double cost;
        int a, b, versionA, versionB;
        bool operator>(const Candidate& o) const {
            if (cost != o.cost) return cost > o.cost;
            return a != o.a ? a > o.a : b > o.b;
        }
    };
    // The means as sum * (1 / n), and the squared distance summed from 0 in
    // channel order.
    auto cost = [&](int a, int b) {
        const double ia = 1. / n[a], ib = 1. / n[b];
        double d2 = 0;
        for (int c = 0; c < 3; c++) {
            const double d = sum[a][c] * ia - sum[b][c] * ib;
            d2 += d * d;
        }
        return n[a] * n[b] / (n[a] + n[b]) * d2;
    };
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> queue;
    for (int a = 0; a < count; a++) {
        for (int b : neighbours[a]) {
            if (a < b) queue.push({cost(a, b), a, b, 0, 0});
        }
    }

    DisjointSets sets(count);
    std::vector<int> version(count, 0), seen(count, -1);
    int small = 0;
    for (int r = 0; r < count; r++) small += n[r] < minArea;
    for (int merge = 0; small > 0 && !queue.empty();) {
        Candidate c = queue.top();
        queue.pop();
        if (sets.find(c.a) != c.a || sets.find(c.b) != c.b || version[c.a] != c.versionA
            || version[c.b] != c.versionB) {
            continue;
        }
        if (onlySmall && n[c.a] >= minArea && n[c.b] >= minArea) continue;
        small -= (n[c.a] < minArea) + (n[c.b] < minArea);
        int root = sets.unite(c.a, c.b), other = root == c.a ? c.b : c.a;
        n[root] += n[other];
        for (int c = 0; c < 3; c++) sum[root][c] += sum[other][c];
        small += n[root] < minArea;
        version[root]++;
        merge++;

        std::vector<int> joined;
        for (int source : {root, other}) {
            for (int x : neighbours[source]) {
                int f = sets.find(x);
                if (f != root && seen[f] != merge) {
                    seen[f] = merge;
                    joined.push_back(f);
                }
            }
        }
        neighbours[root] = joined;
        std::vector<int>().swap(neighbours[other]);
        for (int x : joined) {
            int lo = std::min(root, x), hi = std::max(root, x);
            queue.push({cost(lo, hi), lo, hi, version[lo], version[hi]});
        }
    }

    return relabel(label, sets, count);
}

// ---------------------------------------------------- boundary chains

// A Patch boundary between two junctions, on the lattice of pixel corners
// (0..width x 0..height). A closed chain ends where it starts.
struct Chain {
    std::vector<IVec> points;
    bool closed = false;
};

// Traces every crack edge (a pixel side with different labels on either
// side, the outside of the Frame being label -1) into chains. Junctions are
// the corners with three or more cracks, plus the four Frame corners; a
// junction-free loop starts at its first corner in scan order.
std::vector<Chain> traceChains(const std::vector<int>& label, int w, int h) {
    // Horizontal crack (cx, cy)-(cx+1, cy) between pixels (cx, cy-1) and (cx, cy);
    // vertical crack (cx, cy)-(cx, cy+1) between pixels (cx-1, cy) and (cx, cy).
    // Each crack's state, found once: 0 no crack, 1 a crack not yet walked,
    // 2 a walked one.
    std::vector<uint8_t> crackH(size_t(w) * (h + 1)), crackV(size_t(w + 1) * h);
    for (int cy = 0; cy <= h; cy++) {
        const int* above = cy > 0 ? &label[size_t(cy - 1) * w] : nullptr;
        const int* below = cy < h ? &label[size_t(cy) * w] : nullptr;
        uint8_t* out = &crackH[size_t(cy) * w];
        if (above && below) {
            for (int cx = 0; cx < w; cx++) out[cx] = above[cx] != below[cx];
        } else {
            for (int cx = 0; cx < w; cx++) out[cx] = 1; // the Frame border (label -1 outside)
        }
    }
    for (int cy = 0; cy < h; cy++) {
        const int* row = &label[size_t(cy) * w];
        uint8_t* out = &crackV[size_t(cy) * (w + 1)];
        out[0] = out[w] = 1;
        for (int cx = 1; cx < w; cx++) out[cx] = row[cx - 1] != row[cx];
    }
    auto hasH = [&](int cx, int cy) { return cx >= 0 && cx < w && cy >= 0 && cy <= h && crackH[size_t(cy) * w + cx]; };
    auto hasV = [&](int cx, int cy) { return cy >= 0 && cy < h && cx >= 0 && cx <= w && crackV[size_t(cy) * (w + 1) + cx]; };

    std::vector<uint8_t> junction(size_t(w + 1) * (h + 1), 0);
    for (int cy = 0; cy <= h; cy++) {
        for (int cx = 0; cx <= w; cx++) {
            bool corner = (cx == 0 || cx == w) && (cy == 0 || cy == h);
            int degree = hasH(cx, cy) + hasH(cx - 1, cy) + hasV(cx, cy) + hasV(cx, cy - 1);
            junction[cy * (w + 1) + cx] = corner || degree >= 3;
        }
    }

    // The state of the crack leaving (cx, cy) in direction `dir` (0 right,
    // 1 down, 2 left, 3 up), or null if there is no crack there.
    auto crack = [&](int cx, int cy, int dir) -> uint8_t* {
        switch (dir) {
        case 0: return hasH(cx, cy) ? &crackH[cy * w + cx] : nullptr;
        case 1: return hasV(cx, cy) ? &crackV[cy * (w + 1) + cx] : nullptr;
        case 2: return hasH(cx - 1, cy) ? &crackH[cy * w + cx - 1] : nullptr;
        default: return hasV(cx, cy - 1) ? &crackV[(cy - 1) * (w + 1) + cx] : nullptr;
        }
    };
    const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};

    std::vector<Chain> chains;
    auto walk = [&](int cx, int cy, int dir, bool closed) {
        Chain chain;
        chain.closed = closed;
        chain.points.push_back({cx, cy});
        const int sx = cx, sy = cy;
        while (true) {
            *crack(cx, cy, dir) = 2;
            cx += dx[dir];
            cy += dy[dir];
            chain.points.push_back({cx, cy});
            if (junction[cy * (w + 1) + cx] || (closed && cx == sx && cy == sy)) break;
            // A corner that is no junction has exactly one other crack.
            int next = -1;
            for (int d = 0; d < 4 && next < 0; d++) {
                uint8_t* c = d == ((dir + 2) & 3) ? nullptr : crack(cx, cy, d);
                if (c && *c == 1) next = d;
            }
            if (next < 0) break;
            dir = next;
        }
        chains.push_back(std::move(chain));
    };
    for (int cy = 0; cy <= h; cy++) {
        for (int cx = 0; cx <= w; cx++) {
            if (!junction[cy * (w + 1) + cx]) continue;
            for (int dir = 0; dir < 4; dir++) {
                uint8_t* c = crack(cx, cy, dir);
                if (c && *c == 1) walk(cx, cy, dir, false);
            }
        }
    }
    for (int cy = 0; cy <= h; cy++) {
        for (int cx = 0; cx <= w; cx++) {
            for (int dir = 0; dir < 2; dir++) {
                uint8_t* c = crack(cx, cy, dir);
                if (c && *c == 1) walk(cx, cy, dir, true);
            }
        }
    }
    return chains;
}

// Drops the points where a chain goes straight on. The geometry is
// unchanged, and the ends (junctions, or a loop's start) are kept, so the
// chains still meet at shared points. No other chain touches a dropped point:
// it has only this chain's two cracks.
void mergeCollinear(Chain& chain) {
    std::vector<IVec>& p = chain.points;
    if (p.size() < 3) return;
    std::vector<IVec> kept = {p.front()};
    for (size_t i = 1; i + 1 < p.size(); i++) {
        if (p[i] - p[i - 1] != p[i + 1] - p[i]) kept.push_back(p[i]);
    }
    kept.push_back(p.back());
    p = std::move(kept);
}

// Whether a chain runs along the Frame border. A chain is all border or all
// inside: a corner where a border crack meets an inner one is a junction.
bool onBorder(const Chain& chain, int w, int h) {
    IVec a = chain.points[0], b = chain.points[1];
    return (a.x == b.x && (a.x == 0 || a.x == w)) || (a.y == b.y && (a.y == 0 || a.y == h));
}

// ------------------------------------------------------ simplification

// Marks with Douglas–Peucker the points of p[first..last] to keep at
// tolerance `epsilon`; p[first] and p[last] are kept by the caller.
void douglasPeucker(const std::vector<IVec>& p, size_t first, size_t last, double epsilon, std::vector<char>& keep) {
    std::vector<std::pair<size_t, size_t>> spans = {{first, last}};
    while (!spans.empty()) {
        auto [i, j] = spans.back();
        spans.pop_back();
        if (j <= i + 1) continue;
        const DVec a(p[i]), d = DVec(p[j]) - a;
        const double length2 = dot(d, d);
        double farthest = -1; // squared distance to the line (to `a` if a == b)
        size_t k = i;
        for (size_t m = i + 1; m < j; m++) {
            const DVec q = DVec(p[m]) - a;
            const double cross = q.x * d.y - q.y * d.x;
            const double distance2 = length2 == 0 ? dot(q, q) : cross * cross / length2;
            if (distance2 > farthest) {
                farthest = distance2;
                k = m;
            }
        }
        if (farthest <= epsilon * epsilon) continue;
        keep[k] = 1;
        spans.push_back({i, k});
        spans.push_back({k, j});
    }
}

// The chain simplified with Douglas–Peucker at `epsilon`, its ends kept. A
// chain that ends where it starts (a loop, or a chain from a junction back to
// itself) is split at its point farthest from the start, and keeps at least
// three distinct points, so it still encloses an area.
Chain simplify(const Chain& chain, double epsilon) {
    const std::vector<IVec>& p = chain.points;
    const size_t n = p.size();
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    if (p[0] != p[n - 1]) {
        douglasPeucker(p, 0, n - 1, epsilon, keep);
    } else {
        auto distance2 = [](IVec a, IVec b) {
            I64Vec d = I64Vec(b) - I64Vec(a);
            return d.x * d.x + d.y * d.y;
        };
        size_t far = 1;
        for (size_t k = 1; k + 1 < n; k++) {
            if (distance2(p[0], p[k]) > distance2(p[0], p[far])) far = k;
        }
        keep[far] = 1;
        douglasPeucker(p, 0, far, epsilon, keep);
        douglasPeucker(p, far, n - 1, epsilon, keep);
        if (std::count(keep.begin(), keep.end(), 1) < 4) {
            // Only the start and `far`: add the point farthest from their line.
            const I64Vec a(p[0]), d = I64Vec(p[far]) - a;
            size_t third = 0;
            int64_t best = -1;
            for (size_t k = 1; k + 1 < n; k++) {
                const I64Vec q = I64Vec(p[k]) - a;
                const int64_t area = std::abs(q.x * d.y - q.y * d.x);
                if (!keep[k] && area > best) {
                    best = area;
                    third = k;
                }
            }
            keep[third] = 1;
        }
    }
    Chain simplified;
    simplified.closed = chain.closed;
    for (size_t k = 0; k < n; k++) {
        if (keep[k]) simplified.points.push_back(p[k]);
    }
    return simplified;
}

// Sign of the turn a -> b -> c: 1 left, -1 right, 0 collinear. Exact on the lattice.
int orientation(IVec a, IVec b, IVec c) {
    const int64_t v = int64_t(b.x - a.x) * (c.y - a.y) - int64_t(b.y - a.y) * (c.x - a.x);
    return (v > 0) - (v < 0);
}

// Whether p lies on the closed segment ab (p collinear with a and b).
bool onSegment(IVec a, IVec b, IVec p) {
    return orientation(a, b, p) == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x)
        && std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y);
}

// Whether two constraint segments conflict: they may only meet at a shared
// end point, without overlapping. A shared end with collinear overlap (two
// chains collapsed onto one line, or a chain folding back on itself), the
// same segment twice, a crossing, or a point touching the other segment's
// inside all conflict.
bool conflict(IVec a, IVec b, IVec c, IVec d) {
    const bool sharesA = a == c || a == d, sharesB = b == c || b == d;
    if (sharesA && sharesB) return true;
    if (sharesA || sharesB) {
        const IVec s = sharesA ? a : b, p = sharesA ? b : a, q = s == c ? d : c;
        const I64Vec u = I64Vec(p) - I64Vec(s), v = I64Vec(q) - I64Vec(s);
        return orientation(s, p, q) == 0 && u.x * v.x + u.y * v.y > 0;
    }
    if (orientation(a, b, c) * orientation(a, b, d) < 0 && orientation(c, d, a) * orientation(c, d, b) < 0) {
        return true;
    }
    return onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b);
}

// Every chain simplified once at `startEpsilon`, so the two Patches either
// side of it see the same edge, then the crossing repair. Each round, every
// chain with a segment that conflicts with another segment, of its own or of
// another chain, is simplified again at half its ε; once ε would drop below
// half a pixel it falls back to its exact crack chain, keeping every corner,
// also of a closed loop. The rounds go on until no conflict remains. Exact
// chains never conflict with each other, so each round with a conflict moves
// at least one chain a step down its finite ladder of ε, and the loop ends.
std::vector<Chain> simplifyChains(const std::vector<Chain>& exact, int w, int h, double startEpsilon) {
    const size_t count = exact.size();
    std::vector<Chain> chains(count);
    std::vector<double> epsilon(count, startEpsilon);
    std::vector<char> isExact(count), changed(count, 1);
    // A simplification keeps a subset of the points, so the same count means
    // the exact chain.
    auto simplifyAt = [&](size_t i) {
        chains[i] = simplify(exact[i], epsilon[i]);
        isExact[i] = chains[i].points.size() == exact[i].points.size();
    };
    for (size_t i = 0; i < count; i++) simplifyAt(i);

    // Segments are binned by bounding box into square cells; only pairs that
    // share a cell and involve a chain changed in the last round are tested.
    const int cellSize = 16, columns = w / cellSize + 1, rows = h / cellSize + 1;
    struct Segment {
        int chain, index;
    };
    std::vector<std::vector<Segment>> cells(size_t(columns) * rows);
    while (true) {
        for (auto& cell : cells) cell.clear();
        for (size_t i = 0; i < count; i++) {
            const std::vector<IVec>& p = chains[i].points;
            for (size_t k = 0; k + 1 < p.size(); k++) {
                const int x0 = std::min(p[k].x, p[k + 1].x) / cellSize, x1 = std::max(p[k].x, p[k + 1].x) / cellSize;
                const int y0 = std::min(p[k].y, p[k + 1].y) / cellSize, y1 = std::max(p[k].y, p[k + 1].y) / cellSize;
                for (int y = y0; y <= y1; y++) {
                    for (int x = x0; x <= x1; x++) cells[size_t(y) * columns + x].push_back({int(i), int(k)});
                }
            }
        }
        std::vector<char> conflicting(count, 0);
        for (const auto& cell : cells) {
            for (size_t u = 0; u < cell.size(); u++) {
                for (size_t v = u + 1; v < cell.size(); v++) {
                    const Segment s = cell[u], t = cell[v];
                    if (!changed[s.chain] && !changed[t.chain]) continue;
                    if (s.chain == t.chain && s.index == t.index) continue;
                    const auto &p = chains[s.chain].points, &q = chains[t.chain].points;
                    if (conflict(p[s.index], p[s.index + 1], q[t.index], q[t.index + 1])) {
                        conflicting[s.chain] = conflicting[t.chain] = 1;
                    }
                }
            }
        }
        bool any = false;
        for (size_t i = 0; i < count; i++) {
            changed[i] = conflicting[i] && !isExact[i];
            if (!changed[i]) continue;
            epsilon[i] /= 2;
            if (epsilon[i] >= 0.5) {
                simplifyAt(i);
            } else {
                chains[i] = exact[i];
                isExact[i] = 1;
            }
            any = true;
        }
        // No conflict left, or (which exact chains rule out) only conflicts
        // between exact chains; CDT's fallback would then cover the Frame.
        if (!any) break;
    }
    return chains;
}

// ------------------------------------------------- triangulation, colour

using Triangles = std::vector<std::array<DVec, 3>>;

Triangles triangulate(const std::vector<Chain>& chains, int w) {
    std::vector<CDT::V2d<double>> vertices;
    std::vector<CDT::Edge> edges;
    std::unordered_map<long, CDT::VertInd> index;
    auto vertex = [&](IVec p) {
        auto [it, added] = index.try_emplace(long(p.y) * (w + 1) + p.x, CDT::VertInd(vertices.size()));
        if (added) vertices.push_back(CDT::V2d<double>(p.x, p.y));
        return it->second;
    };
    for (const Chain& chain : chains) {
        for (size_t i = 0; i + 1 < chain.points.size(); i++) {
            // vertex() numbers vertices as it first sees them, and C++ leaves
            // the order of evaluating function arguments unspecified (GCC goes
            // right to left, clang left to right), so the two calls must be
            // sequenced: otherwise CDT gets another vertex order, hence other
            // Tessels, with each compiler (#56). This is GCC's old order, which
            // keeps the golden hash.
            const CDT::VertInd b = vertex(chain.points[i + 1]);
            const CDT::VertInd a = vertex(chain.points[i]);
            edges.push_back(CDT::Edge(a, b));
        }
    }

    auto run = [&](bool constrained) {
        CDT::Triangulation<double> cdt(CDT::VertexInsertionOrder::Auto,
                                       CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
        cdt.insertVertices(vertices);
        if (constrained) cdt.insertEdges(edges);
        cdt.eraseSuperTriangle();
        Triangles triangles;
        triangles.reserve(cdt.triangles.size());
        for (const CDT::Triangle& t : cdt.triangles) {
            std::array<DVec, 3> triangle;
            for (int k = 0; k < 3; k++) {
                const CDT::V2d<double>& v = cdt.vertices[t.vertices[k]];
                triangle[k] = {v.x, v.y};
            }
            triangles.push_back(triangle);
        }
        return triangles;
    };
    try {
        return run(true);
    } catch (const std::exception& e) {
        // The crossing repair leaves no conflicting chains, so this should
        // not happen. The four Frame corners are vertices, so the
        // unconstrained triangulation still tiles the Frame.
        log(LogLevel::Warning, std::string("constrained triangulation failed (") + e.what()
                                   + "), Tessels may cross Patches in this Frame");
        try {
            return run(false);
        } catch (const std::exception& e) {
            log(LogLevel::Error, std::string("triangulation failed: ") + e.what());
            return {};
        }
    }
}

// Floor and ceiling of a / d for d > 0.
int64_t floorDiv(int64_t a, int64_t d) {
    return a >= 0 ? a / d : -((-a + d - 1) / d);
}

int64_t ceilDiv(int64_t a, int64_t d) {
    return -floorDiv(-a, d);
}

// Mean colour of the pixels whose centre lies inside the triangle; a centre
// on an edge counts for the triangle the edge is a top or left edge of, so
// each pixel counts once. The pixel at the centroid if no centre is inside.
//
// The vertices are lattice points (pixel corners) and the centres are at
// half pixels, so on the doubled lattice each edge's side test is exact in
// integers: per row, each edge bounds the run of inside centres with an
// integer division, the same decision for every pixel as the test
// side > 0 || (side == 0 && topLeft) in doubles, which is exact here too.
// The pixels of each run are then summed with no test.
Rgb meanColour(const RgbImage& rgb, std::array<DVec, 3> t) {
    const int w = rgb.width, h = rgb.height;
    auto cross = [](DVec a, DVec b, DVec p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
    if (cross(t[0], t[1], t[2]) < 0) std::swap(t[1], t[2]);
    const int x0 = std::max(0, int(std::floor(std::min({t[0].x, t[1].x, t[2].x}) - 0.5)));
    const int x1 = std::min(w - 1, int(std::ceil(std::max({t[0].x, t[1].x, t[2].x}))));
    const int y0 = std::max(0, int(std::floor(std::min({t[0].y, t[1].y, t[2].y}) - 0.5)));
    const int y1 = std::min(h - 1, int(std::ceil(std::max({t[0].y, t[1].y, t[2].y}))));
    // Doubled vertices; edge k runs from v[k] to v[k + 1].
    int64_t vx[3], vy[3];
    for (int k = 0; k < 3; k++) {
        vx[k] = 2 * int64_t(t[k].x);
        vy[k] = 2 * int64_t(t[k].y);
    }
    uint64_t r = 0, g = 0, b = 0, count = 0;
    for (int y = y0; y <= y1; y++) {
        const int64_t cy = 2 * int64_t(y) + 1;
        int64_t lo = x0, hi = x1; // the run of inside centres in this row
        for (int k = 0; k < 3 && lo <= hi; k++) {
            const int64_t ax = vx[k], ay = vy[k], ex = vx[(k + 1) % 3] - ax, ey = vy[(k + 1) % 3] - ay;
            // side(X) = ex (cy - ay) - ey (X - ax) = c - ey X at centre X = 2x + 1.
            const int64_t c = ex * (cy - ay) + ey * ax;
            if (ey == 0) {
                // topLeft if ex < 0: inside if c > 0, or c == 0 on a top edge.
                if (!(c > 0 || (c == 0 && ex < 0))) hi = lo - 1;
            } else if (ey > 0) {
                // A left edge (topLeft): inside if c - ey X >= 0, X <= c / ey.
                const int64_t maxX = floorDiv(c, ey);
                hi = std::min(hi, floorDiv(maxX - 1, 2));
            } else {
                // Not topLeft: inside if c + |ey| X > 0, X > -c / |ey|.
                const int64_t minX = floorDiv(-c, -ey) + 1;
                lo = std::max(lo, ceilDiv(minX - 1, 2));
            }
        }
        if (lo > hi) continue;
        const uint8_t* p = pixel(rgb, int(lo), y);
        for (int64_t x = lo; x <= hi; x++, p += 3) {
            r += p[0];
            g += p[1];
            b += p[2];
        }
        count += uint64_t(hi - lo + 1);
    }
    if (count == 0) {
        DVec c = (t[0] + t[1] + t[2]) / 3.0;
        const uint8_t* p = pixel(rgb, std::clamp(int(c.x), 0, w - 1), std::clamp(int(c.y), 0, h - 1));
        return {p[0], p[1], p[2]};
    }
    auto mean = [&](uint64_t sum) { return uint8_t((sum + count / 2) / count); };
    return {mean(r), mean(g), mean(b)};
}

// Steps 1 and 2, keeping the Lab Frame out of the public interface.
Segmentation segmentWith(const RgbImage& rgb, const Sizes& sizes) {
    const int w = rgb.width, h = rgb.height;
    const LabFrame lab = blurredLab(rgb, sizes.blurSigma);
    Segmentation segmentation;
    int& count = segmentation.count;
    std::vector<int>& label = segmentation.label;
    label = felzenszwalb(lab, count);
    count = joinEqualColours(rgb, label, count);
    count = wardMerge(lab, label, count, sizes.minPatchArea, false);
    // Ward merging may still leave a Patch in pieces that touch only at a
    // corner; absorbing just the small pieces keeps the rest of the Patches.
    count = splitIntoPieces(label, w, h);
    count = wardMerge(lab, label, count, sizes.minPatchArea, true);
    return segmentation;
}

bool isEmpty(const RgbImage& rgb) {
    return !rgb.data || rgb.width <= 0 || rgb.height <= 0;
}

} // namespace

Sizes sizesFor(int width, int height, int minPatchArea) {
    // Exactly 1 at 320x240, so the constants come through unchanged there.
    const double areaRatio = double(width) * height / (double(referenceWidth) * referenceHeight);
    const int shorter = std::min(width, height);
    Sizes sizes;
    sizes.minPatchArea = std::max(1, int(std::lround(minPatchArea * areaRatio)));
    sizes.blurSigma = blurSigma * (float(shorter) / referenceHeight);
    sizes.boundaryEpsilon = boundaryEpsilon * (double(shorter) / referenceHeight);
    return sizes;
}

Segmentation segment(const RgbImage& frame, int minPatchArea) {
    if (isEmpty(frame)) return {};
    return segmentWith(frame, sizesFor(frame.width, frame.height, minPatchArea));
}

Mosaic makeAdaptive(const RgbImage& rgb, int minPatchArea) {
    Mosaic mosaic;
    if (isEmpty(rgb)) return mosaic;
    const int w = rgb.width, h = rgb.height;
    const Sizes sizes = sizesFor(w, h, minPatchArea);
    const std::vector<int> label = segmentWith(rgb, sizes).label;

    std::vector<Chain> exact = traceChains(label, w, h);
    for (Chain& chain : exact) mergeCollinear(chain);
    std::vector<Chain> chains = simplifyChains(exact, w, h, sizes.boundaryEpsilon);
    for (size_t i = 0; i < chains.size(); i++) {
        if (onBorder(exact[i], w, h)) continue;
        const Chain& chain = chains[i];
        Contour contour;
        contour.closed = chain.closed;
        // A closed chain repeats its start at the end; a Contour does not.
        const size_t n = chain.points.size() - (chain.closed ? 1 : 0);
        for (size_t k = 0; k < n; k++) contour.points.push_back({float(chain.points[k].x), float(chain.points[k].y)});
        mosaic.contours.push_back(std::move(contour));
    }

    for (const auto& triangle : triangulate(chains, w)) {
        Tessel tessel;
        for (int k = 0; k < 3; k++) tessel.triangle[k] = {float(triangle[k].x), float(triangle[k].y)};
        tessel.color = meanColour(rgb, triangle);
        mosaic.tessels.push_back(tessel);
    }
    return mosaic;
}

uint64_t hash(const Mosaic& mosaic) {
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
        for (const Point& v : tessel.triangle) {
            word(v.x);
            word(v.y);
        }
        byte(tessel.color.r);
        byte(tessel.color.g);
        byte(tessel.color.b);
    }
    return h;
}

void setLogHook(LogHook hook) {
    logHook() = std::move(hook);
}

} // namespace mosaic::core
