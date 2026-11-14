// Standalone benchmark for research ticket #14 (spippoli/hipsterismo):
// greedy error-driven vertex insertion into a Delaunay triangulation of a
// 320x240 Frame, starting from the 4 Frame corners, capped at ~5000 vertices.
//
// Candidates compared:
//   bw        hand-written Bowyer-Watson on a triangle/neighbour array,
//             exact predicates on a half-pixel grid, O(1) point location
//             (the candidate's own triangle is the walk start)
//   subdiv    cv::Subdiv2D::insert (OpenCV 4, quad-edge, Guibas-Stolfi)
//   dlt-K%    delaunator-cpp rebuilt from scratch after each batch of
//             insertions; batch = K% of the current triangle count
//   bourke    the Paul Bourke / Gilles Dumoulin Bowyer-Watson used by
//             ofxDelaunay (libs/Delaunay/src/Delaunay.cpp), one batch call
//
// The per-triangle work is the one decided in #9: mean Lab colour of the
// pixels whose centres the triangle owns (top-left rule), and the pixel of
// maximum Lab deviation from that mean (the colour-vertex candidate).  The
// error measure is undecided (#10); max deltaE is used as a stand-in.
//
// Build:
//   g++ -std=c++20 -O3 greedy_insertion_bench.cpp <OF_ROOT>/addons/ofxDelaunay/libs/Delaunay/src/Delaunay.cpp \
//       -I<OF_ROOT>/addons/ofxDelaunay/libs/Delaunay/src -I<delaunator-cpp>/include \
//       $(pkg-config --cflags --libs opencv4) -o bench
// Run:   [SCAN=span] [SUBDIV_MARGIN=N] [BORDER_SNAP=1] ./bench tests/fixtures/fingers.png [reps]
// Columns: "new/ins" = triangles (re)scanned per inserted vertex; "maxnew" =
// largest star of one insertion (for dlt-*: number of rebuilds); "px visit" =
// pixel visits by the scans (bbox mode counts both passes, span mode only the
// max-deviation pass); "stale" = heap entries whose triangle was already gone.

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "Delaunay.h"      // ofxDelaunay's Bourke implementation
#include "delaunator.hpp"  // delaunator-cpp (MIT)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ---------------------------------------------------------------- settings
static const int kCap = 5000;      // vertex cap, corners included (#9)
static double gMaxArea = 1024.0;   // max tessel area, px^2 (hard)
static double gMinArea = 4.0;      // min tessel area, px^2 (soft)
static double gThreshold = 8.0;    // max deltaE tolerance (stand-in for #10)
static bool gSpanScan = false;     // SCAN=span: row spans + prefix sums instead of bbox tests
static int gSubdivMargin = 0;      // SUBDIV_MARGIN=N: grow the Subdiv2D rect by N px per side
static bool gBorderSnap = false;   // BORDER_SNAP=1: a candidate in the outer pixel ring moves onto the border

struct P { double x, y; };

