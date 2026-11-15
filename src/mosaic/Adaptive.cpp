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

cv::Mat blurredLab(const ofPixels& rgb) {
    cv::Mat source = ofxCv::toCv(rgb), unit, lab;
    source.convertTo(unit, CV_32F, 1.0 / 255);
    cv::cvtColor(unit, lab, cv::COLOR_RGB2Lab);
    cv::GaussianBlur(lab, lab, cv::Size(0, 0), blurSigma);
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
        // Exact crack chains never cross, so this should not happen. The
        // four Frame corners are vertices, so the unconstrained
        // triangulation still tiles the Frame.
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

Mosaic make(const ofPixels& rgb, int minPatchArea) {
    const int w = rgb.getWidth(), h = rgb.getHeight();
    cv::Mat lab = blurredLab(rgb);
    int count = 0;
    std::vector<int> label = felzenszwalb(lab, count);
    count = joinEqualColours(rgb, label, count);
    wardMerge(lab, label, count, minPatchArea);

    std::vector<Chain> chains = traceChains(label, w, h);
    Mosaic mosaic;
    for (Chain& chain : chains) {
        mergeCollinear(chain);
        if (onBorder(chain, w, h)) continue;
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
