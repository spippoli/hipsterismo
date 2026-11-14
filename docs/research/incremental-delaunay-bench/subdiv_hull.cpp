// Minimal check for research ticket #14: does cv::Subdiv2D keep the Frame
// border edges when a point lies close to the border?
// Build: g++ -std=c++20 -O2 subdiv_hull.cpp $(pkg-config --cflags --libs opencv4) -o subdiv_hull
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstdio>

static double area(const std::vector<cv::Vec6f>& t) {
    double s = 0;
    for (auto& v : t) s += std::fabs((v[2] - v[0]) * (v[5] - v[1]) - (v[3] - v[1]) * (v[4] - v[0])) / 2;
    return s;
}

int main() {
    const int W = 320, H = 240;
    for (int margin : {0, 1000, 100000}) {
        for (float y : {20.0f, 1.0f, 0.5f}) {
            cv::Subdiv2D sd(cv::Rect(-margin, -margin, W + 1 + 2 * margin, H + 1 + 2 * margin));
            sd.insert(cv::Point2f(0, 0));
            sd.insert(cv::Point2f((float)W, 0));
            sd.insert(cv::Point2f((float)W, (float)H));
            sd.insert(cv::Point2f(0, (float)H));
            sd.insert(cv::Point2f(160.f, y));
            std::vector<cv::Vec6f> t, in;
            sd.getTriangleList(t);
            for (auto& v : t) {
                bool ok = true;
                for (int k = 0; k < 6; k += 2)
                    if (v[k] < 0 || v[k] > W || v[k + 1] < 0 || v[k + 1] > H) ok = false;
                if (ok) in.push_back(v);
            }
            std::printf("margin %6d  point (160,%4.1f): %zu triangles inside the Frame, area %.1f of %d\n",
                        margin, y, in.size(), area(in), W * H);
        }
    }
}
