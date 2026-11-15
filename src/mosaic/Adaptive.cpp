#include "Adaptive.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <queue>
#include <unordered_map>

#include "CDT.h"

namespace adaptive {
namespace {

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

// ------------------------------------------------------------ Lab Frame

cv::Mat blurredLab(const ofPixels& rgb, float sigma) {
    cv::Mat source = ofxCv::toCv(rgb), unit, lab;
    source.convertTo(unit, CV_32F, 1.0 / 255);
    cv::cvtColor(unit, lab, cv::COLOR_RGB2Lab);
    cv::GaussianBlur(lab, lab, cv::Size(0, 0), sigma);
    return lab;
}

// ------------------------------------------- Felzenszwalb–Huttenlocher

struct PixelEdge {
    int a, b;
    float weight; // Lab distance
};

// The edges from each pixel to its right, lower, lower-right and lower-left
// neighbours, sorted by weight with a bucket sort (1/16 ΔE per bucket,
// stable, so the order is deterministic).
std::vector<PixelEdge> sortedEdges(const cv::Mat& lab) {
    const int w = lab.cols, h = lab.rows;
    std::vector<PixelEdge> edges;
    edges.reserve(size_t(w) * h * 4);
    auto distance = [](const cv::Vec3f& p, const cv::Vec3f& q) {
        cv::Vec3f d = p - q;
        return std::sqrt(d.dot(d));
    };
    for (int y = 0; y < h; y++) {
        const cv::Vec3f* row = lab.ptr<cv::Vec3f>(y);
        const cv::Vec3f* below = y + 1 < h ? lab.ptr<cv::Vec3f>(y + 1) : nullptr;
        for (int x = 0; x < w; x++) {
            const int i = y * w + x;
            if (x + 1 < w) edges.push_back({i, i + 1, distance(row[x], row[x + 1])});
            if (below) {
                edges.push_back({i, i + w, distance(row[x], below[x])});
                if (x + 1 < w) edges.push_back({i, i + w + 1, distance(row[x], below[x + 1])});
                if (x > 0) edges.push_back({i, i + w - 1, distance(row[x], below[x - 1])});
            }
        }
    }
    const int buckets = 4096;
    auto bucket = [&](float weight) { return std::min(buckets - 1, int(weight * 16.f)); };
    std::vector<int> start(buckets + 1, 0);
    for (const PixelEdge& e : edges) start[bucket(e.weight) + 1]++;
    for (int i = 0; i < buckets; i++) start[i + 1] += start[i];
    std::vector<PixelEdge> sorted(edges.size());
    for (const PixelEdge& e : edges) sorted[start[bucket(e.weight)]++] = e;
    return sorted;
}

// Over-segmentation: joins two components when the edge between them is no
// heavier than either one's internal difference plus k / size. Min size 1,
// so there is no small-component pass. Returns a label per pixel, 0..count-1.
std::vector<int> felzenszwalb(const cv::Mat& lab, int& count) {
    const int n = lab.cols * lab.rows;
    DisjointSets sets(n);
    std::vector<float> threshold(n, segmentationK);
    for (const PixelEdge& e : sortedEdges(lab)) {
        int a = sets.find(e.a), b = sets.find(e.b);
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

// The pairs of 4-neighbouring labels, each once, sorted.
std::vector<std::pair<int, int>> neighbourPairs(const std::vector<int>& label, int w, int h) {
    std::vector<uint64_t> pairs;
    auto pair = [&](int a, int b) {
        if (a != b) pairs.push_back(uint64_t(std::min(a, b)) << 32 | uint32_t(std::max(a, b)));
    };
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x + 1 < w) pair(label[y * w + x], label[y * w + x + 1]);
            if (y + 1 < h) pair(label[y * w + x], label[(y + 1) * w + x]);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    std::vector<std::pair<int, int>> result;
    result.reserve(pairs.size());
    for (uint64_t p : pairs) result.push_back({int(p >> 32), int(p & 0xffffffffu)});
    return result;
}

// ------------------------------------------- equal-colour joining

// Joins neighbouring Patches whose mean colours in the unblurred Frame are
// exactly equal. The blur turns a sharp edge into strips of in-between
// colours one pixel wide, and the segmentation keeps them as Patches long
// enough to pass `min patch area`; in the Frame itself they have the colour
// of the side they belong to. Returns the new count.
int joinEqualColours(const ofPixels& rgb, std::vector<int>& label, int count) {
    const int w = rgb.getWidth(), h = rgb.getHeight();
    std::vector<std::array<uint64_t, 4>> sum(count, {0, 0, 0, 0}); // r, g, b, pixels
    const unsigned char* data = rgb.getData();
    for (int i = 0; i < w * h; i++) {
        auto& s = sum[label[i]];
        s[0] += data[i * 3];
        s[1] += data[i * 3 + 1];
        s[2] += data[i * 3 + 2];
        s[3]++;
    }
    // Equal means are an equivalence, and joining two Patches keeps their
    // mean, so one pass over the original neighbours is enough.
    DisjointSets sets(count);
    for (auto [a, b] : neighbourPairs(label, w, h)) {
        const auto &p = sum[a], &q = sum[b];
        bool equal = true;
        for (int c = 0; c < 3; c++) equal = equal && p[c] * q[3] == q[c] * p[3];
        int ra = sets.find(a), rb = sets.find(b);
        if (equal && ra != rb) sets.unite(ra, rb);
    }
    return relabel(label, sets, count);
}

// ------------------------------------------------------- Ward merging

// Merges 4-neighbouring Patches, cheapest Ward cost n1 n2 / (n1 + n2) |μ1 - μ2|²
// first, until no Patch has fewer than `minArea` pixels. Relabels in place,
// 0..count-1 in scan order.
void wardMerge(const cv::Mat& lab, std::vector<int>& label, int count, int minArea) {
    const int w = lab.cols, h = lab.rows;
    std::vector<double> n(count, 0);
    std::vector<cv::Vec3d> sum(count, cv::Vec3d(0, 0, 0));
    for (int y = 0; y < h; y++) {
        const cv::Vec3f* row = lab.ptr<cv::Vec3f>(y);
        for (int x = 0; x < w; x++) {
            int r = label[y * w + x];
            n[r] += 1;
            sum[r] += cv::Vec3d(row[x]);
        }
    }

    std::vector<std::vector<int>> neighbours(count);
    for (auto [a, b] : neighbourPairs(label, w, h)) {
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
    auto cost = [&](int a, int b) {
        cv::Vec3d d = sum[a] / n[a] - sum[b] / n[b];
        return n[a] * n[b] / (n[a] + n[b]) * d.dot(d);
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
        small -= (n[c.a] < minArea) + (n[c.b] < minArea);
        int root = sets.unite(c.a, c.b), other = root == c.a ? c.b : c.a;
        n[root] += n[other];
        sum[root] += sum[other];
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

    relabel(label, sets, count);
}

// ---------------------------------------------------- boundary chains

// A Patch boundary between two junctions, on the lattice of pixel corners
// (0..width x 0..height). A closed chain ends where it starts.
struct Chain {
    std::vector<glm::ivec2> points;
    bool closed = false;
};

// Traces every crack edge (a pixel side with different labels on either
// side, the outside of the Frame being label -1) into chains. Junctions are
// the corners with three or more cracks, plus the four Frame corners; a
// junction-free loop starts at its first corner in scan order.
std::vector<Chain> traceChains(const std::vector<int>& label, int w, int h) {
    auto at = [&](int x, int y) { return (x < 0 || y < 0 || x >= w || y >= h) ? -1 : label[y * w + x]; };
    // Horizontal crack (cx, cy)-(cx+1, cy) between pixels (cx, cy-1) and (cx, cy);
    // vertical crack (cx, cy)-(cx, cy+1) between pixels (cx-1, cy) and (cx, cy).
    auto hasH = [&](int cx, int cy) { return cx >= 0 && cx < w && cy >= 0 && cy <= h && at(cx, cy - 1) != at(cx, cy); };
    auto hasV = [&](int cx, int cy) { return cy >= 0 && cy < h && cx >= 0 && cx <= w && at(cx - 1, cy) != at(cx, cy); };
    std::vector<uint8_t> visitedH(size_t(w) * (h + 1), 0), visitedV(size_t(w + 1) * h, 0);

    std::vector<uint8_t> junction(size_t(w + 1) * (h + 1), 0);
    for (int cy = 0; cy <= h; cy++) {
        for (int cx = 0; cx <= w; cx++) {
            bool corner = (cx == 0 || cx == w) && (cy == 0 || cy == h);
            int degree = hasH(cx, cy) + hasH(cx - 1, cy) + hasV(cx, cy) + hasV(cx, cy - 1);
            junction[cy * (w + 1) + cx] = corner || degree >= 3;
        }
    }

    // The visited flag of the crack leaving (cx, cy) in direction `dir`
    // (0 right, 1 down, 2 left, 3 up), or null if there is no crack there.
    auto crack = [&](int cx, int cy, int dir) -> uint8_t* {
        switch (dir) {
        case 0: return hasH(cx, cy) ? &visitedH[cy * w + cx] : nullptr;
        case 1: return hasV(cx, cy) ? &visitedV[cy * (w + 1) + cx] : nullptr;
        case 2: return hasH(cx - 1, cy) ? &visitedH[cy * w + cx - 1] : nullptr;
        default: return hasV(cx, cy - 1) ? &visitedV[(cy - 1) * (w + 1) + cx] : nullptr;
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
            *crack(cx, cy, dir) = 1;
            cx += dx[dir];
            cy += dy[dir];
            chain.points.push_back({cx, cy});
            if (junction[cy * (w + 1) + cx] || (closed && cx == sx && cy == sy)) break;
            // A corner that is no junction has exactly one other crack.
            int next = -1;
            for (int d = 0; d < 4 && next < 0; d++) {
                uint8_t* c = d == ((dir + 2) & 3) ? nullptr : crack(cx, cy, d);
                if (c && !*c) next = d;
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
                if (c && !*c) walk(cx, cy, dir, false);
            }
        }
    }
    for (int cy = 0; cy <= h; cy++) {
        for (int cx = 0; cx <= w; cx++) {
            for (int dir = 0; dir < 2; dir++) {
                uint8_t* c = crack(cx, cy, dir);
                if (c && !*c) walk(cx, cy, dir, true);
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
    std::vector<glm::ivec2>& p = chain.points;
    if (p.size() < 3) return;
    std::vector<glm::ivec2> kept = {p.front()};
    for (size_t i = 1; i + 1 < p.size(); i++) {
        if (p[i] - p[i - 1] != p[i + 1] - p[i]) kept.push_back(p[i]);
    }
    kept.push_back(p.back());
    p = std::move(kept);
}

// Whether a chain runs along the Frame border. A chain is all border or all
// inside: a corner where a border crack meets an inner one is a junction.
bool onBorder(const Chain& chain, int w, int h) {
    glm::ivec2 a = chain.points[0], b = chain.points[1];
    return (a.x == b.x && (a.x == 0 || a.x == w)) || (a.y == b.y && (a.y == 0 || a.y == h));
}

// ------------------------------------------------------ simplification

// Marks with Douglas–Peucker the points of p[first..last] to keep at
// tolerance `epsilon`; p[first] and p[last] are kept by the caller.
void douglasPeucker(const std::vector<glm::ivec2>& p, size_t first, size_t last, double epsilon,
                    std::vector<char>& keep) {
    std::vector<std::pair<size_t, size_t>> spans = {{first, last}};
    while (!spans.empty()) {
        auto [i, j] = spans.back();
        spans.pop_back();
        if (j <= i + 1) continue;
        const glm::dvec2 a(p[i]), d = glm::dvec2(p[j]) - a;
        const double length2 = glm::dot(d, d);
        double farthest = -1; // squared distance to the line (to `a` if a == b)
        size_t k = i;
        for (size_t m = i + 1; m < j; m++) {
            const glm::dvec2 q = glm::dvec2(p[m]) - a;
            const double cross = q.x * d.y - q.y * d.x;
            const double distance2 = length2 == 0 ? glm::dot(q, q) : cross * cross / length2;
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

// The chain simplified with Douglas–Peucker at `epsilon`, its ends kept. A chain that ends where it starts (a loop, or a chain from a junction
// back to itself) is split at its point farthest from the start, and keeps at
// least three distinct points, so it still encloses an area.
Chain simplify(const Chain& chain, double epsilon) {
    const std::vector<glm::ivec2>& p = chain.points;
    const size_t n = p.size();
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    if (p[0] != p[n - 1]) {
        douglasPeucker(p, 0, n - 1, epsilon, keep);
    } else {
        auto distance2 = [](glm::ivec2 a, glm::ivec2 b) {
            glm::i64vec2 d = glm::i64vec2(b) - glm::i64vec2(a);
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
            const glm::i64vec2 a(p[0]), d = glm::i64vec2(p[far]) - a;
            size_t third = 0;
            int64_t best = -1;
            for (size_t k = 1; k + 1 < n; k++) {
                const glm::i64vec2 q = glm::i64vec2(p[k]) - a;
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
int orientation(glm::ivec2 a, glm::ivec2 b, glm::ivec2 c) {
    const int64_t v = int64_t(b.x - a.x) * (c.y - a.y) - int64_t(b.y - a.y) * (c.x - a.x);
    return (v > 0) - (v < 0);
}

// Whether p lies on the closed segment ab (p collinear with a and b).
bool onSegment(glm::ivec2 a, glm::ivec2 b, glm::ivec2 p) {
    return orientation(a, b, p) == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x)
        && std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y);
}

// Whether two constraint segments conflict: they may only meet at a shared
// end point, without overlapping. A shared end with collinear overlap (two
// chains collapsed onto one line, or a chain folding back on itself), the
// same segment twice, a crossing, or a point touching the other segment's
// inside all conflict.
bool conflict(glm::ivec2 a, glm::ivec2 b, glm::ivec2 c, glm::ivec2 d) {
    const bool sharesA = a == c || a == d, sharesB = b == c || b == d;
    if (sharesA && sharesB) return true;
    if (sharesA || sharesB) {
        const glm::ivec2 s = sharesA ? a : b, p = sharesA ? b : a, q = s == c ? d : c;
        const glm::i64vec2 u = glm::i64vec2(p) - glm::i64vec2(s), v = glm::i64vec2(q) - glm::i64vec2(s);
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
            const std::vector<glm::ivec2>& p = chains[i].points;
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

using Triangles = std::vector<std::array<glm::dvec2, 3>>;

Triangles triangulate(const std::vector<Chain>& chains, int w) {
    std::vector<CDT::V2d<double>> vertices;
    std::vector<CDT::Edge> edges;
    std::unordered_map<long, CDT::VertInd> index;
    auto vertex = [&](glm::ivec2 p) {
        auto [it, added] = index.try_emplace(long(p.y) * (w + 1) + p.x, CDT::VertInd(vertices.size()));
        if (added) vertices.push_back(CDT::V2d<double>(p.x, p.y));
        return it->second;
    };
    for (const Chain& chain : chains) {
        for (size_t i = 0; i + 1 < chain.points.size(); i++) {
            edges.push_back(CDT::Edge(vertex(chain.points[i]), vertex(chain.points[i + 1])));
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
            std::array<glm::dvec2, 3> triangle;
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
        ofLogWarning("Mosaic") << "constrained triangulation failed (" << e.what()
                               << "), Tessels may cross Patches in this Frame";
        try {
            return run(false);
        } catch (const std::exception& e) {
            ofLogError("Mosaic") << "triangulation failed: " << e.what();
            return {};
        }
    }
}

// Mean colour of the pixels whose centre lies inside the triangle; a centre
// on an edge counts for the triangle the edge is a top or left edge of, so
// each pixel counts once. The pixel at the centroid if no centre is inside.
ofColor meanColour(const ofPixels& rgb, std::array<glm::dvec2, 3> t) {
    const int w = rgb.getWidth(), h = rgb.getHeight();
    auto cross = [](glm::dvec2 a, glm::dvec2 b, glm::dvec2 p) {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    };
    if (cross(t[0], t[1], t[2]) < 0) std::swap(t[1], t[2]);
    const int x0 = std::max(0, int(std::floor(std::min({t[0].x, t[1].x, t[2].x}) - 0.5)));
    const int x1 = std::min(w - 1, int(std::ceil(std::max({t[0].x, t[1].x, t[2].x}))));
    const int y0 = std::max(0, int(std::floor(std::min({t[0].y, t[1].y, t[2].y}) - 0.5)));
    const int y1 = std::min(h - 1, int(std::ceil(std::max({t[0].y, t[1].y, t[2].y}))));
    const unsigned char* data = rgb.getData();
    uint64_t r = 0, g = 0, b = 0, count = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            glm::dvec2 centre(x + 0.5, y + 0.5);
            bool inside = true;
            for (int k = 0; k < 3 && inside; k++) {
                glm::dvec2 a = t[k], e = t[(k + 1) % 3] - a;
                double side = cross(a, a + e, centre);
                bool topLeft = (e.y == 0 && e.x < 0) || e.y > 0;
                inside = side > 0 || (side == 0 && topLeft);
            }
            if (!inside) continue;
            const unsigned char* p = data + (size_t(y) * w + x) * 3;
            r += p[0];
            g += p[1];
            b += p[2];
            count++;
        }
    }
    if (count == 0) {
        glm::dvec2 c = (t[0] + t[1] + t[2]) / 3.0;
        return rgb.getColor(ofClamp(int(c.x), 0, w - 1), ofClamp(int(c.y), 0, h - 1));
    }
    auto mean = [&](uint64_t sum) { return (unsigned char)((sum + count / 2) / count); };
    return ofColor(mean(r), mean(g), mean(b));
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

Mosaic make(const ofPixels& rgb, int minPatchArea) {
    const int w = rgb.getWidth(), h = rgb.getHeight();
    const Sizes sizes = sizesFor(w, h, minPatchArea);
    cv::Mat lab = blurredLab(rgb, sizes.blurSigma);
    int count = 0;
    std::vector<int> label = felzenszwalb(lab, count);
    count = joinEqualColours(rgb, label, count);
    wardMerge(lab, label, count, sizes.minPatchArea);

    std::vector<Chain> exact = traceChains(label, w, h);
    for (Chain& chain : exact) mergeCollinear(chain);
    std::vector<Chain> chains = simplifyChains(exact, w, h, sizes.boundaryEpsilon);
    Mosaic mosaic;
    for (size_t i = 0; i < chains.size(); i++) {
        if (onBorder(exact[i], w, h)) continue;
        const Chain& chain = chains[i];
        ofPolyline contour;
        size_t n = chain.points.size() - (chain.closed ? 1 : 0);
        for (size_t i = 0; i < n; i++) contour.addVertex(chain.points[i].x, chain.points[i].y);
        contour.setClosed(chain.closed);
        mosaic.contours.push_back(contour);
    }

    for (const auto& triangle : triangulate(chains, w)) {
        Tessel tessel;
        for (int k = 0; k < 3; k++) tessel.triangle[k] = glm::vec3(triangle[k].x, triangle[k].y, 0);
        tessel.color = meanColour(rgb, triangle);
        mosaic.tessels.push_back(tessel);
    }
    return mosaic;
}

} // namespace adaptive