static inline double orient(P a, P b, P c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
// > 0 when d is strictly inside the circumcircle of the positively oriented abc.
// Exact for coordinates on a half-pixel grid in [0, 640] (all products are
// multiples of 1/16 below 2^53 / 16).
static inline double incircle(P a, P b, P c, P d) {
    double adx = a.x - d.x, ady = a.y - d.y;
    double bdx = b.x - d.x, bdy = b.y - d.y;
    double cdx = c.x - d.x, cdy = c.y - d.y;
    double al = adx * adx + ady * ady, bl = bdx * bdx + bdy * bdy, cl = cdx * cdx + cdy * cdy;
    return adx * (bdy * cl - bl * cdy) - ady * (bdx * cl - bl * cdx) + al * (bdx * cdy - bdy * cdx);
}
static inline double snapHalf(double v) { return std::round(v * 2.0) / 2.0; }
static inline uint64_t packPt(P p) {
    return (uint64_t)(uint32_t)(int)std::lround(p.x * 2) << 32 | (uint32_t)(int)std::lround(p.y * 2);
}
static inline uint64_t mix(uint64_t x) {  // splitmix64 finaliser
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// ------------------------------------------------------ per-triangle stats
struct Frame {
    int W, H;
    cv::Mat lab;               // CV_32FC3
    std::vector<double> pre;   // per-row prefix sums of lab, H x (W+1) x 3
    std::vector<uint8_t> isVertex;  // pixel centre already used as a vertex
};

struct Stats {
    double area = 0;
    int n = 0;
    float mean[3] = {0, 0, 0};
    double err = 0;            // max deltaE from the mean
    P cand{-1, -1};            // max-deviation pixel centre
    int cls = 0;               // 2 = above max area, 1 = colour, 0 = done
    double key = 0;
};

struct Counters {
    long long trianglesScanned = 0, pixelsVisited = 0, bboxPixels = 0;
    long long insertions = 0, newTris = 0, maxNewTris = 0;
    long long staleSkips = 0;
    long long poppedPixels = 0;  // pixels owned by the triangles that received a vertex
    double scanMs = 0, topoMs = 0;
};

// top-left ownership: an edge a->b of a positively oriented triangle owns
// the pixel centres lying exactly on it when this is true.  The two
// triangles that share an edge traverse it in opposite directions, so
// exactly one of them owns such a centre.
static inline bool ownsEdge(P a, P b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return dy > 0 || (dy == 0 && dx < 0);
}

template <class F>
static void forEachPixel(const Frame& f, P a, P b, P c, Counters* ctr, F fn) {
    if (orient(a, b, c) < 0) std::swap(b, c);
    int x0 = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x})));
    int x1 = std::min(f.W - 1, (int)std::ceil(std::max({a.x, b.x, c.x})));
    int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})));
    int y1 = std::min(f.H - 1, (int)std::ceil(std::max({a.y, b.y, c.y})));
    bool oa = ownsEdge(b, c), ob = ownsEdge(c, a), oc = ownsEdge(a, b);
    for (int y = y0; y <= y1; ++y) {
        P q{0, y + 0.5};
        for (int x = x0; x <= x1; ++x) {
            q.x = x + 0.5;
            if (ctr) ctr->bboxPixels++;
            double w0 = orient(b, c, q), w1 = orient(c, a, q), w2 = orient(a, b, q);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if ((w0 == 0 && !oa) || (w1 == 0 && !ob) || (w2 == 0 && !oc)) continue;
            fn(x, y);
        }
    }
}

// Owned pixels of one row of a convex triangle are contiguous: find the span
// [xa, xb] from the edge intersections, then fix its ends with the exact test.
template <class F>
static void forEachSpan(const Frame& f, P a, P b, P c, Counters* ctr, F fn) {
    if (orient(a, b, c) < 0) std::swap(b, c);
    int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})));
    int y1 = std::min(f.H - 1, (int)std::ceil(std::max({a.y, b.y, c.y})));
    bool oa = ownsEdge(b, c), ob = ownsEdge(c, a), oc = ownsEdge(a, b);
    auto inside = [&](int x, double cy) {
        P q{x + 0.5, cy};
        double w0 = orient(b, c, q), w1 = orient(c, a, q), w2 = orient(a, b, q);
        if (w0 < 0 || w1 < 0 || w2 < 0) return false;
        return !((w0 == 0 && !oa) || (w1 == 0 && !ob) || (w2 == 0 && !oc));
    };
    const P v[3] = {a, b, c};
    for (int y = y0; y <= y1; ++y) {
        double cy = y + 0.5, xl = 1e30, xr = -1e30;
        for (int i = 0; i < 3; ++i) {
            P p = v[i], q = v[(i + 1) % 3];
            if (p.y == q.y || cy < std::min(p.y, q.y) || cy > std::max(p.y, q.y)) continue;
            double x = p.x + (cy - p.y) * (q.x - p.x) / (q.y - p.y);
            xl = std::min(xl, x); xr = std::max(xr, x);
        }
        if (xl > xr) continue;
        int lo = std::max(0, (int)std::floor(xl - 0.5) - 1), hi = std::min(f.W - 1, (int)std::ceil(xr - 0.5) + 1);
        int xa = lo, xb = hi;
        while (xa <= hi && !inside(xa, cy)) ++xa;
        while (xb >= xa && !inside(xb, cy)) --xb;
        if (ctr) ctr->bboxPixels += (xa - lo) + (hi - xb) + 2;
        if (xa <= xb) fn(y, xa, xb);
    }
}

