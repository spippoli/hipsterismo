// Throwaway benchmark for issue #15: Patches first, then constrained triangles.
//
// Pipeline: Lab + blur -> Patch segmentation -> shared boundary chains on the
// pixel-corner lattice -> Douglas-Peucker per chain with crossing repair ->
// one constrained Delaunay triangulation of the whole Frame (artem-ogre/CDT)
// refined to a max triangle area -> Tessel colour = mean of the pixels whose
// centre lies in the triangle.
//
// Build (CDT 2.0.2, header-only):
//   git clone --depth 1 --branch 2.0.2 https://github.com/artem-ogre/CDT.git
//   g++ -std=c++20 -O3 -DNDEBUG -ICDT/CDT/include patch_bench.cpp \
//       $(pkg-config --cflags --libs opencv4) -o patch_bench
// Run:
//   ./patch_bench fingers.png OUTDIR [mode minArea maxArea eps]
//   mode: fh | ward | small   (default: run the whole matrix)
//
// Not production code: never merge into src/.

#include <CDT.h>
#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <numeric>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---------------------------------------------------------------- union-find
struct DSU {
    std::vector<int> p, sz;
    explicit DSU(int n) : p(n), sz(n, 1) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) {
        while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
        return x;
    }
    int unite(int a, int b) {  // returns root
        a = find(a); b = find(b);
        if (a == b) return a;
        if (sz[a] < sz[b] || (sz[a] == sz[b] && b < a)) std::swap(a, b);
        p[b] = a; sz[a] += sz[b];
        return a;
    }
};

// ------------------------------------------------------- Felzenszwalb-Huttenlocher
// 8-connected grid graph, Lab Euclidean weights, bucket sort (1/16 dE bins,
// ties by edge index, so deterministic). Returns a DSU over pixels.
struct GEdge { int a, b; float w; };

static void buildSortedEdges(const cv::Mat& lab, std::vector<GEdge>& out) {
    const int W = lab.cols, H = lab.rows;
    std::vector<GEdge> e;
    e.reserve(size_t(W) * H * 4);
    auto px = [&](int x, int y) { return lab.ptr<cv::Vec3f>(y)[x]; };
    auto dist = [](const cv::Vec3f& p, const cv::Vec3f& q) {
        cv::Vec3f d = p - q;
        return std::sqrt(d.dot(d));
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const cv::Vec3f p = px(x, y);
            int i = y * W + x;
            if (x + 1 < W) e.push_back({i, i + 1, dist(p, px(x + 1, y))});
            if (y + 1 < H) e.push_back({i, i + W, dist(p, px(x, y + 1))});
            if (x + 1 < W && y + 1 < H) e.push_back({i, i + W + 1, dist(p, px(x + 1, y + 1))});
            if (x > 0 && y + 1 < H) e.push_back({i, i + W - 1, dist(p, px(x - 1, y + 1))});
        }
    const int NB = 4096;
    std::vector<int> cnt(NB + 1, 0);
    auto bin = [&](float w) { return std::min(NB - 1, int(w * 16.f)); };
    for (auto& g : e) cnt[bin(g.w) + 1]++;
    for (int i = 0; i < NB; ++i) cnt[i + 1] += cnt[i];
    out.resize(e.size());
    for (auto& g : e) out[cnt[bin(g.w)]++] = g;
}

static DSU felzenszwalb(const std::vector<GEdge>& edges, int n, float k, int minSize) {
    DSU d(n);
    std::vector<float> thr(n, k);
    for (const auto& g : edges) {
        int a = d.find(g.a), b = d.find(g.b);
        if (a != b && g.w <= thr[a] && g.w <= thr[b]) {
            int r = d.unite(a, b);
            thr[r] = g.w + k / d.sz[r];
        }
    }
    if (minSize > 1)  // FH 2004 post-processing: merge small components along cheapest edges
        for (const auto& g : edges) {
            int a = d.find(g.a), b = d.find(g.b);
            if (a != b && (d.sz[a] < minSize || d.sz[b] < minSize)) d.unite(a, b);
        }
    return d;
}

// --------------------------------------------- region merging on the RAG
// mode WARD : global agglomeration by Ward cost n1 n2/(n1+n2) |mu1-mu2|^2,
//             stop as soon as no Patch is smaller than minArea.
// mode SMALL: only pairs with a Patch < minArea, by |mu1-mu2|^2 (each small
//             Patch goes to its most similar neighbour), until none is small.
enum class Merge { WARD, SMALL };

struct MergeStats { int initial = 0, merges = 0, finalCount = 0; double lastCost = 0, lastDE = 0; };

