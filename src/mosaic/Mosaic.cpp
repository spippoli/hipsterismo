#include "Mosaic.h"

MosaicMaker::MosaicMaker() {
    parameters.setName("mosaic");
    parameters.add(threshold1.set("first threshold", 0, 0, 400));
    parameters.add(threshold2.set("second threshold", 20, 0, 400));
    parameters.add(blurIterations.set("blur iteration", 9, 1, 31));
    parameters.add(resolution.set("interpolation resolution", 1000, 2, 10000));

    finder.setMinArea(2);
    finder.setMaxArea(400000);
    finder.setThreshold(128);
    finder.setFindHoles(true);
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
    finder.findContours(edges);
    mosaic.contours = finder.getPolylines();

    interpolator.clear();
    for (const auto& contour : finder.getContours()) {
        for (const auto& point : contour) {
            interpolator.push_back(ofxCv::toOf(point));
        }
    }
    if (interpolator.size() < 3) {
        return mosaic;
    }

    // The spline overshoots past the Frame where it jumps between Contours;
    // keep every sample inside so Tessels (and their colours) stay in the Frame.
    const float maxX = rgb.getWidth() - 1;
    const float maxY = rgb.getHeight() - 1;
    delaunay.reset();
    for (float step = 0.0; step < 1.0; step += 1 / (float)resolution) {
        glm::vec2 sample = interpolator.sampleAt(step);
        delaunay.addPoint(ofPoint(ofClamp(sample.x, 0, maxX), ofClamp(sample.y, 0, maxY)));
    }
    delaunay.triangulate();

    for (const ofMeshFace& face : delaunay.triangleMesh.getUniqueFaces()) {
        glm::vec3 center = (face.getVertex(0) + face.getVertex(1) + face.getVertex(2)) / 3.f;
        mosaic.tessels.push_back({{face.getVertex(0), face.getVertex(1), face.getVertex(2)},
                                  rgb.getColor(int(center.x), int(center.y))});
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
