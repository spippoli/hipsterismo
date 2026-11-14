#include "Mosaic.h"

#include <set>
#include <utility>

MosaicMaker::MosaicMaker() {
    parameters.setName("mosaic");
    parameters.add(threshold1.set("first threshold", 0, 0, 400));
    parameters.add(threshold2.set("second threshold", 20, 0, 400));
    parameters.add(blurIterations.set("blur iteration", 9, 1, 31));
    parameters.add(resolution.set("interpolation resolution", 1000, 2, 10000));
    parameters.add(minArea.set("min area", 2, 0, 10000));
    parameters.add(maxArea.set("max area", 400000, 0, 2000000));
    parameters.add(findHoles.set("find holes", true));

    // Canny output is binary (0 or 255), so any threshold in between is equivalent.
    finder.setThreshold(128);
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

    interpolator.clear();
    for (const auto& contour : finder.getContours()) {
        for (const auto& point : contour) {
            interpolator.push_back(ofxCv::toOf(point));
        }
    }
    // The Mosaic spans the whole Frame rectangle, from (0,0) to (width,height).
    const float width = rgb.getWidth();
    const float height = rgb.getHeight();
    delaunay.reset();
    // ofxDelaunay's triangulation is not robust to nearly coincident points:
    // the spline bunches samples into tiny clusters at sharp Contour corners,
    // and those clusters give overlapping triangles. Snap every point to a
    // half-pixel grid and add each grid point once.
    std::set<std::pair<float, float>> added;
    auto addPoint = [&](float x, float y) {
        x = std::round(x * 2) / 2;
        y = std::round(y * 2) / 2;
        if (added.insert({x, y}).second) {
            delaunay.addPoint(ofPoint(x, y));
        }
    };

    // Points along the Frame's border, corners included, so the triangulation
    // covers the full rectangle and not just the hull of the Contours.
    const int borderSegments = 8;
    for (int i = 0; i < borderSegments; i++) {
        float t = i / (float)borderSegments;
        addPoint(t * width, 0);
        addPoint(width, t * height);
        addPoint(width - t * width, height);
        addPoint(0, height - t * height);
    }

    // The spline overshoots past the Frame where it jumps between Contours;
    // keep every sample inside so Tessels stay in the Frame.
    if (interpolator.size() >= 3) {
        for (float step = 0.0; step < 1.0; step += 1 / (float)resolution) {
            glm::vec2 sample = interpolator.sampleAt(step);
            addPoint(ofClamp(sample.x, 0, width), ofClamp(sample.y, 0, height));
        }
    }
    delaunay.triangulate();

    for (const ofMeshFace& face : delaunay.triangleMesh.getUniqueFaces()) {
        // Clamped samples lying on the border are collinear with the border
        // points and can give zero-area triangles: they draw nothing, skip them.
        glm::vec3 ab = face.getVertex(1) - face.getVertex(0);
        glm::vec3 ac = face.getVertex(2) - face.getVertex(0);
        if (std::abs(ab.x * ac.y - ab.y * ac.x) < 1e-3f) {
            continue;
        }
        glm::vec3 center = (face.getVertex(0) + face.getVertex(1) + face.getVertex(2)) / 3.f;
        int x = ofClamp(int(center.x), 0, rgb.getWidth() - 1);
        int y = ofClamp(int(center.y), 0, rgb.getHeight() - 1);
        mosaic.tessels.push_back({{face.getVertex(0), face.getVertex(1), face.getVertex(2)},
                                  rgb.getColor(x, y)});
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
