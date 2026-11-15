#pragma once
// Throwaway: per-stage timings and hashes filled by makeTimed(), the
// instrumented copy of adaptive::make() (timed_make.inc).
#include <chrono>
#include <cstdint>
#include <cstring>

enum Stage { ST_LAB, ST_BLUR, ST_FH, ST_JOIN, ST_WARD, ST_ABSORB, ST_CHAINS, ST_SIMPLIFY, ST_CDT, ST_COLOUR, ST_COUNT };
static const char* const stageNames[ST_COUNT] = {"lab",    "blur",   "fh",       "join", "ward",
                                                 "absorb", "chains", "simplify", "cdt",  "colour"};

struct Trace {
    double ms[ST_COUNT] = {};
    uint64_t hash[ST_COUNT] = {}; // FNV-1a of the stage's output (hashing is not timed)
    uint64_t unitHash = 0; // the RGB floats after convertTo, before Lab
    bool probeCdt = false; // also triangulate the vertices alone (no constraints)
    uint64_t cdtVerticesOnly = 0, cdtEdges = 0, cdtErased = 0;
};

struct Fnv {
    uint64_t h = 14695981039346656037ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; i++) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
};
