// Throwaway: how far are OpenCV's float RGB2Lab and GaussianBlur from plain C++?
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static cv::Mat lab(const cv::Mat& rgb8) {
    cv::Mat unit, out;
    rgb8.convertTo(unit, CV_32F, 1.0 / 255);
    cv::cvtColor(unit, out, cv::COLOR_RGB2Lab);
    return out;
}

// Textbook sRGB -> Lab (D65), in double.
static void naiveLab(int r8, int g8, int b8, float out[3]) {
    auto lin = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    double R = lin(r8 / 255.0), G = lin(g8 / 255.0), B = lin(b8 / 255.0);
    double X = (0.412453 * R + 0.357580 * G + 0.180423 * B) / 0.950456;
    double Y = 0.212671 * R + 0.715160 * G + 0.072169 * B;
    double Z = (0.019334 * R + 0.119193 * G + 0.950227 * B) / 1.088754;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116; };
    double L = Y > 0.008856 ? 116 * std::cbrt(Y) - 16 : 903.3 * Y;
    out[0] = float(L);
    out[1] = float(500 * (f(X) - f(Y)));
    out[2] = float(200 * (f(Y) - f(Z)));
}

// Separable float blur, REFLECT_101, kernel from cv::getGaussianKernel, plain loops.
static cv::Mat plainBlur(const cv::Mat& src, double sigma, bool fused) {
    int n = cvRound(sigma * 4 * 2 + 1) | 1;
    cv::Mat k = cv::getGaussianKernel(n, sigma, CV_32F);
    const float* kx = k.ptr<float>();
    int r = n / 2, w = src.cols, h = src.rows;
    auto reflect = [](int p, int len) {
        if (len == 1) return 0;
        while (p < 0 || p >= len) p = p < 0 ? -p : 2 * len - 2 - p;
        return p;
    };
    cv::Mat tmp(src.size(), src.type()), dst(src.size(), src.type());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) {
                float s = 0;
                for (int i = 0; i < n; i++) {
                    float v = src.at<cv::Vec3f>(y, reflect(x + i - r, w))[c];
                    s = fused ? std::fma(v, kx[i], s) : s + v * kx[i];
                }
                tmp.at<cv::Vec3f>(y, x)[c] = s;
            }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) {
                float s = 0;
                for (int i = 0; i < n; i++) {
                    float v = tmp.at<cv::Vec3f>(reflect(y + i - r, h), x)[c];
                    s = fused ? std::fma(v, kx[i], s) : s + v * kx[i];
                }
                dst.at<cv::Vec3f>(y, x)[c] = s;
            }
    return dst;
}

static long differing(const cv::Mat& a, const cv::Mat& b, float* maxDiff) {
    long d = 0;
    *maxDiff = 0;
    const float* p = a.ptr<float>();
    const float* q = b.ptr<float>();
    for (size_t i = 0; i < a.total() * a.channels(); i++) {
        if (std::memcmp(&p[i], &q[i], 4)) d++;
        *maxDiff = std::max(*maxDiff, std::abs(p[i] - q[i]));
    }
    return d;
}

int main(int argc, char** argv) {
    // 1. Lab of every 8-bit RGB colour: OpenCV vs the textbook formula.
    cv::Mat all(4096, 4096, CV_8UC3);
    for (int i = 0; i < 1 << 24; i++) all.at<cv::Vec3b>(i >> 12, i & 4095) = cv::Vec3b(i >> 16, (i >> 8) & 255, i & 255);
    cv::Mat cvLab = lab(all), naive(all.size(), CV_32FC3);
    for (int i = 0; i < 1 << 24; i++) naiveLab(i >> 16, (i >> 8) & 255, i & 255, &naive.at<cv::Vec3f>(i >> 12, i & 4095)[0]);
    float maxDiff;
    long d = differing(cvLab, naive, &maxDiff);
    std::printf("Lab, all 2^24 colours: %ld of %ld floats differ from textbook double, max |diff| %g\n", d,
                long(cvLab.total() * 3), maxDiff);
    cv::setUseOptimized(false);
    cv::Mat cvLabNoOpt = lab(all);
    cv::setUseOptimized(true);
    d = differing(cvLab, cvLabNoOpt, &maxDiff);
    std::printf("Lab, optimized vs setUseOptimized(false): %ld differ\n", d);
    // Lab as a pure function of the 8-bit triple: is every value k / 2^14 * (100 or 256) - offset?
    long offGrid = 0;
    for (int i = 0; i < 1 << 24; i += 97) {
        cv::Vec3f v = cvLab.at<cv::Vec3f>(i >> 12, i & 4095);
        double iL = v[0] / 100.0 * 16384, ia = (v[1] + 128) / 256.0 * 16384;
        if (iL != std::round(iL) || ia != std::round(ia)) offGrid++;
    }
    std::printf("Lab values off the 1/2^14 grid (sampled): %ld\n", offGrid);

    // 2. Blur of fingers.png's Lab at sigma 0.8 (320x240) and 3.2 (1280x960 equivalent).
    cv::Mat bgr = cv::imread(argv[1]), rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    cv::Mat l = lab(rgb);
    for (double sigma : {0.8, 3.2}) {
        cv::Mat ref;
        cv::GaussianBlur(l, ref, cv::Size(0, 0), sigma);
        cv::setUseOptimized(false);
        cv::Mat noopt;
        cv::GaussianBlur(l, noopt, cv::Size(0, 0), sigma);
        cv::setUseOptimized(true);
        long a = differing(ref, noopt, &maxDiff);
        std::printf("blur sigma %.1f: optimized vs setUseOptimized(false): %ld of %ld floats differ, max %g\n", sigma, a,
                    long(ref.total() * 3), maxDiff);
        for (bool fused : {false, true}) {
            long b = differing(ref, plainBlur(l, sigma, fused), &maxDiff);
            std::printf("blur sigma %.1f: OpenCV vs plain C++ (%s): %ld differ, max %g\n", sigma,
                        fused ? "fma" : "mul+add", b, maxDiff);
        }
    }
}
