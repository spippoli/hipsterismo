#pragma once

// Step 1 of the adaptive pipeline without OpenCV: the Lab conversion and the
// Gaussian blur. Plain C++, the same floats on every IEEE platform
// (compiled with -ffp-contract=off where the compiler would fuse a*b+c).

#include <array>
#include <cstdint>
#include <vector>

namespace mosaic::core {

// The Lab of an 8-bit sRGB colour, bit-identical to OpenCV's float
// conversion: cv::cvtColor of (r, g, b) / 255 (Mat::convertTo) with
// cv::COLOR_RGB2Lab. Like OpenCV, it interpolates a 33^3 table of integer
// Lab values (AdaptiveLabTable.inc) with integer weights.
std::array<float, 3> labOf(uint8_t r, uint8_t g, uint8_t b);

// labOf() of every pixel of an 8-bit RGB image (`stride` bytes per row)
// into `lab`, width * height * 3 floats, row by row.
void labFrame(const uint8_t* rgb, int width, int height, int stride, float* lab);

// The number of taps of the blur at `sigma`, as cv::GaussianBlur picks it
// for float images with Size(0, 0): cvRound(sigma * 8 + 1) | 1.
int gaussianTaps(double sigma);

// The normalised Gaussian kernel of `taps` taps (odd) at `sigma` > 0,
// bit-identical to cv::getGaussianKernel(taps, sigma, CV_32F): the same
// double arithmetic, including OpenCV's software exp.
std::vector<float> gaussianKernel(int taps, double sigma);

// Blurs a 3-channel float image in place (`values` points to width * height
// * 3 floats, row by row) with the separable kernel of gaussianKernel(), rows
// first, then columns. The border is reflected without repeating the edge
// (OpenCV's BORDER_REFLECT_101). Each output is the taps in order,
// accumulated from 0 with std::fma, so it is correctly rounded at every
// step and doesn't depend on the CPU or the vector width. It differs from
// cv::GaussianBlur in the last bits, whose order of accumulation depends on
// the CPU.
void gaussianBlur(float* values, int width, int height, double sigma);

} // namespace mosaic::core