static Stats computeStats(const Frame& f, P a, P b, P c, uint64_t seed, Counters& ctr) {
    auto t0 = Clock::now();
    Stats s;
    s.area = std::fabs(orient(a, b, c)) * 0.5;
    double sum[3] = {0, 0, 0};
    if (gSpanScan) {
        thread_local std::vector<std::array<int, 3>> spans;
        spans.clear();
        forEachSpan(f, a, b, c, &ctr, [&](int y, int xa, int xb) {
            spans.push_back({y, xa, xb});
            const double* r = &f.pre[(size_t)y * (f.W + 1) * 3];
            for (int k = 0; k < 3; ++k) sum[k] += r[3 * (xb + 1) + k] - r[3 * xa + k];
            s.n += xb - xa + 1;
        });
        ctr.trianglesScanned++;
        if (s.n > 0) {
            for (int k = 0; k < 3; ++k) s.mean[k] = (float)(sum[k] / s.n);
            float best = -1;
            for (auto& sp : spans) {
                const float* row = f.lab.ptr<float>(sp[0]);
                const uint8_t* used = &f.isVertex[(size_t)sp[0] * f.W];
                for (int x = sp[1]; x <= sp[2]; ++x) {
                    const float* p = row + 3 * x;
                    float d0 = p[0] - s.mean[0], d1 = p[1] - s.mean[1], d2 = p[2] - s.mean[2];
                    float d = d0 * d0 + d1 * d1 + d2 * d2;
                    if (d > best && !used[x]) { best = d; s.cand = {x + 0.5, sp[0] + 0.5}; }
                }
            }
            ctr.pixelsVisited += s.n;
            s.err = best > 0 ? std::sqrt(best) : 0;
        }
    } else {
    forEachPixel(f, a, b, c, &ctr, [&](int x, int y) {
        const float* p = f.lab.ptr<float>(y) + 3 * x;
        sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
        s.n++;
    });
    ctr.pixelsVisited += s.n;
    ctr.trianglesScanned++;
    if (s.n > 0) {
        for (int k = 0; k < 3; ++k) s.mean[k] = (float)(sum[k] / s.n);
        double best = -1;
        forEachPixel(f, a, b, c, nullptr, [&](int x, int y) {
            if (f.isVertex[y * f.W + x]) return;
            const float* p = f.lab.ptr<float>(y) + 3 * x;
            double d0 = p[0] - s.mean[0], d1 = p[1] - s.mean[1], d2 = p[2] - s.mean[2];
            double d = d0 * d0 + d1 * d1 + d2 * d2;
            if (d > best) { best = d; s.cand = {x + 0.5, y + 0.5}; }
        });
        ctr.pixelsVisited += s.n;
        s.err = best > 0 ? std::sqrt(best) : 0;
    }
    }
    if (s.area > gMaxArea) {
        // centroid plus a deterministic barycentric jitter, snapped to the half-pixel grid
        uint64_t h = mix(seed);
        double j1 = ((h & 0xffff) / 65535.0 - 0.5) * 0.2, j2 = (((h >> 16) & 0xffff) / 65535.0 - 0.5) * 0.2;
        double wa = 1.0 / 3 + j1, wb = 1.0 / 3 + j2, wc = 1.0 - wa - wb;
        s.cand = {snapHalf(wa * a.x + wb * b.x + wc * c.x), snapHalf(wa * a.y + wb * b.y + wc * c.y)};
        s.cls = 2; s.key = s.area;
    } else if (s.area >= gMinArea && s.err > gThreshold && s.cand.x >= 0) {
        s.cls = 1; s.key = s.err;
        if (gBorderSnap) {
            if (s.cand.x == 0.5) s.cand.x = 0; else if (s.cand.x == f.W - 0.5) s.cand.x = f.W;
            if (s.cand.y == 0.5) s.cand.y = 0; else if (s.cand.y == f.H - 0.5) s.cand.y = f.H;
        }
    }
    ctr.scanMs += msSince(t0);
    return s;
}

// remember that a pixel centre (or the border point snapped from it) is a vertex
static void markVertex(Frame& f, P p) {
    bool centre = p.x - std::floor(p.x) == 0.5 && p.y - std::floor(p.y) == 0.5;
    bool border = p.x == 0 || p.y == 0 || p.x == f.W || p.y == f.H;
    if (!centre && !border) return;
    int x = std::clamp((int)std::floor(p.x), 0, f.W - 1), y = std::clamp((int)std::floor(p.y), 0, f.H - 1);
    f.isVertex[y * f.W + x] = 1;
}

struct HeapItem {
    int cls; double key; int id;
    bool operator<(const HeapItem& o) const {  // max-heap; ties broken by id, so deterministic
        if (cls != o.cls) return cls < o.cls;
        if (key != o.key) return key < o.key;
        return id > o.id;
    }
};

struct Result {
    std::vector<std::array<P, 3>> tris;
    int vertices = 0;
    int nonDelaunay = -1;  // bw only: interior edges failing the empty-circumcircle test
    Counters ctr;
    double totalMs = 0;
};

// ============================================== hand-written Bowyer-Watson
struct BW {
    struct T { int v[3]; int n[3]; bool alive; };  // n[i] is opposite v[i]
    std::vector<P> pts;
    std::vector<T> tris;
    std::vector<int> cavity, stamp;
    int stampNow = 0;
    struct BEdge { int a, b, outer, outerSlot; };
    std::vector<BEdge> boundary;

