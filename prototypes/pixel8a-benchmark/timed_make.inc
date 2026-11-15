// Throwaway: appended inside `namespace adaptive` of a generated copy of
// src/mosaic/Adaptive.cpp (see CMakeLists.txt), so it can call the file's
// internal helpers. Same steps as make(), with blurredLab() inlined to time
// Lab and blur apart, a clock around each stage, and a hash of each output.

Mosaic makeTimed(const ofPixels& rgb, int minPatchArea, Trace& trace) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    auto lap = [&](Stage s) {
        auto t1 = clock::now();
        trace.ms[s] = std::chrono::duration<double, std::milli>(t1 - t0).count();
        t0 = t1;
    };
    auto hashMat = [](const cv::Mat& m) {
        Fnv f;
        for (int y = 0; y < m.rows; y++) f.bytes(m.ptr(y), m.cols * m.elemSize());
        return f.h;
    };
    auto hashLabels = [](const std::vector<int>& l) {
        Fnv f;
        f.bytes(l.data(), l.size() * sizeof(int));
        return f.h;
    };
    auto hashChains = [](const std::vector<Chain>& cs) {
        Fnv f;
        for (const Chain& c : cs) {
            f.bytes(c.points.data(), c.points.size() * sizeof(glm::ivec2));
            unsigned char closed = c.closed;
            f.bytes(&closed, 1);
        }
        return f.h;
    };

    const int w = rgb.getWidth(), h = rgb.getHeight();
    const Sizes sizes = sizesFor(w, h, minPatchArea);
    // blurredLab(), inlined.
    cv::Mat source = ofxCv::toCv(rgb), unit, lab;
    source.convertTo(unit, CV_32F, 1.0 / 255);
    cv::cvtColor(unit, lab, cv::COLOR_RGB2Lab);
    lap(ST_LAB);
    trace.unitHash = hashMat(unit);
    trace.hash[ST_LAB] = hashMat(lab);
    t0 = clock::now();
    cv::GaussianBlur(lab, lab, cv::Size(0, 0), sizes.blurSigma);
    lap(ST_BLUR);
    trace.hash[ST_BLUR] = hashMat(lab);
    t0 = clock::now();

    int count = 0;
    std::vector<int> label = felzenszwalb(lab, count);
    lap(ST_FH);
    trace.hash[ST_FH] = hashLabels(label);
    t0 = clock::now();
    count = joinEqualColours(rgb, label, count);
    lap(ST_JOIN);
    trace.hash[ST_JOIN] = hashLabels(label);
    t0 = clock::now();
    wardMerge(lab, label, count, sizes.minPatchArea, false);
    lap(ST_WARD);
    trace.hash[ST_WARD] = hashLabels(label);
    t0 = clock::now();
    count = splitIntoPieces(label, w, h);
    wardMerge(lab, label, count, sizes.minPatchArea, true);
    lap(ST_ABSORB);
    trace.hash[ST_ABSORB] = hashLabels(label);
    t0 = clock::now();

    std::vector<Chain> exact = traceChains(label, w, h);
    for (Chain& chain : exact) mergeCollinear(chain);
    lap(ST_CHAINS);
    trace.hash[ST_CHAINS] = hashChains(exact);
    t0 = clock::now();
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
    lap(ST_SIMPLIFY);
    trace.hash[ST_SIMPLIFY] = hashChains(chains);
    t0 = clock::now();

    Triangles triangles = triangulate(chains, w);
    lap(ST_CDT);
    {
        Fnv f;
        for (const auto& t : triangles) f.bytes(t.data(), sizeof t);
        trace.hash[ST_CDT] = f.h;
    }
    if (trace.probeCdt) { // diagnostics, untimed: which CDT step differs
        std::vector<CDT::V2d<double>> vertices;
        std::unordered_map<long, CDT::VertInd> index;
        for (const Chain& chain : chains)
            for (glm::ivec2 p : chain.points)
                if (index.try_emplace(long(p.y) * (w + 1) + p.x, CDT::VertInd(vertices.size())).second)
                    vertices.push_back(CDT::V2d<double>(p.x, p.y));
        CDT::Triangulation<double> cdt(CDT::VertexInsertionOrder::Auto, CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
        cdt.insertVertices(vertices);
        Fnv f; // before erasing the super-triangle, raw triangle records
        for (const CDT::Triangle& t : cdt.triangles) f.bytes(t.vertices.data(), sizeof t.vertices);
        trace.cdtVerticesOnly = f.h;
        std::vector<CDT::Edge> edges;
        for (const Chain& chain : chains)
            for (size_t i = 0; i + 1 < chain.points.size(); i++) {
                auto key = [&](glm::ivec2 p) { return index.at(long(p.y) * (w + 1) + p.x); };
                edges.push_back(CDT::Edge(key(chain.points[i]), key(chain.points[i + 1])));
            }
        cdt.insertEdges(edges);
        Fnv g; // after the constraints, before erasing the super-triangle
        for (const CDT::Triangle& t : cdt.triangles) g.bytes(t.vertices.data(), sizeof t.vertices);
        trace.cdtEdges = g.h;
        cdt.eraseSuperTriangle();
        Fnv e; // after erasing the super-triangle
        for (const CDT::Triangle& t : cdt.triangles) e.bytes(t.vertices.data(), sizeof t.vertices);
        trace.cdtErased = e.h;
    }
    t0 = clock::now();
    for (const auto& triangle : triangles) {
        Tessel tessel;
        for (int k = 0; k < 3; k++) tessel.triangle[k] = glm::vec3(triangle[k].x, triangle[k].y, 0);
        tessel.color = meanColour(rgb, triangle);
        mosaic.tessels.push_back(tessel);
    }
    lap(ST_COLOUR);
    {
        Fnv f;
        for (const Tessel& t : mosaic.tessels) f.bytes(&t.color, sizeof t.color);
        trace.hash[ST_COLOUR] = f.h;
    }
    return mosaic;
}