static std::vector<int> mergeRegions(const cv::Mat& lab, std::vector<int> label, int nInit, int W, int H,
                                     int minArea, Merge mode, MergeStats& st) {
    st.initial = nInit;
    std::vector<double> n(nInit, 0), sL(nInit, 0), sa(nInit, 0), sb(nInit, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int r = label[y * W + x];
            const cv::Vec3f p = lab.ptr<cv::Vec3f>(y)[x];
            n[r] += 1; sL[r] += p[0]; sa[r] += p[1]; sb[r] += p[2];
        }
    // adjacency (4-neighbour)
    std::vector<std::vector<int>> adj(nInit);
    {
        std::vector<uint64_t> pairs;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                int a = label[y * W + x];
                if (x + 1 < W) { int b = label[y * W + x + 1]; if (a != b) pairs.push_back(uint64_t(std::min(a, b)) << 32 | uint32_t(std::max(a, b))); }
                if (y + 1 < H) { int b = label[(y + 1) * W + x]; if (a != b) pairs.push_back(uint64_t(std::min(a, b)) << 32 | uint32_t(std::max(a, b))); }
            }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        for (auto pr : pairs) { int a = int(pr >> 32), b = int(pr & 0xffffffffu); adj[a].push_back(b); adj[b].push_back(a); }
    }
    DSU d(nInit);
    std::vector<int> ver(nInit, 0);
    int small = 0;
    for (int r = 0; r < nInit; ++r) small += n[r] < minArea;
    struct E { double c; int a, b, va, vb; bool operator>(const E& o) const { return c != o.c ? c > o.c : (a != o.a ? a > o.a : b > o.b); } };
    std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
    auto cost = [&](int a, int b) -> double {
        double dL = sL[a] / n[a] - sL[b] / n[b], da = sa[a] / n[a] - sa[b] / n[b], db = sb[a] / n[a] - sb[b] / n[b];
        double d2 = dL * dL + da * da + db * db;
        if (mode == Merge::WARD) return n[a] * n[b] / (n[a] + n[b]) * d2;
        return d2;
    };
    auto eligible = [&](int a, int b) { return mode == Merge::WARD || n[a] < minArea || n[b] < minArea; };
    for (int a = 0; a < nInit; ++a)
        for (int b : adj[a]) if (a < b && eligible(a, b)) pq.push({cost(a, b), a, b, 0, 0});
    std::vector<int> stamp(nInit, -1);
    while (small > 0 && !pq.empty()) {
        E e = pq.top(); pq.pop();
        if (d.find(e.a) != e.a || d.find(e.b) != e.b || ver[e.a] != e.va || ver[e.b] != e.vb) continue;
        if (!eligible(e.a, e.b)) continue;
        int a = e.a, b = e.b;
        small -= (n[a] < minArea) + (n[b] < minArea);
        double dE = std::sqrt(mode == Merge::WARD ? e.c * (n[a] + n[b]) / (n[a] * n[b]) : e.c);
        int r = d.unite(a, b), o = r == a ? b : a;
        n[r] += n[o]; sL[r] += sL[o]; sa[r] += sa[o]; sb[r] += sb[o];
        small += n[r] < minArea;
        ver[r]++;
        st.merges++; st.lastCost = e.c; st.lastDE = dE;
        std::vector<int> nb;
        for (int src : {r, o})
            for (int x : adj[src]) {
                int f = d.find(x);
                if (f != r && stamp[f] != st.merges) { stamp[f] = st.merges; nb.push_back(f); }
            }
        adj[r] = nb; adj[o].clear(); adj[o].shrink_to_fit();
        for (int x : nb) if (eligible(r, x)) {
            int lo = std::min(r, x), hi = std::max(r, x);
            pq.push({cost(lo, hi), lo, hi, ver[lo], ver[hi]});
        }
    }
    std::vector<int> remap(nInit, -1);
    int next = 0;
    for (auto& l : label) { int f = d.find(l); if (remap[f] < 0) remap[f] = next++; l = remap[f]; }
    st.finalCount = next;
    return label;
}

static std::vector<int> dsuLabels(DSU& d, int n, int& count) {
    std::vector<int> remap(n, -1), lab(n);
    count = 0;
    for (int i = 0; i < n; ++i) { int f = d.find(i); if (remap[f] < 0) remap[f] = count++; lab[i] = remap[f]; }
    return lab;
}

// --------------------------------------------- shared boundary chains
// Corners (cx,cy), 0..W x 0..H. A crack edge separates two pixels with
// different labels; the outside of the Frame is label -1, so the Frame border
// is made of crack edges too. Junctions: corners of degree >= 3, the 4 Frame
// corners. Chains run junction to junction; junction-free closed loops get
// their first corner in scan order as anchor.
struct Chain { std::vector<cv::Point> pts; bool closed = false; };