    void init(int W, int H) {
        pts = {{0, 0}, {(double)W, 0}, {(double)W, (double)H}, {0, (double)H}};
        // y down: (0,0),(W,0),(W,H) has positive orient
        tris.push_back({{0, 1, 2}, {-1, 1, -1}, true});
        tris.push_back({{0, 2, 3}, {-1, -1, 0}, true});
    }
    int slotOf(int t, int nb) const {
        for (int i = 0; i < 3; ++i) if (tris[t].n[i] == nb) return i;
        return -1;
    }
    // visibility walk; terminates on a Delaunay triangulation
    int locate(int t, P p) const {
        for (;;) {
            const T& tr = tris[t];
            int next = -1;
            for (int i = 0; i < 3; ++i) {
                P a = pts[tr.v[(i + 1) % 3]], b = pts[tr.v[(i + 2) % 3]];
                if (orient(a, b, p) < 0) { next = tr.n[i]; break; }
            }
            if (next < 0) return t;
            t = next;
        }
    }
    // returns the ids of the new triangles in `out`
    void insert(int t0, P p, std::vector<int>& out, std::vector<int>& dead) {
        t0 = locate(t0, p);
        int pi = (int)pts.size();
        pts.push_back(p);
        if ((int)stamp.size() < (int)tris.size()) stamp.resize(tris.size() * 2, 0);
        ++stampNow;
        cavity.clear(); boundary.clear();
        cavity.push_back(t0); stamp[t0] = stampNow;
        // flood fill: triangles whose circumcircle strictly contains p
        for (size_t k = 0; k < cavity.size(); ++k) {
            int t = cavity[k];
            for (int i = 0; i < 3; ++i) {
                int nb = tris[t].n[i];
                if (nb < 0 || stamp[nb] == stampNow) continue;
                const T& u = tris[nb];
                if (incircle(pts[u.v[0]], pts[u.v[1]], pts[u.v[2]], p) > 0) {
                    stamp[nb] = stampNow; cavity.push_back(nb);
                }
            }
        }
        for (int t : cavity) {
            for (int i = 0; i < 3; ++i) {
                int nb = tris[t].n[i];
                if (nb >= 0 && stamp[nb] == stampNow) continue;
                boundary.push_back({tris[t].v[(i + 1) % 3], tris[t].v[(i + 2) % 3], nb,
                                    nb >= 0 ? slotOf(nb, t) : -1});
            }
            tris[t].alive = false;
            dead.push_back(t);
        }
        out.clear();
        for (const BEdge& e : boundary) {
            if (orient(pts[e.a], pts[e.b], p) == 0) continue;  // p on a hull edge
            int id = (int)tris.size();
            tris.push_back({{pi, e.a, e.b}, {e.outer, -1, -1}, true});
            if (e.outer >= 0) tris[e.outer].n[e.outerSlot] = id;
            out.push_back(id);
        }
        // link the fan: new triangle (p,a,b) has edge p-a opposite b (slot 2)
        // and edge p-b opposite a (slot 1)
        for (int i : out)
            for (int j : out) {
                if (i == j) continue;
                if (tris[i].v[2] == tris[j].v[1]) { tris[i].n[1] = j; tris[j].n[2] = i; }
            }
        if (stamp.size() < tris.size()) stamp.resize(tris.size() * 2, 0);
    }
};

static Result runBW(Frame f) {
    Result r;
    auto t0 = Clock::now();
    Counters& c = r.ctr;
    BW bw;
    bw.init(f.W, f.H);
    std::unordered_set<uint64_t> used;
    for (auto& p : bw.pts) used.insert(packPt(p));
    std::vector<Stats> st;
    std::priority_queue<HeapItem> heap;
    auto add = [&](int t) {
        if ((int)st.size() <= t) st.resize(t + 1);
        const auto& T = bw.tris[t];
        st[t] = computeStats(f, bw.pts[T.v[0]], bw.pts[T.v[1]], bw.pts[T.v[2]],
                             mix(T.v[0]) ^ mix(T.v[1] * 7919ULL) ^ mix(T.v[2] * 104729ULL), c);
        if (st[t].cls) heap.push({st[t].cls, st[t].key, t});
    };
    add(0); add(1);
    std::vector<int> out, dead;
    while (!heap.empty() && (int)bw.pts.size() < kCap) {
        HeapItem h = heap.top(); heap.pop();
        if (!bw.tris[h.id].alive) { c.staleSkips++; continue; }
        P p = st[h.id].cand;
        if (!used.insert(packPt(p)).second) continue;  // duplicate: drop candidate
        auto tt = Clock::now();
        dead.clear();
        bw.insert(h.id, p, out, dead);
        c.topoMs += msSince(tt);
        markVertex(f, p);
        c.insertions++;
        c.poppedPixels += st[h.id].n;
        c.newTris += out.size();
        c.maxNewTris = std::max<long long>(c.maxNewTris, out.size());
        for (int t : out) add(t);
    }
    r.totalMs = msSince(t0);
    r.vertices = (int)bw.pts.size();
    r.nonDelaunay = 0;
    for (auto& T : bw.tris) {
        if (!T.alive) continue;
        r.tris.push_back({bw.pts[T.v[0]], bw.pts[T.v[1]], bw.pts[T.v[2]]});
        for (int i = 0; i < 3; ++i) {
            if (T.n[i] < 0) continue;
            const auto& U = bw.tris[T.n[i]];
            for (int j = 0; j < 3; ++j)
                if (U.v[j] != T.v[(i + 1) % 3] && U.v[j] != T.v[(i + 2) % 3] &&
                    incircle(bw.pts[T.v[0]], bw.pts[T.v[1]], bw.pts[T.v[2]], bw.pts[U.v[j]]) > 0)
                    r.nonDelaunay++;
        }
    }
    return r;
}

