#include "Mosaic.h"
#include "Adaptive.h"

#include <set>

namespace {

// Fixed pseudo-random offset of a cell's vertex, in [-0.4, 0.4] cells on each
// axis: it hides the grid without moving from one Frame to the next.
glm::vec2 cellJitter(int cx, int cy) {
    uint32_t h = uint32_t(cx) * 73856093u ^ uint32_t(cy) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return {((h & 0xffff) / 65535.f - 0.5f) * 0.8f, (((h >> 16) & 0xffff) / 65535.f - 0.5f) * 0.8f};
}

// Mean colour of the 5x5 pixels around (x, y), clipped to the Frame.
ofColor blockMean(const ofPixels& rgb, int x, int y) {
    const int w = rgb.getWidth();
    const int h = rgb.getHeight();
    int r = 0, g = 0, b = 0, n = 0;
    for (int yy = std::max(0, y - 2); yy <= std::min(h - 1, y + 2); yy++) {
        for (int xx = std::max(0, x - 2); xx <= std::min(w - 1, x + 2); xx++) {
            ofColor c = rgb.getColor(xx, yy);
            r += c.r;
            g += c.g;
            b += c.b;
            n++;
        }
    }
    return ofColor(r / n, g / n, b / n);
}

} // namespace

MosaicMaker::MosaicMaker() {
    parameters.setName("mosaic");
    parameters.add(threshold1.set("first threshold", 0, 0, 400));
    parameters.add(threshold2.set("second threshold", 20, 0, 400));
    parameters.add(blurIterations.set("blur iteration", 9, 1, 31));
    parameters.add(vertexMode.set("vertex mode", VERTEX_MODE_ADAPTIVE, 0, VERTEX_MODE_COUNT - 1));
    parameters.add(cellSize.set("cell size", 5, 2, 40));
    parameters.add(persistence.set("persistence", 2, 0, 30));
    parameters.add(resolution.set("interpolation resolution", 1000, 2, 10000));
    parameters.add(minArea.set("min area", 2, 0, 10000));
    parameters.add(maxArea.set("max area", 400000, 0, 2000000));
    parameters.add(findHoles.set("find holes", true));
    // In px² at 320x240: adaptive::sizesFor() scales it with the Frame area.
    parameters.add(minPatchArea.set("min patch area", 32, 8, 1024));

    // Canny output is binary (0 or 255), so any threshold in between is equivalent.
    finder.setThreshold(128);
}

std::vector<std::shared_ptr<ofAbstractParameter>> MosaicMaker::parametersFor(int mode) {
    auto usedIn = [&](const ofAbstractParameter& parameter) {
        // Adaptive runs no Canny and no ContourFinder: only its own parameter.
        if (parameter.isReferenceTo(minPatchArea)) {
            return mode == VERTEX_MODE_ADAPTIVE;
        }
        if (mode == VERTEX_MODE_ADAPTIVE) {
            return false;
        }
        if (parameter.isReferenceTo(cellSize) || parameter.isReferenceTo(persistence)) {
            return mode == VERTEX_MODE_STABLE;
        }
        if (parameter.isReferenceTo(resolution)) {
            return mode == VERTEX_MODE_SPLINE;
        }
        return true;
    };
    std::vector<std::shared_ptr<ofAbstractParameter>> shown;
    for (auto& parameter : parameters) {
        if (parameter->isReferenceTo(vertexMode)) {
            shown.insert(shown.begin(), parameter);
        } else if (usedIn(*parameter)) {
            shown.push_back(parameter);
        }
    }
    return shown;
}

void MosaicMaker::reset() {
    cellAge.clear();
}