static std::vector<Chain> traceChains(const std::vector<int>& L, int W, int H) {
    auto lab = [&](int x, int y) { return (x < 0 || y < 0 || x >= W || y >= H) ? -1 : L[y * W + x]; };
    // edge existence + visited
    // horizontal edge (cx,cy)-(cx+1,cy): pixels (cx,cy-1) and (cx,cy)
    // vertical edge   (cx,cy)-(cx,cy+1): pixels (cx-1,cy) and (cx,cy)
    std::vector<uint8_t> hv((W) * (H + 1), 0), vv((W + 1) * H, 0);
    auto hasH = [&](int cx, int cy) { return cx >= 0 && cx < W && cy >= 0 && cy <= H && lab(cx, cy - 1) != lab(cx, cy); };
    auto hasV = [&](int cx, int cy) { return cy >= 0 && cy < H && cx >= 0 && cx <= W && lab(cx - 1, cy) != lab(cx, cy); };
    auto degree = [&](int cx, int cy) {
        return int(hasH(cx, cy)) + int(hasH(cx - 1, cy)) + int(hasV(cx, cy)) + int(hasV(cx, cy - 1));
    };
    std::vector<uint8_t> junc((W + 1) * (H + 1), 0);
    for (int cy = 0; cy <= H; ++cy)
        for (int cx = 0; cx <= W; ++cx) {
            bool corner = (cx == 0 || cx == W) && (cy == 0 || cy == H);
            junc[cy * (W + 1) + cx] = corner || degree(cx, cy) >= 3;
        }
    // directions: 0 right, 1 down, 2 left, 3 up
    auto edgeFlag = [&](int cx, int cy, int dir) -> uint8_t* {
        switch (dir) {
            case 0: return hasH(cx, cy) ? &hv[cy * W + cx] : nullptr;
            case 2: return hasH(cx - 1, cy) ? &hv[cy * W + cx - 1] : nullptr;
            case 1: return hasV(cx, cy) ? &vv[cy * (W + 1) + cx] : nullptr;
            default: return hasV(cx, cy - 1) ? &vv[(cy - 1) * (W + 1) + cx] : nullptr;
        }
    };
    const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
    std::vector<Chain> chains;
    auto walk = [&](int cx, int cy, int dir, bool closedStart) {
        Chain c;
        c.pts.push_back({cx, cy});
        int sx = cx, sy = cy;
        while (true) {
            *edgeFlag(cx, cy, dir) = 1;
            cx += DX[dir]; cy += DY[dir];
            c.pts.push_back({cx, cy});
            if (junc[cy * (W + 1) + cx] || (closedStart && cx == sx && cy == sy)) break;
            int back = (dir + 2) & 3, nd = -1;
            for (int k = 0; k < 4; ++k) if (k != back) { uint8_t* f = edgeFlag(cx, cy, k); if (f && !*f) { nd = k; break; } }
            if (nd < 0) break;  // cannot happen on a valid label map
            dir = nd;
        }
        c.closed = closedStart;
        chains.push_back(std::move(c));
    };
    for (int cy = 0; cy <= H; ++cy)
        for (int cx = 0; cx <= W; ++cx)
            if (junc[cy * (W + 1) + cx])
                for (int dir = 0; dir < 4; ++dir) { uint8_t* f = edgeFlag(cx, cy, dir); if (f && !*f) walk(cx, cy, dir, false); }
    for (int cy = 0; cy <= H; ++cy)
        for (int cx = 0; cx <= W; ++cx)
            for (int dir = 0; dir < 2; ++dir) { uint8_t* f = edgeFlag(cx, cy, dir); if (f && !*f) walk(cx, cy, dir, true); }
    return chains;
}

