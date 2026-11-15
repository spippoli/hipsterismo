// Throwaway benchmark for #53: adaptive's speed and determinism, desktop vs
// Pixel 8a. Runs the UNMODIFIED adaptive::make() (as MosaicMaker::make()
// does in adaptive mode) for the end-to-end time and the Mosaic hash, and
// the instrumented copy adaptive_timed::makeTimed() for the stage times and
// stage hashes. Frames are binary PPM (P6) files, so no image library.
//
// bench [--reps N] [--warmup N] [--budget SEC] [--min-area A] [--threads T]
//       [--sustained SEC] [--dump DIR] FILE.ppm...
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "Adaptive.h"
#include "trace.h"

namespace adaptive_timed {
Mosaic makeTimed(const ofPixels& rgb, int minPatchArea, Trace& trace);
}

static uint64_t mosaicHash(const Mosaic& mosaic) { // as tests/src/main.cpp
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

// The same hash over the Tessels as a set: each triangle rotated to start
// at its smallest vertex (orientation kept), then sorted. Equal when two
// Mosaics have the same Tessels in a different order.
static uint64_t mosaicSetHash(const Mosaic& mosaic) {
    using Key = std::array<float, 9>;
    std::vector<Key> keys;
    for (const Tessel& t : mosaic.tessels) {
        int r = 0;
        for (int k = 1; k < 3; k++) {
            auto& a = t.triangle[k]; auto& b = t.triangle[r];
            if (a.y < b.y || (a.y == b.y && a.x < b.x)) r = k;
        }
        Key key;
        for (int k = 0; k < 3; k++) { key[2 * k] = t.triangle[(r + k) % 3].x; key[2 * k + 1] = t.triangle[(r + k) % 3].y; }
        key[6] = t.color.r; key[7] = t.color.g; key[8] = t.color.b;
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    Fnv f;
    f.bytes(keys.data(), keys.size() * sizeof(Key));
    return f.h;
}

static void dump(const Mosaic& mosaic, const std::string& path) {
    FILE* out = std::fopen(path.c_str(), "w");
    if (!out) return;
    for (const Tessel& t : mosaic.tessels) {
        for (const glm::vec3& v : t.triangle) std::fprintf(out, "%g %g ", v.x, v.y);
        std::fprintf(out, "%d %d %d\n", t.color.r, t.color.g, t.color.b);
    }
    std::fclose(out);
}

static bool readPpm(const char* path, std::vector<unsigned char>& data, int& w, int& h) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int maxval = 0;
    in >> magic;
    auto skip = [&] { // whitespace and comments
        while (true) {
            int c = in.peek();
            if (c == '#') { std::string line; std::getline(in, line); }
            else if (std::isspace(c)) in.get();
            else break;
        }
    };
    skip(); in >> w; skip(); in >> h; skip(); in >> maxval;
    in.get();
    if (!in || magic != "P6" || maxval != 255 || w <= 0 || h <= 0) return false;
    data.resize(size_t(w) * h * 3);
    in.read(reinterpret_cast<char*>(data.data()), data.size());
    return bool(in);
}

using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

static double pct(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    size_t i = std::min(v.size() - 1, size_t(p * (v.size() - 1) + 0.5));
    return v[i];
}

int main(int argc, char** argv) {
    int reps = 30, warmup = 3, minArea = 32, threads = -1;
    double budget = 60, sustained = 0;
    std::string dumpDir;
    std::vector<const char*> files;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? argv[++i] : (std::exit(2), argv[0]); };
        if (a == "--reps") reps = std::atoi(next());
        else if (a == "--warmup") warmup = std::atoi(next());
        else if (a == "--budget") budget = std::atof(next());
        else if (a == "--min-area") minArea = std::atoi(next());
        else if (a == "--threads") threads = std::atoi(next());
        else if (a == "--sustained") sustained = std::atof(next());
        else if (a == "--dump") dumpDir = next();
        else files.push_back(argv[i]);
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: bench [--reps N] [--warmup N] [--budget SEC] [--min-area A] [--threads T] [--sustained SEC] FILE.ppm...\n");
        return 2;
    }
    if (threads >= 0) cv::setNumThreads(threads);
    std::printf("# opencv %s, cv threads %d, min area %d\n", CV_VERSION, cv::getNumThreads(), minArea);

    for (const char* path : files) {
        std::vector<unsigned char> data;
        int w, h;
        if (!readPpm(path, data, w, h)) { std::fprintf(stderr, "cannot read %s\n", path); return 1; }
        ofPixels frame{data.data(), w, h};
        const char* name = std::strrchr(path, '/') ? std::strrchr(path, '/') + 1 : path;

        if (sustained > 0) { // one Frame over and over: does the time degrade?
            std::vector<std::pair<double, double>> runs; // (start s, ms)
            auto start = Clock::now();
            while (since(start) < sustained * 1000) {
                double at = since(start) / 1000;
                auto t = Clock::now();
                Mosaic m = adaptive::make(frame, minArea);
                runs.push_back({at, since(t)});
            }
            for (int s = 0; s < int(sustained + 0.999); s++) {
                std::vector<double> v;
                for (auto& r : runs) if (int(r.first) == s) v.push_back(r.second);
                if (v.empty()) continue;
                std::printf("SUSTAINED %s second=%d frames=%zu median=%.2f p90=%.2f max=%.2f\n", name, s, v.size(),
                            pct(v, 0.5), pct(v, 0.9), pct(v, 1));
            }
            continue;
        }

        uint64_t hash = 0, setHash = 0;
        size_t tessels = 0, contours = 0;
        bool stableHash = true;
        auto begin = Clock::now();
        for (int i = 0; i < warmup; i++) {
            Mosaic m = adaptive::make(frame, minArea);
            hash = mosaicHash(m);
            setHash = mosaicSetHash(m);
            if (!dumpDir.empty() && i == 0) dump(m, dumpDir + "/" + name + ".tessels");
            tessels = m.tessels.size();
            contours = m.contours.size();
        }
        std::vector<double> e2e;
        for (int i = 0; i < reps && (i < 3 || since(begin) < budget * 500); i++) {
            auto t = Clock::now();
            Mosaic m = adaptive::make(frame, minArea);
            e2e.push_back(since(t));
            uint64_t hh = mosaicHash(m);
            if (warmup == 0 && i == 0) { hash = hh; setHash = mosaicSetHash(m); tessels = m.tessels.size(); contours = m.contours.size(); }
            stableHash &= hh == hash;
        }
        std::vector<std::vector<double>> stage(ST_COUNT);
        std::vector<double> timedTotal;
        Trace trace;
        bool timedMatches = true;
        auto timedBegin = Clock::now();
        for (int i = 0; i < int(e2e.size()) && (i < 3 || since(timedBegin) < budget * 500); i++) {
            Trace t;
            t.probeCdt = i == 0;
            Mosaic m = adaptive_timed::makeTimed(frame, minArea, t);
            timedMatches &= mosaicHash(m) == hash;
            double total = 0;
            for (int s = 0; s < ST_COUNT; s++) { stage[s].push_back(t.ms[s]); total += t.ms[s]; }
            timedTotal.push_back(total);
            if (i == 0) trace = t;
        }
        std::printf("RESULT %s %dx%d tessels=%zu contours=%zu hash=0x%016llx set=0x%016llx reps=%zu median=%.2f p90=%.2f max=%.2f%s%s\n",
                    name, w, h, tessels, contours, (unsigned long long)hash, (unsigned long long)setHash, e2e.size(), pct(e2e, 0.5), pct(e2e, 0.9),
                    pct(e2e, 1), stableHash ? "" : " UNSTABLE-HASH", timedMatches ? "" : " TIMED-HASH-MISMATCH");
        std::printf("STAGES %s timed_sum=%.2f", name, pct(timedTotal, 0.5));
        for (int s = 0; s < ST_COUNT; s++) std::printf(" %s=%.2f", stageNames[s], pct(stage[s], 0.5));
        std::printf("\nHASHES %s unit=%016llx", name, (unsigned long long)trace.unitHash);
        for (int s = 0; s < ST_COUNT; s++) std::printf(" %s=%016llx", stageNames[s], (unsigned long long)trace.hash[s]);
        std::printf(" cdt_vertices_only=%016llx cdt_edges=%016llx cdt_erased=%016llx\n",
                    (unsigned long long)trace.cdtVerticesOnly, (unsigned long long)trace.cdtEdges,
                    (unsigned long long)trace.cdtErased);
        std::fflush(stdout);
    }
    // Peak resident memory of the whole process (run one Frame per process
    // for a per-size number).
    std::ifstream status("/proc/self/status");
    for (std::string line; std::getline(status, line);)
        if (line.rfind("VmHWM", 0) == 0) std::printf("MEMORY %s\n", line.c_str());
}
