#!/usr/bin/env python3
"""Throwaway: copy src/mosaic/Adaptive.cpp to OUT with per-stage timing hooks.

usage: instrument.py SRC/Adaptive.cpp OUT/Adaptive.cpp
The hooks (Tick, g_t, g_count) live in shim/Mosaic.h. Only timers are added;
the algorithm is untouched, so the golden hash must still come out.
"""
import sys

src, out = sys.argv[1], sys.argv[2]
s = open(src).read()

def sub(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)

sub("""    source.convertTo(unit, CV_32F, 1.0 / 255);
    cv::cvtColor(unit, lab, cv::COLOR_RGB2Lab);
    cv::GaussianBlur(lab, lab, cv::Size(0, 0), sigma);""",
"""    Tick tk;
    source.convertTo(unit, CV_32F, 1.0 / 255);
    cv::cvtColor(unit, lab, cv::COLOR_RGB2Lab);
    tk.lap(S_LAB);
    cv::GaussianBlur(lab, lab, cv::Size(0, 0), sigma);
    tk.lap(S_BLUR);""")
sub("""    std::vector<PixelEdge> edges;
    edges.reserve""", """    Tick tk;
    std::vector<PixelEdge> edges;
    edges.reserve""")
sub("""    const int buckets = 4096;""", """    tk.lap(S_EDGES);
    const int buckets = 4096;""")
sub("""    for (const PixelEdge& e : edges) sorted[start[bucket(e.weight)]++] = e;
    return sorted;""", """    for (const PixelEdge& e : edges) sorted[start[bucket(e.weight)]++] = e;
    tk.lap(S_SORT);
    return sorted;""")
sub("""    for (const PixelEdge& e : sortedEdges(lab)) {""", """    auto sortedList = sortedEdges(lab);
    Tick tk;
    for (const PixelEdge& e : sortedList) {""")
sub("""    count = relabel(label, sets, n);
    return label;""", """    count = relabel(label, sets, n);
    tk.lap(S_UF);
    g_count[0] += count;
    return label;""")
sub("""        if (!any) break;""", """        g_count[5]++;
        if (!any) break;""")
sub("""    std::vector<int> label = felzenszwalb(lab, count);
    count = joinEqualColours(rgb, label, count);
    wardMerge(lab, label, count, sizes.minPatchArea, false);""",
"""    std::vector<int> label = felzenszwalb(lab, count);
    Tick tk;
    count = joinEqualColours(rgb, label, count);
    g_count[1] += count;
    tk.lap(S_JOIN);
    wardMerge(lab, label, count, sizes.minPatchArea, false);
    tk.lap(S_WARD1);""")
sub("""    count = splitIntoPieces(label, w, h);
    wardMerge(lab, label, count, sizes.minPatchArea, true);

    std::vector<Chain> exact = traceChains(label, w, h);
    for (Chain& chain : exact) mergeCollinear(chain);
    std::vector<Chain> chains = simplifyChains(exact, w, h, sizes.boundaryEpsilon);""",
"""    count = splitIntoPieces(label, w, h);
    g_count[2] += count;
    tk.lap(S_SPLIT);
    wardMerge(lab, label, count, sizes.minPatchArea, true);
    tk.lap(S_WARD2);

    std::vector<Chain> exact = traceChains(label, w, h);
    for (Chain& chain : exact) mergeCollinear(chain);
    g_count[3] += exact.size();
    tk.lap(S_TRACE);
    std::vector<Chain> chains = simplifyChains(exact, w, h, sizes.boundaryEpsilon);
    tk.lap(S_SIMPL);""")
sub("""    for (const auto& triangle : triangulate(chains, w)) {""", """    auto triangles = triangulate(chains, w);
    tk.lap(S_CDT);
    g_count[4] += triangles.size();
    for (const auto& triangle : triangles) {""")
sub("""        mosaic.tessels.push_back(tessel);
    }
    return mosaic;""", """        mosaic.tessels.push_back(tessel);
    }
    tk.lap(S_COLOUR);
    return mosaic;""")
open(out, "w").write(s)