// --------------------------------------------- Douglas-Peucker with fixed ends
static void dpRec(const std::vector<cv::Point>& p, int i, int j, double eps2, std::vector<char>& keep) {
    if (j <= i + 1) return;
    double ax = p[i].x, ay = p[i].y, bx = p[j].x, by = p[j].y, dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
    double best = -1; int bi = -1;
    for (int k = i + 1; k < j; ++k) {
        double px = p[k].x - ax, py = p[k].y - ay, d2;
        if (L2 == 0) d2 = px * px + py * py;
        else { double c = px * dy - py * dx; d2 = c * c / L2; }
        if (d2 > best) { best = d2; bi = k; }
    }
    if (best > eps2) { keep[bi] = 1; dpRec(p, i, bi, eps2, keep); dpRec(p, bi, j, eps2, keep); }
}
static std::vector<cv::Point> simplify(const Chain& c, double eps) {
    const auto& p = c.pts;
    int n = int(p.size());
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    if (c.closed) {  // split at the point farthest from the anchor, keep a third point
        int far = 1; long best = -1;
        for (int k = 1; k < n - 1; ++k) { long d = long(p[k].x - p[0].x) * (p[k].x - p[0].x) + long(p[k].y - p[0].y) * (p[k].y - p[0].y); if (d > best) { best = d; far = k; } }
        keep[far] = 1;
        dpRec(p, 0, far, eps * eps, keep); dpRec(p, far, n - 1, eps * eps, keep);
        int kept = 0; for (char k : keep) kept += k;
        if (kept < 4) {  // need >= 3 distinct vertices: add the farthest from the anchor-far line
            int third = -1; double bd = -1;
            for (int k = 1; k < n - 1; ++k) if (!keep[k]) {
                double c2 = std::abs(double(p[k].x - p[0].x) * (p[far].y - p[0].y) - double(p[k].y - p[0].y) * (p[far].x - p[0].x));
                if (c2 > bd) { bd = c2; third = k; }
            }
            if (third >= 0) keep[third] = 1;
        }
    } else if (eps >= 0) {
        dpRec(p, 0, n - 1, eps * eps, keep);
    }
    std::vector<cv::Point> out;
    for (int k = 0; k < n; ++k) if (keep[k]) out.push_back(p[k]);
    return out;
}

// exact segment conflict test on integer coordinates: allowed contact is only a shared endpoint
static int orient(cv::Point a, cv::Point b, cv::Point c) {
    long v = long(b.x - a.x) * (c.y - a.y) - long(b.y - a.y) * (c.x - a.x);
    return (v > 0) - (v < 0);
}
static bool onSeg(cv::Point a, cv::Point b, cv::Point p) {
    return orient(a, b, p) == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y);
}
static bool conflict(cv::Point a, cv::Point b, cv::Point c, cv::Point d) {
    bool sa = a == c || a == d, sb = b == c || b == d;
    if (sa && sb) return true;  // duplicate segment (two chains collapsed onto each other)
    if (sa || sb) {             // one shared endpoint: conflict only if collinear overlap
        cv::Point s = sa ? a : b, p = sa ? b : a, q = (s == c) ? d : c;
        return orient(s, p, q) == 0 && ((p.x - s.x) * (q.x - s.x) + (p.y - s.y) * (q.y - s.y)) > 0;
    }
    int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 != o2 && o3 != o4 && o1 * o2 <= 0 && o3 * o4 <= 0 && !(o1 == 0 && o2 == 0)) return true;
    return onSeg(a, b, c) || onSeg(a, b, d) || onSeg(c, d, a) || onSeg(c, d, b);
}

struct SimplifyStats { int chains = 0, segments = 0, repairedChains = 0, rounds = 0; };

static std::vector<std::vector<cv::Point>> simplifyAll(const std::vector<Chain>& chains, double eps, int W, int H, SimplifyStats& st) {
    std::vector<double> ce(chains.size(), eps);
    std::vector<std::vector<cv::Point>> s(chains.size());
    for (size_t i = 0; i < chains.size(); ++i) s[i] = simplify(chains[i], eps);
    const int CS = 16, GW = W / CS + 1, GH = H / CS + 1;
    std::vector<char> dirty(chains.size(), 1);
    for (int round = 0; round < 12; ++round) {
        std::vector<std::vector<std::pair<int, int>>> grid(GW * GH);  // (chain, seg)
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t k = 0; k + 1 < s[i].size(); ++k) {
                cv::Point a = s[i][k], b = s[i][k + 1];
                int x0 = std::min(a.x, b.x) / CS, x1 = std::max(a.x, b.x) / CS, y0 = std::min(a.y, b.y) / CS, y1 = std::max(a.y, b.y) / CS;
                for (int gy = y0; gy <= y1; ++gy) for (int gx = x0; gx <= x1; ++gx) grid[gy * GW + gx].push_back({int(i), int(k)});
            }
        std::vector<char> bad(chains.size(), 0);
        int nbad = 0;
        for (auto& cell : grid)
            for (size_t u = 0; u < cell.size(); ++u)
                for (size_t v = u + 1; v < cell.size(); ++v) {
                    auto [ci, si] = cell[u]; auto [cj, sj] = cell[v];
                    if (ci == cj && std::abs(si - sj) <= 1) {
                        if (si == sj) continue;
                        // consecutive segments of one chain: only a fold-back is a conflict
                        int lo = std::min(si, sj);
                        cv::Point a = s[ci][lo], b = s[ci][lo + 1], c = s[ci][lo + 2];
                        if (orient(a, b, c) == 0 && (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) < 0) { if (!bad[ci]) { bad[ci] = 1; nbad++; } }
                        continue;
                    }
                    if (ci == cj && chains[ci].closed && ((si == 0 && sj == int(s[ci].size()) - 2) || (sj == 0 && si == int(s[ci].size()) - 2))) continue;
                    if (conflict(s[ci][si], s[ci][si + 1], s[cj][sj], s[cj][sj + 1])) {
                        if (!bad[ci]) { bad[ci] = 1; nbad++; }
                        if (!bad[cj]) { bad[cj] = 1; nbad++; }
                    }
                }
        st.rounds = round + 1;
        if (!nbad) break;
        for (size_t i = 0; i < s.size(); ++i) if (bad[i]) {
            if (ce[i] == eps) st.repairedChains++;
            ce[i] = ce[i] < 0.3 ? -1 : ce[i] * 0.5;  // -1: keep every corner (raw crack chain)
            s[i] = simplify(chains[i], ce[i]);
        }
    }
    st.chains = int(s.size());
    for (auto& c : s) st.segments += int(c.size()) - 1;
    return s;
}