Mosaic MosaicMaker::make(const ofPixels& frame) {
    Mosaic mosaic;
    if (!frame.isAllocated() || frame.getWidth() == 0 || frame.getHeight() == 0) {
        return mosaic;
    }

    rgb = frame;
    if (rgb.getImageType() != OF_IMAGE_COLOR) {
        rgb.setImageType(OF_IMAGE_COLOR);
    }

    // The Mosaic spans the whole Frame rectangle, from (0,0) to (width,height).
    const int width = rgb.getWidth();
    const int height = rgb.getHeight();
    const int g = std::max(1, cellSize.get());
    const int mode = vertexMode;
    const bool stable = mode == VERTEX_MODE_STABLE;
    if (width != stateWidth || height != stateHeight || g != stateCellSize || mode != stateMode) {
        reset();
        stateWidth = width;
        stateHeight = height;
        stateCellSize = g;
        stateMode = mode;
    }

    if (mode == VERTEX_MODE_ADAPTIVE) {
        return adaptive::make(rgb, minPatchArea);
    }

    cv::Mat source = ofxCv::toCv(rgb);
    cv::cvtColor(source, gray, cv::COLOR_RGB2GRAY);
    for (int i = 1; i < blurIterations; i += 2) {
        cv::GaussianBlur(gray, gray, cv::Size(i, i), 1);
    }
    ofxCv::Canny(gray, edges, threshold1, threshold2, 3, false);
    finder.setMinArea(minArea);
    finder.setMaxArea(maxArea);
    finder.setFindHoles(findHoles);
    finder.findContours(edges);
    mosaic.contours = finder.getPolylines();

    if (stable) {
        // Cells the Contours pass through now are fresh; the others age and
        // are dropped once older than `persistence` Frames.
        std::set<Cell> touched;
        for (const ofPolyline& contour : mosaic.contours) {
            if (contour.size() == 0) continue;
            ofPolyline closed = contour;
            closed.addVertex(contour[0]);
            for (const auto& v : closed.getResampledBySpacing(1)) {
                touched.insert({int(v.x) / g, int(v.y) / g});
            }
        }
        for (auto it = cellAge.begin(); it != cellAge.end();) {
            if (!touched.count(it->first) && ++it->second > persistence) {
                it = cellAge.erase(it);
            } else {
                ++it;
            }
        }
        for (const Cell& cell : touched) {
            cellAge[cell] = 0;
        }
    }

    delaunay.reset();
    // Snap every point to a half-pixel grid and add each grid point once:
    // the triangulation needs distinct points, and spline mode's look
    // depends on this snapping. ofxDelaunay must carry
    // patches/ofxDelaunay-hull-triangles.patch, or it drops flat triangles
    // along the Frame border and overlaps triangles where grid points lie
    // on one circle (#39).
    std::set<std::pair<float, float>> added;
    auto addPoint = [&](float x, float y) {
        x = std::round(x * 2) / 2;
        y = std::round(y * 2) / 2;
        if (added.insert({x, y}).second) {
            delaunay.addPoint(ofPoint(x, y));
        }
    };

    // Points along the Frame's border, corners included, so the triangulation
    // covers the full rectangle and not just the area around the Contours.
    const int borderSegments = 8;
    for (int i = 0; i < borderSegments; i++) {
        float t = i / (float)borderSegments;
        addPoint(t * width, 0);
        addPoint(width, t * height);
        addPoint(width - t * width, height);
        addPoint(0, height - t * height);
    }

    if (stable) {
        for (const auto& [cell, age] : cellAge) {
            glm::vec2 p = (glm::vec2(cell.first, cell.second) + 0.5f + cellJitter(cell.first, cell.second)) * float(g);
            addPoint(ofClamp(p.x, 0, width), ofClamp(p.y, 0, height));
        }
    } else {
        // One spline through all Contour points, sampled uniformly. It
        // overshoots past the Frame where it jumps between Contours; keep
        // every sample inside so Tessels stay in the Frame.
        interpolator.clear();
        for (const auto& contour : finder.getContours()) {
            for (const auto& point : contour) {
                interpolator.push_back(ofxCv::toOf(point));
            }
        }
        if (interpolator.size() >= 3) {
            for (float step = 0.0; step < 1.0; step += 1 / (float)resolution) {
                glm::vec2 sample = interpolator.sampleAt(step);
                addPoint(ofClamp(sample.x, 0.f, float(width)), ofClamp(sample.y, 0.f, float(height)));
            }
        }
    }
    delaunay.triangulate();

    for (const ofMeshFace& face : delaunay.triangleMesh.getUniqueFaces()) {
        // Points lying on the border are collinear with the border points and
        // can give zero-area triangles: they draw nothing, skip them.
        glm::vec3 ab = face.getVertex(1) - face.getVertex(0);
        glm::vec3 ac = face.getVertex(2) - face.getVertex(0);
        if (std::abs(ab.x * ac.y - ab.y * ac.x) < 1e-3f) {
            continue;
        }
        glm::vec3 center = (face.getVertex(0) + face.getVertex(1) + face.getVertex(2)) / 3.f;
        int x = ofClamp(int(center.x), 0, width - 1);
        int y = ofClamp(int(center.y), 0, height - 1);
        mosaic.tessels.push_back({{face.getVertex(0), face.getVertex(1), face.getVertex(2)},
                                  stable ? blockMean(rgb, x, y) : rgb.getColor(x, y)});
    }
    return mosaic;
}

ofMesh toMesh(const Mosaic& mosaic) {
    ofMesh mesh;
    mesh.setMode(OF_PRIMITIVE_TRIANGLES);
    for (const Tessel& tessel : mosaic.tessels) {
        for (const glm::vec3& vertex : tessel.triangle) {
            mesh.addVertex(vertex);
            mesh.addColor(tessel.color);
        }
    }
    return mesh;
}