// ============================================================= cv::Subdiv2D
static Result runSubdiv(Frame f) {
    Result r;
    auto t0 = Clock::now();
    Counters& c = r.ctr;
    // insert() and locate() reject x >= rect.x + width, so the far corner
    // (W, H) needs a rect one pixel larger than the Frame.
    const int m = gSubdivMargin;
    cv::Subdiv2D sd(cv::Rect(-m, -m, f.W + 1 + 2 * m, f.H + 1 + 2 * m));
    const int firstReal = 4;  // ids 1..3 are the virtual super-triangle vertices
    std::unordered_set<uint64_t> used;
    P corners[4] = {{0, 0}, {(double)f.W, 0}, {(double)f.W, (double)f.H}, {0, (double)f.H}};
    for (P p : corners) { sd.insert(cv::Point2f((float)p.x, (float)p.y)); used.insert(packPt(p)); }

    struct Rec { int v[3]; Stats s; };
    std::vector<Rec> recs;
    std::priority_queue<HeapItem> heap;
    auto vpt = [&](int v) { cv::Point2f q = sd.getVertex(v); return P{q.x, q.y}; };
    auto addFace = [&](int a, int b, int cc) {
        if (a < firstReal || b < firstReal || cc < firstReal) return false;
        int v[3] = {a, b, cc};
        std::rotate(v, std::min_element(v, v + 3), v + 3);  // keep cyclic order
        Rec rec{{v[0], v[1], v[2]}, {}};
        rec.s = computeStats(f, vpt(v[0]), vpt(v[1]), vpt(v[2]),
                             mix(v[0]) ^ mix(v[1] * 7919ULL) ^ mix(v[2] * 104729ULL), c);
        recs.push_back(rec);
        if (rec.s.cls) heap.push({rec.s.cls, rec.s.key, (int)recs.size() - 1});
        return true;
    };
    // the faces incident to vertex v (after an insertion: exactly the new triangles)
    auto starFaces = [&](int v) {
        int e0 = 0;
        sd.getVertex(v, &e0);
        int e = e0, n = 0;
        do {
            int b = sd.edgeDst(e);
            int cc = sd.edgeDst(sd.getEdge(e, cv::Subdiv2D::NEXT_AROUND_LEFT));
            if (addFace(v, b, cc)) n++;
            e = sd.nextEdge(e);
        } while (e != e0);
        return n;
    };
    // is the face (a,b,c) still there?  edge a->b must exist with c on its left
    auto alive = [&](const int* v) {
        int e0 = 0;
        sd.getVertex(v[0], &e0);
        int e = e0;
        do {
            if (sd.edgeDst(e) == v[1])
                return sd.edgeDst(sd.getEdge(e, cv::Subdiv2D::NEXT_AROUND_LEFT)) == v[2];
            e = sd.nextEdge(e);
        } while (e != e0);
        return false;
    };
    {   // initial faces: walk every corner's star, dedupe by triple
        for (int v = firstReal; v < firstReal + 4; ++v) starFaces(v);
        std::sort(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) {
            return std::lexicographical_compare(x.v, x.v + 3, y.v, y.v + 3); });
        recs.erase(std::unique(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) {
            return std::equal(x.v, x.v + 3, y.v); }), recs.end());
        heap = {};
        for (int i = 0; i < (int)recs.size(); ++i) if (recs[i].s.cls) heap.push({recs[i].s.cls, recs[i].s.key, i});
    }
    int nv = 4;
    while (!heap.empty() && nv < kCap) {
        HeapItem h = heap.top(); heap.pop();
        if (!alive(recs[h.id].v)) { c.staleSkips++; continue; }
        P p = recs[h.id].s.cand;
        if (!used.insert(packPt(p)).second) continue;
        auto tt = Clock::now();
        int id = sd.insert(cv::Point2f((float)p.x, (float)p.y));
        c.topoMs += msSince(tt);
        nv++;
        markVertex(f, p);
        int n = starFaces(id);  // star walk: counted in total, not in topo
        c.insertions++;
        c.newTris += n;
        c.maxNewTris = std::max<long long>(c.maxNewTris, n);
    }
    r.totalMs = msSince(t0);
    r.vertices = nv;
    std::vector<cv::Vec6f> tl;
    sd.getTriangleList(tl);
    for (auto& t : tl) {  // with a margin, getTriangleList also returns triangles outside the Frame
        bool in = true;
        for (int k = 0; k < 6; k += 2) if (t[k] < 0 || t[k] > f.W || t[k + 1] < 0 || t[k + 1] > f.H) in = false;
        if (in) r.tris.push_back({P{t[0], t[1]}, P{t[2], t[3]}, P{t[4], t[5]}});
    }
    return r;
}