// --------------------------------------------- triangulation + colouring
struct Tri { double x[3], y[3]; cv::Vec3b col; };
struct MeshStats { int verts = 0, tris = 0, steiner = 0; double area = 0; long uncovered = 0, multi = 0; double leak = 0; };

static std::vector<Tri> triangulate(const std::vector<std::vector<cv::Point>>& chains, int W, int H, double maxArea,
                                    const cv::Mat& rgb /* any 3-channel 8-bit, written back in the same order */, const std::vector<int>& label, MeshStats& st,
                                    double& tCdt, double& tColour, bool patchMean) {
    auto t0 = Clock::now();
    std::unordered_map<long, int> idx;
    std::vector<CDT::V2d<double>> V;
    std::vector<CDT::Edge> E;
    auto vid = [&](cv::Point p) {
        long key = long(p.y) * (W + 1) + p.x;
        auto it = idx.find(key);
        if (it != idx.end()) return it->second;
        int i = int(V.size());
        V.push_back(CDT::V2d<double>(p.x, p.y));
        idx.emplace(key, i);
        return i;
    };
    for (auto& c : chains)
        for (size_t k = 0; k + 1 < c.size(); ++k) {
            int a = vid(c[k]), b = vid(c[k + 1]);
            if (a != b) E.push_back(CDT::Edge(a, b));
        }
    // STEINER=ruppert : CDT::refineTriangles(LargestArea), Ruppert-style, also splits encroached constraint edges
    // STEINER=centroid (default): rebuild with the centroids of triangles above maxArea, until none is left;
    //                   constraint edges are never split, Steiner points are strictly inside Patches.
    static const bool ruppert = [] { const char* v = std::getenv("STEINER"); return v && std::string(v) == "ruppert"; }();
    const size_t inputVerts = V.size();
    CDT::Triangulation<double> cdt(CDT::VertexInsertionOrder::Auto, CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
    if (ruppert) {
        cdt.insertVertices(V);
        cdt.insertEdges(E);
        CDT::TriIndUSet er = cdt.collectSuperTriangle();
        cdt.refineTriangles(CDT::VertInd(20000), CDT::RefinementCriterion::LargestArea, maxArea, &er, 0.5);
        cdt.finalizeTriangulation(er);
    } else {
        for (int round = 0; ; ++round) {
            cdt = CDT::Triangulation<double>(CDT::VertexInsertionOrder::Auto, CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
            cdt.insertVertices(V);
            cdt.insertEdges(E);
            cdt.eraseSuperTriangle();
            size_t added = 0;
            for (const auto& t : cdt.triangles) {
                const auto &a = cdt.vertices[t.vertices[0]], &b = cdt.vertices[t.vertices[1]], &c = cdt.vertices[t.vertices[2]];
                double A = std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
                if (A > maxArea) {
                    // snap to the 1/8 px grid so predicates stay exact-ish and output is reproducible
                    double cx = std::round((a.x + b.x + c.x) / 3 * 8) / 8, cy = std::round((a.y + b.y + c.y) / 3 * 8) / 8;
                    V.push_back(CDT::V2d<double>(cx, cy)); added++;
                }
            }
            if (!added || round == 16) break;
        }
    }
    st.steiner = int(cdt.vertices.size() - inputVerts);
    auto t1 = Clock::now();

    std::vector<Tri> out;
    out.reserve(cdt.triangles.size());
    std::vector<uint8_t> cover(size_t(W) * H, 0);
    std::vector<double> sum;
    long leakPx = 0;
    // optional: Patch means
    int nP = 0; for (int l : label) nP = std::max(nP, l + 1);
    std::vector<cv::Vec3d> pm(patchMean ? nP : 0); std::vector<int> pn(patchMean ? nP : 0);
    if (patchMean) for (int i = 0; i < W * H; ++i) { pm[label[i]] += cv::Vec3d(rgb.ptr<cv::Vec3b>(i / W)[i % W]); pn[label[i]]++; }
    std::unordered_map<int, int> votes;
    for (const auto& t : cdt.triangles) {
        Tri tr;
        for (int k = 0; k < 3; ++k) { tr.x[k] = cdt.vertices[t.vertices[k]].x; tr.y[k] = cdt.vertices[t.vertices[k]].y; }
        double A2 = (tr.x[1] - tr.x[0]) * (tr.y[2] - tr.y[0]) - (tr.y[1] - tr.y[0]) * (tr.x[2] - tr.x[0]);
        if (A2 < 0) { std::swap(tr.x[1], tr.x[2]); std::swap(tr.y[1], tr.y[2]); A2 = -A2; }
        st.area += A2 / 2;
        int x0 = std::max(0, int(std::floor(std::min({tr.x[0], tr.x[1], tr.x[2]}) - 0.5)));
        int x1 = std::min(W - 1, int(std::ceil(std::max({tr.x[0], tr.x[1], tr.x[2]}))));
        int y0 = std::max(0, int(std::floor(std::min({tr.y[0], tr.y[1], tr.y[2]}) - 0.5)));
        int y1 = std::min(H - 1, int(std::ceil(std::max({tr.y[0], tr.y[1], tr.y[2]}))));
        cv::Vec3d acc(0, 0, 0); int cnt = 0;
        votes.clear();
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                double px = x + 0.5, py = y + 0.5;
                bool in = true;
                for (int k = 0; k < 3 && in; ++k) {  // y-down, positive area: inside = e >= 0 with top-left tie rule
                    double ax = tr.x[k], ay = tr.y[k], bx = tr.x[(k + 1) % 3], by = tr.y[(k + 1) % 3];
                    double e = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                    double dx = bx - ax, dy = by - ay;
                    bool topLeft = (dy == 0 && dx < 0) || dy > 0;
                    in = e < 0 ? false : (e > 0 || topLeft);
                }
                if (!in) continue;
                cover[y * W + x]++;
                acc += cv::Vec3d(rgb.ptr<cv::Vec3b>(y)[x]); cnt++;
                votes[label[y * W + x]]++;
            }
        int maj = 0, majL = -1;
        for (auto& [l, v] : votes) if (v > maj || (v == maj && l < majL)) { maj = v; majL = l; }
        leakPx += cnt - maj;
        if (patchMean && majL >= 0) acc = pm[majL] * (1.0 / pn[majL]), cnt = 1;
        if (cnt == 0) {  // sliver with no pixel centre: sample the centroid pixel
            int cx = std::clamp(int((tr.x[0] + tr.x[1] + tr.x[2]) / 3), 0, W - 1), cy = std::clamp(int((tr.y[0] + tr.y[1] + tr.y[2]) / 3), 0, H - 1);
            acc = cv::Vec3d(rgb.ptr<cv::Vec3b>(cy)[cx]); cnt = 1;
        }
        tr.col = cv::Vec3b(cv::saturate_cast<uchar>(acc[0] / cnt), cv::saturate_cast<uchar>(acc[1] / cnt), cv::saturate_cast<uchar>(acc[2] / cnt));
        out.push_back(tr);
    }
    for (auto c : cover) { st.uncovered += c == 0; st.multi += c > 1; }
    st.leak = double(leakPx) / (W * H);
    st.verts = int(cdt.vertices.size());
    st.tris = int(out.size());
    auto t2 = Clock::now();
    tCdt = ms(t0, t1); tColour = ms(t1, t2);
    return out;
}

static void render(const std::vector<Tri>& tris, int W, int H, int S, const std::string& path, bool wire,
                   const std::vector<std::vector<cv::Point>>* chains = nullptr) {
    cv::Mat img(H * S, W * S, CV_8UC3, cv::Scalar(255, 0, 255));
    const int SH = 8;
    for (auto& t : tris) {
        cv::Point p[3];
        for (int k = 0; k < 3; ++k) p[k] = cv::Point(int(std::lround(t.x[k] * S * (1 << SH))), int(std::lround(t.y[k] * S * (1 << SH))));
        cv::fillConvexPoly(img, p, 3, cv::Scalar(t.col[0], t.col[1], t.col[2]), cv::LINE_8, SH);
    }
    if (wire) {
        for (auto& t : tris) {
            cv::Point p[3];
            for (int k = 0; k < 3; ++k) p[k] = cv::Point(int(std::lround(t.x[k] * S * (1 << SH))), int(std::lround(t.y[k] * S * (1 << SH))));
            const cv::Point* pp = p; int n = 3;
            cv::polylines(img, &pp, &n, 1, true, cv::Scalar(40, 40, 40), 1, cv::LINE_AA, SH);
        }
        if (chains)
            for (auto& c : *chains)
                for (size_t k = 0; k + 1 < c.size(); ++k)
                    cv::line(img, c[k] * S, c[k + 1] * S, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    }
    cv::imwrite(path, img);
}

static void renderBoundaries(const cv::Mat& bgr, const std::vector<int>& L, int W, int H, const std::string& path) {
    cv::Mat big; cv::resize(bgr, big, {}, 3, 3, cv::INTER_NEAREST);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (x + 1 < W && L[y * W + x] != L[y * W + x + 1]) cv::line(big, {3 * x + 3, 3 * y}, {3 * x + 3, 3 * y + 3}, {0, 0, 255}, 1);
            if (y + 1 < H && L[y * W + x] != L[(y + 1) * W + x]) cv::line(big, {3 * x, 3 * y + 3}, {3 * x + 3, 3 * y + 3}, {0, 0, 255}, 1);
        }
    cv::imwrite(path, big);
}

// ------------------------------------------------------------------ driver
struct Cfg { std::string mode; int minArea; double maxArea; double eps; };

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s IMAGE OUTDIR [mode minArea maxArea eps]\n", argv[0]); return 2; }
    cv::Mat bgr = cv::imread(argv[1], cv::IMREAD_COLOR);
    if (bgr.empty()) return 1;
    std::string outDir = argv[2];
    const int W = bgr.cols, H = bgr.rows, N = W * H;
    cv::setNumThreads(1);

    std::vector<Cfg> cfgs;
    if (argc >= 7) cfgs.push_back({argv[3], std::atoi(argv[4]), std::atof(argv[5]), std::atof(argv[6])});
    else
        for (std::string m : {"fh", "ward", "small"})
            for (int mn : {64, 256}) cfgs.push_back({m, mn, 1024, 1.0});

    const int REPS = 15;
    const float BLUR = 0.8f;   // FH 2004 use sigma 0.8
    auto envf = [](const char* n, float d) { const char* v = std::getenv(n); return v ? float(std::atof(v)) : d; };
    const float KAPPA = envf("KAPPA", 2.0f);  // fh mode: k = KAPPA * minArea (dE of a min-sized Patch)
    const float K0 = envf("K0", 0.0f);        // ward/small init: FH with this k and no min size (0: identical-colour components)
    std::printf("KAPPA %.2f  K0 %.2f\n", KAPPA, K0);

    std::printf("image %s %dx%d, reps %d (median ms)\n", argv[1], W, H, REPS);
    std::printf("%-6s %5s %5s %4s | %6s %6s %6s %6s %6s %6s %6s %7s | %6s %6s %5s %6s %6s %5s %4s %7s %6s %4s %4s %6s\n",
                "mode", "min", "max", "eps", "lab", "graph", "merge", "trace", "simpl", "cdt", "colour", "TOTAL",
                "init", "Patch", "chain", "segs", "fixed", "verts", "stnr", "tris", "area", "unc", "mul", "leak%");
    for (const auto& c : cfgs) {
        std::vector<double> tLab, tGraph, tMerge, tTrace, tSimp, tCdt, tCol, tTot;
        MergeStats ms_; SimplifyStats ss; MeshStats mst; std::vector<int> labels; std::vector<Tri> tris;
        std::vector<std::vector<cv::Point>> simp;
        uint64_t hash0 = 0;
        for (int rep = 0; rep < REPS; ++rep) {
            auto a = Clock::now();
            cv::Mat f, lab;
            bgr.convertTo(f, CV_32F, 1.0 / 255);
            cv::cvtColor(f, lab, cv::COLOR_BGR2Lab);
            cv::GaussianBlur(lab, lab, cv::Size(0, 0), BLUR);
            auto b = Clock::now();
            std::vector<GEdge> edges;
            buildSortedEdges(lab, edges);
            int cnt = 0;
            std::vector<int> L;
            MergeStats st;
            if (c.mode == "fh") {
                DSU d = felzenszwalb(edges, N, KAPPA * c.minArea, c.minArea);
                L = dsuLabels(d, N, cnt);
                st.initial = st.finalCount = cnt;
            } else {
                DSU d = felzenszwalb(edges, N, K0, 1);
                L = dsuLabels(d, N, cnt);
            }
            auto b2 = Clock::now();
            if (c.mode != "fh") L = mergeRegions(lab, L, cnt, W, H, c.minArea, c.mode == "ward" ? Merge::WARD : Merge::SMALL, st);
            auto d0 = Clock::now();
            auto chains = traceChains(L, W, H);
            auto e0 = Clock::now();
            SimplifyStats s1;
            auto sc = simplifyAll(chains, c.eps, W, H, s1);
            auto f0 = Clock::now();
            MeshStats m1; double tc, tcol;
            auto tr = triangulate(sc, W, H, c.maxArea, bgr, L, m1, tc, tcol, false);
            auto g0 = Clock::now();
            tLab.push_back(ms(a, b)); tGraph.push_back(ms(b, b2)); tMerge.push_back(ms(b2, d0)); tTrace.push_back(ms(d0, e0));
            tSimp.push_back(ms(e0, f0)); tCdt.push_back(tc); tCol.push_back(tcol); tTot.push_back(ms(a, g0));
            uint64_t h = 1469598103934665603ull;
            for (auto& t : tr) for (int k = 0; k < 3; ++k) { h = (h ^ uint64_t(t.x[k] * 1024)) * 1099511628211ull; h = (h ^ uint64_t(t.y[k] * 1024)) * 1099511628211ull; }
            if (rep == 0) hash0 = h;
            else if (h != hash0) std::printf("  !! non-deterministic mesh on rep %d\n", rep);
            ms_ = st; ss = s1; mst = m1; labels = L; tris = tr; simp = sc;
        }
        std::printf("%-6s %5d %5.0f %4.1f | %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %7.2f | %6d %6d %5d %6d %6d %5d %4d %7d %6.0f %4ld %4ld %6.2f\n",
                    c.mode.c_str(), c.minArea, c.maxArea, c.eps, median(tLab), median(tGraph), median(tMerge), median(tTrace),
                    median(tSimp), median(tCdt), median(tCol), median(tTot), ms_.initial, ms_.finalCount,
                    ss.chains, ss.segments, ss.repairedChains, mst.verts, mst.steiner, mst.tris, mst.area, mst.uncovered, mst.multi, 100 * mst.leak);
        if (c.mode != "fh") std::printf("       merges %d, last merge: cost %.1f, dE %.2f; repair rounds %d\n", ms_.merges, ms_.lastCost, ms_.lastDE, ss.rounds);
        char base[256];
        std::snprintf(base, sizeof base, "%s/%s-min%d-max%.0f-eps%.1f", outDir.c_str(), c.mode.c_str(), c.minArea, c.maxArea, c.eps);
        render(tris, W, H, 3, std::string(base) + "-mosaic.png", false);
        render(tris, W, H, 3, std::string(base) + "-wire.png", true, &simp);
        renderBoundaries(bgr, labels, W, H, std::string(base) + "-patches.png");
        // Patch-mean colouring variant
        MeshStats m2; double a1, a2;
        auto trp = triangulate(simp, W, H, c.maxArea, bgr, labels, m2, a1, a2, true);
        render(trp, W, H, 3, std::string(base) + "-patchmean.png", false);
    }

    // reference timings: OpenCV ximgproc FH, SLICO, mean-shift (single thread)
    {
        std::vector<double> t1, t2, t3;
        cv::Mat lab8; cv::cvtColor(bgr, lab8, cv::COLOR_BGR2Lab);
        int nfh = 0, nsl = 0;
        cv::Mat seg, sl, msf;
        for (int rep = 0; rep < REPS; ++rep) {
            auto a = Clock::now();
            auto gs = cv::ximgproc::segmentation::createGraphSegmentation(0.8, 300, 64);
            gs->processImage(bgr, seg);
            auto b = Clock::now();
            auto s = cv::ximgproc::createSuperpixelSLIC(lab8, cv::ximgproc::SLICO, 8, 10.f);
            s->iterate(10);
            s->enforceLabelConnectivity(25);
            auto c = Clock::now();
            cv::pyrMeanShiftFiltering(bgr, msf, 8, 16, 0);
            auto d = Clock::now();
            t1.push_back(ms(a, b)); t2.push_back(ms(b, c)); t3.push_back(ms(c, d));
            double mx; cv::minMaxLoc(seg, nullptr, &mx); nfh = int(mx) + 1; nsl = s->getNumberOfSuperpixels();
            if (rep == 0) {
                cv::Mat l; s->getLabels(l);
                std::vector<int> L(l.begin<int>(), l.end<int>());
                renderBoundaries(bgr, L, W, H, outDir + "/ref-slico-64.png");
                std::vector<int> L2(seg.begin<int>(), seg.end<int>());
                renderBoundaries(bgr, L2, W, H, outDir + "/ref-ximgproc-fh-k300-min64.png");
            }
        }
        std::printf("\nreference: ximgproc FH(0.8,300,64) %.2f ms -> %d Patches; SLICO(8px,10 it) %.2f ms -> %d; pyrMeanShiftFiltering(8,16) %.2f ms\n",
                    median(t1), nfh, median(t2), nsl, median(t3));
    }
    return 0;
}