// ================================================= delaunator-cpp in batches
static Result runDelaunatorBatch(Frame f, double frac) {
    Result r;
    auto t0 = Clock::now();
    Counters& c = r.ctr;
    std::vector<double> coords = {0, 0, (double)f.W, 0, (double)f.W, (double)f.H, 0, (double)f.H};
    std::unordered_set<uint64_t> used;
    for (int i = 0; i < 4; ++i) used.insert(packPt({coords[2 * i], coords[2 * i + 1]}));
    std::unordered_map<uint64_t, Stats> cache, next;
    auto key3 = [](size_t a, size_t b, size_t cc) {
        size_t v[3] = {a, b, cc}; std::sort(v, v + 3);
        return (uint64_t)v[0] << 42 | (uint64_t)v[1] << 21 | v[2]; };
    int rounds = 0;
    std::vector<size_t> tri;
    for (;;) {
        auto tt = Clock::now();
        delaunator::Delaunator d(coords);
        c.topoMs += msSince(tt);
        rounds++;
        tri = d.triangles;
        next.clear();
        std::vector<HeapItem> cands;
        for (size_t i = 0; i < tri.size(); i += 3) {
            uint64_t k = key3(tri[i], tri[i + 1], tri[i + 2]);
            auto it = cache.find(k);
            Stats s;
            if (it != cache.end()) s = it->second;
            else {
                P a{coords[2 * tri[i]], coords[2 * tri[i] + 1]}, b{coords[2 * tri[i + 1]], coords[2 * tri[i + 1] + 1]},
                  cc{coords[2 * tri[i + 2]], coords[2 * tri[i + 2] + 1]};
                s = computeStats(f, a, b, cc, mix(k), c);
                c.newTris++;
            }
            next.emplace(k, s);
            if (s.cls) cands.push_back({s.cls, s.key, (int)(i / 3)});
        }
        std::swap(cache, next);
        int nv = (int)coords.size() / 2;
        if (cands.empty() || nv >= kCap) break;
        size_t k = std::max<size_t>(1, (size_t)std::ceil(frac * (tri.size() / 3)));
        k = std::min({k, cands.size(), (size_t)(kCap - nv)});
        std::partial_sort(cands.begin(), cands.begin() + k, cands.end(),
                          [](const HeapItem& x, const HeapItem& y) { return y < x; });
        for (size_t j = 0; j < k; ++j) {
            int t = cands[j].id;
            const Stats& s = cache[key3(tri[3 * t], tri[3 * t + 1], tri[3 * t + 2])];
            if (!used.insert(packPt(s.cand)).second) continue;
            coords.push_back(s.cand.x); coords.push_back(s.cand.y);
            markVertex(f, s.cand);
            c.insertions++;
        }
    }
    r.totalMs = msSince(t0);
    r.vertices = (int)coords.size() / 2;
    r.ctr.maxNewTris = rounds;  // reused: number of rebuilds
    for (size_t i = 0; i < tri.size(); i += 3)
        r.tris.push_back({P{coords[2 * tri[i]], coords[2 * tri[i] + 1]}, P{coords[2 * tri[i + 1]], coords[2 * tri[i + 1] + 1]},
                          P{coords[2 * tri[i + 2]], coords[2 * tri[i + 2] + 1]}});
    return r;
}

// ============================================ Bourke (ofxDelaunay), one call
static std::vector<std::array<P, 3>> runBourke(const std::vector<P>& in, double& ms) {
    std::vector<P> s = in;
    std::sort(s.begin(), s.end(), [](P a, P b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    int nv = (int)s.size();
    std::vector<XYZ> v(nv + 3);
    for (int i = 0; i < nv; ++i) v[i] = {s[i].x, s[i].y, 0};
    std::vector<ITRIANGLE> t(3 * nv);
    int ntri = 0;
    auto t0 = Clock::now();
    Triangulate(nv, v.data(), t.data(), ntri);
    ms = msSince(t0);
    std::vector<std::array<P, 3>> out;
    for (int i = 0; i < ntri; ++i) out.push_back({s[t[i].p1], s[t[i].p2], s[t[i].p3]});
    return out;
}

// ================================================================ checks
struct Coverage { double areaSum; int zeroArea; long long uncovered, multiply; };
static Coverage checkCoverage(const Frame& f, const std::vector<std::array<P, 3>>& tris) {
    Coverage cv{0, 0, 0, 0};
    std::vector<int> owner(f.W * f.H, 0);
    for (auto& t : tris) {
        double a = std::fabs(orient(t[0], t[1], t[2])) * 0.5;
        cv.areaSum += a;
        if (a == 0) { cv.zeroArea++; continue; }
        forEachPixel(f, t[0], t[1], t[2], nullptr, [&](int x, int y) { owner[y * f.W + x]++; });
    }
    for (int o : owner) { if (o == 0) cv.uncovered++; else if (o > 1) cv.multiply++; }
    return cv;
}
static uint64_t meshHash(std::vector<std::array<P, 3>> tris) {
    std::vector<std::array<uint64_t, 3>> k;
    for (auto& t : tris) {
        std::array<uint64_t, 3> a = {packPt(t[0]), packPt(t[1]), packPt(t[2])};
        std::sort(a.begin(), a.end());
        k.push_back(a);
    }
    std::sort(k.begin(), k.end());
    uint64_t h = 1469598103934665603ULL;
    for (auto& a : k) for (auto x : a) h = mix(h ^ x);
    return h;
}
static std::vector<P> vertsOf(const std::vector<std::array<P, 3>>& tris) {
    std::unordered_set<uint64_t> seen; std::vector<P> v;
    for (auto& t : tris) for (auto& p : t) if (seen.insert(packPt(p)).second) v.push_back(p);
    return v;
}

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s image.png [reps]\n", argv[0]); return 2; }
    int reps = argc > 2 ? std::atoi(argv[2]) : 15;
    if (const char* e = std::getenv("SCAN")) gSpanScan = std::string(e) == "span";
    if (const char* e = std::getenv("SUBDIV_MARGIN")) gSubdivMargin = std::atoi(e);
    if (const char* e = std::getenv("BORDER_SNAP")) gBorderSnap = std::atoi(e) != 0;
    std::printf("scan mode: %s, Subdiv2D margin: %d px, border snap: %s\n", gSpanScan ? "row spans + prefix sums" : "bbox edge tests, two passes", gSubdivMargin, gBorderSnap ? "on" : "off");
    cv::Mat bgr = cv::imread(argv[1]);
    if (bgr.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    cv::resize(bgr, bgr, cv::Size(320, 240));
    cv::Mat noise(240, 320, CV_8UC3);
    cv::RNG rng(12345);
    rng.fill(noise, cv::RNG::UNIFORM, 0, 256);

    struct Scenario { const char* name; cv::Mat img; double thr; };
    std::vector<Scenario> sc = {
        {"fingers T=8", bgr, 8.0},
        {"fingers T=0 (cap)", bgr, 0.0},
        {"noise T=0 (cap)", noise, 0.0},
    };
    for (auto& s : sc) {
        Frame f;
        f.W = s.img.cols; f.H = s.img.rows;
        auto tl = Clock::now();
        cv::Mat flt;
        std::vector<double> labMs;
        for (int i = 0; i < reps; ++i) {
            auto t = Clock::now();
            s.img.convertTo(flt, CV_32F, 1.0 / 255);
            cv::cvtColor(flt, f.lab, cv::COLOR_BGR2Lab);
            f.pre.assign((size_t)f.H * (f.W + 1) * 3, 0.0);
            for (int y = 0; y < f.H; ++y) {
                const float* row = f.lab.ptr<float>(y);
                double* r = &f.pre[(size_t)y * (f.W + 1) * 3];
                for (int x = 0; x < f.W; ++x)
                    for (int k = 0; k < 3; ++k) r[3 * (x + 1) + k] = r[3 * x + k] + row[3 * x + k];
            }
            labMs.push_back(msSince(t));
        }
        (void)tl;
        f.isVertex.assign(f.W * f.H, 0);
        gThreshold = s.thr;
        std::printf("\n== %s  (%dx%d, max area %.0f, min area %.0f, cap %d)  BGR->Lab float + row prefix sums: %.3f ms\n",
                    s.name, f.W, f.H, gMaxArea, gMinArea, kCap, median(labMs));
        std::printf("%-10s %6s %6s %9s %9s %8s %8s %10s %9s %8s %7s %8s %9s %s\n", "method", "verts", "tris",
                    "total ms", "topo ms", "scan ms", "new/ins", "maxnew", "px visit", "stale", "area", "uncov", "multi", "hash");

        auto report = [&](const char* name, std::function<Result()> fn) {
            std::vector<double> tot, topo, scan;
            Result last;
            for (int i = 0; i < reps; ++i) {
                last = fn();
                tot.push_back(last.totalMs); topo.push_back(last.ctr.topoMs); scan.push_back(last.ctr.scanMs);
            }
            Coverage cv = checkCoverage(f, last.tris);
            const Counters& c = last.ctr;
            std::printf("%-10s %6d %6zu %9.2f %9.2f %8.2f %8.2f %10lld %9.2fM %8lld %7.0f %8lld %9lld %016llx\n", name,
                        last.vertices, last.tris.size(), median(tot), median(topo), median(scan),
                        c.insertions ? (double)c.newTris / c.insertions : 0.0, c.maxNewTris, c.pixelsVisited / 1e6,
                        c.staleSkips, cv.areaSum, cv.uncovered, cv.multiply, (unsigned long long)meshHash(last.tris));
            return last;
        };
        Result bw = report("bw", [&] { return runBW(f); });
        Result sd = report("subdiv", [&] { return runSubdiv(f); });
        report("dlt-1%", [&] { return runDelaunatorBatch(f, 0.01); });
        report("dlt-5%", [&] { return runDelaunatorBatch(f, 0.05); });
        report("dlt-20%", [&] { return runDelaunatorBatch(f, 0.20); });

        // one-shot rebuild costs on the final bw vertex set
        std::vector<P> v = vertsOf(bw.tris);
        std::vector<double> dms, bms;
        std::vector<std::array<P, 3>> bt;
        size_t dtris = 0;
        for (int i = 0; i < reps; ++i) {
            std::vector<double> co; for (P p : v) { co.push_back(p.x); co.push_back(p.y); }
            auto t = Clock::now();
            delaunator::Delaunator d(co);
            dms.push_back(msSince(t));
            dtris = d.triangles.size() / 3;
            double ms; bt = runBourke(v, ms); bms.push_back(ms);
        }
        Coverage bc = checkCoverage(f, bt);
        std::printf("one rebuild of %zu verts: delaunator %.3f ms (%zu tris), bourke/ofxDelaunay %.2f ms (%zu tris, area %.0f, zero-area %d, uncovered %lld, multiply %lld)\n",
                    v.size(), median(dms), dtris, median(bms), bt.size(), bc.areaSum, bc.zeroArea, bc.uncovered, bc.multiply);
        std::printf("bw non-Delaunay edges: %d; triangles scanned %lld, pixels owned by split triangles %.2fM (vs %.2fM scanned per pass)\n",
                    bw.nonDelaunay, bw.ctr.trianglesScanned, bw.ctr.poppedPixels / 1e6, bw.ctr.pixelsVisited / (gSpanScan ? 1e6 : 2e6));
        {   // vertices on the Frame border, and the triangles that own a border edge
            int onBorder = 0;
            for (P p : v) if (p.x == 0 || p.y == 0 || p.x == f.W || p.y == f.H) onBorder++;
            double maxLen = 0, maxH = 0;
            for (auto& t : bw.tris)
                for (int i = 0; i < 3; ++i) {
                    P a = t[i], b = t[(i + 1) % 3];
                    bool border = (a.y == 0 && b.y == 0) || (a.y == f.H && b.y == f.H) || (a.x == 0 && b.x == 0) || (a.x == f.W && b.x == f.W);
                    if (!border) continue;
                    double len = std::hypot(b.x - a.x, b.y - a.y);
                    maxLen = std::max(maxLen, len);
                    maxH = std::max(maxH, std::fabs(orient(t[0], t[1], t[2])) / len);
                }
            std::printf("bw border: %d vertices on the border, longest border edge %.1f px, tallest border triangle %.2f px\n",
                        onBorder, maxLen, maxH);
        }
        std::printf("bw vs subdiv identical mesh: %s\n", meshHash(bw.tris) == meshHash(sd.tris) ? "yes" : "no");
    }
    return 0;
}
