#include "AdaptiveLab.h"

#include "AdaptiveParallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace mosaic::core {
namespace {

// ------------------------------------------------------------------- Lab
//
// OpenCV 4.10.0 modules/imgproc/src/color_lab.cpp, RGB2Lab_f with
// useInterpolation: each channel scaled to [0, 2^14], the cube of the
// 33^3 table that holds it, and integer trilinear weights in 1/16ths.

constexpr int labBaseShift = 14; // LAB_BASE = 2^14
constexpr int lutShift = 5; // 32 cubes per axis, LAB_LUT_DIM = 33
constexpr int lutDim = (1 << lutShift) + 1;
constexpr int trilinearShift = 8 - lutShift + 1; // weights in 1/16ths
constexpr int trilinearBase = 1 << trilinearShift;

const int16_t labTable[lutDim * lutDim * lutDim * 3] = {
#include "AdaptiveLabTable.inc"
};

// Each 8-bit value's position on the 2^14 scale: OpenCV's float v / 255
// (Mat::convertTo multiplies by the float 1/255), clipped to [0, 1], times
// 2^14 and rounded to nearest even (cvRound). From it, per axis: the table
// offset of the cube's low corner and of its high corner (the same at the
// last grid point), and the weights of the two corners, in 1/16ths.
struct LabScale {
    struct Axis {
        int low[256], high[256], lowWeight[256], highWeight[256];
    };
    Axis axis[3]; // r, g, b, with their table strides
    LabScale() {
        const float unit = float(1.0 / 255);
        const int stride[3] = {3, lutDim * 3, lutDim * lutDim * 3};
        for (int v = 0; v < 256; v++) {
            const float f = std::min(1.0f, std::max(0.0f, float(v) * unit));
            const int c = int(std::lrint(f * float(1 << labBaseShift)));
            const int cube = c >> (labBaseShift - lutShift);
            const int weight = (c >> (labBaseShift - 8 - 1)) & (trilinearBase - 1);
            for (int k = 0; k < 3; k++) {
                axis[k].low[v] = cube * stride[k];
                axis[k].high[v] = std::min(cube + 1, lutDim - 1) * stride[k];
                axis[k].lowWeight[v] = trilinearBase - weight;
                axis[k].highWeight[v] = weight;
            }
        }
    }
};

const LabScale labScale;

// ------------------------------------------------------------------ exp
//
// OpenCV 4.10.0 modules/core/src/softfloat.cpp, f64_exp: a table of 2^(i/64)
// and a degree-5 polynomial. softdouble's arithmetic is IEEE double, so with
// no fused operations plain doubles give the same bits.

double fromBits(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

constexpr int expTabScale = 6;
constexpr int expTabMask = (1 << expTabScale) - 1;

const uint64_t expTab[] = {
    0x3ff0000000000000ull, 0x3ff02c9a3e778061ull, 0x3ff059b0d3158574ull, 0x3ff0874518759bc8ull,
    0x3ff0b5586cf9890full, 0x3ff0e3ec32d3d1a2ull, 0x3ff11301d0125b51ull, 0x3ff1429aaea92de0ull,
    0x3ff172b83c7d517bull, 0x3ff1a35beb6fcb75ull, 0x3ff1d4873168b9aaull, 0x3ff2063b88628cd6ull,
    0x3ff2387a6e756238ull, 0x3ff26b4565e27cddull, 0x3ff29e9df51fdee1ull, 0x3ff2d285a6e4030bull,
    0x3ff306fe0a31b715ull, 0x3ff33c08b26416ffull, 0x3ff371a7373aa9cbull, 0x3ff3a7db34e59ff7ull,
    0x3ff3dea64c123422ull, 0x3ff4160a21f72e2aull, 0x3ff44e086061892dull, 0x3ff486a2b5c13cd0ull,
    0x3ff4bfdad5362a27ull, 0x3ff4f9b2769d2ca7ull, 0x3ff5342b569d4f82ull, 0x3ff56f4736b527daull,
    0x3ff5ab07dd485429ull, 0x3ff5e76f15ad2148ull, 0x3ff6247eb03a5585ull, 0x3ff6623882552225ull,
    0x3ff6a09e667f3bcdull, 0x3ff6dfb23c651a2full, 0x3ff71f75e8ec5f74ull, 0x3ff75feb564267c9ull,
    0x3ff7a11473eb0187ull, 0x3ff7e2f336cf4e62ull, 0x3ff82589994cce13ull, 0x3ff868d99b4492edull,
    0x3ff8ace5422aa0dbull, 0x3ff8f1ae99157736ull, 0x3ff93737b0cdc5e5ull, 0x3ff97d829fde4e50ull,
    0x3ff9c49182a3f090ull, 0x3ffa0c667b5de565ull, 0x3ffa5503b23e255dull, 0x3ffa9e6b5579fdbfull,
    0x3ffae89f995ad3adull, 0x3ffb33a2b84f15fbull, 0x3ffb7f76f2fb5e47ull, 0x3ffbcc1e904bc1d2ull,
    0x3ffc199bdd85529cull, 0x3ffc67f12e57d14bull, 0x3ffcb720dcef9069ull, 0x3ffd072d4a07897cull,
    0x3ffd5818dcfba487ull, 0x3ffda9e603db3285ull, 0x3ffdfc97337b9b5full, 0x3ffe502ee78b3ff6ull,
    0x3ffea4afa2a490daull, 0x3ffefa1bee615a27ull, 0x3fff50765b6e4540ull, 0x3fffa7c1819e90d8ull,
};

double softExp(double x) {
    if (std::isnan(x)) return x;
    if (std::isinf(x)) return x > 0 ? x : 0.0;
    const double polyA0 = fromBits(0x3f83ce0f3e46f431ull);
    const double a5 = 1.0 / polyA0;
    const double a4 = fromBits(0x3fe62e42fefa39f1ull) / polyA0;
    const double a3 = fromBits(0x3fcebfbdff82a45aull) / polyA0;
    const double a2 = fromBits(0x3fac6b08d81fec75ull) / polyA0;
    const double a1 = fromBits(0x3f83b2a72b4f3cd3ull) / polyA0;
    const double a0 = fromBits(0x3f55e7aa1566c2a4ull) / polyA0;
    const double prescale = fromBits(0x3ff71547652b82feull) * double(1 << expTabScale);
    const double postscale = 1.0 / double(1 << expTabScale);
    const double maxValue = double(3000 * (1 << expTabScale));

    uint64_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    double x0;
    if (int((bits >> 52) & 0x7ff) > 1023 + 10) {
        x0 = (bits >> 63) ? -maxValue : maxValue;
    } else {
        x0 = x * prescale;
    }
    const int val0 = int(std::nearbyint(x0)); // cvRound: nearest, ties to even
    int t = (val0 >> expTabScale) + 1023;
    t = t < 0 ? 0 : (t > 2047 ? 2047 : t);
    const double buf = fromBits(uint64_t(t) << 52);
    x0 = (x0 - std::nearbyint(x0)) * postscale;
    const double poly = ((((a0 * x0 + a1) * x0 + a2) * x0 + a3) * x0 + a4) * x0 + a5;
    return buf * polyA0 * fromBits(expTab[val0 & expTabMask]) * poly;
}

// -------------------------------------------------------------- the blur

// OpenCV's borderInterpolate for BORDER_REFLECT_101: gfedcb|abcdefgh|gfedcba.
int reflect101(int p, int length) {
    if (unsigned(p) < unsigned(length)) return p;
    if (length == 1) return 0;
    do {
        p = p < 0 ? -p : 2 * length - 2 - p;
    } while (unsigned(p) >= unsigned(length));
    return p;
}

// out[j] = the taps in order, s = fma(sources[i][j], kernel[i], s) from
// s = 0, for j < count. std::fma is correctly rounded, so every version
// below gives the same floats: a portable loop, and vector versions that
// keep 8 (AVX2) or 4 (NEON) sums in a register over all the taps. Each sum is
// a chain of dependent fused multiply-adds, so the vector versions run
// several blocks of outputs side by side (8 registers of sums), whose chains
// are independent: the order of each output's taps stays the same.
void convolvePlain(float* out, const float* const* sources, const float* kernel, int taps, int from, int count) {
    for (int j = from; j < count; j++) {
        float s = 0;
        for (int i = 0; i < taps; i++) s = std::fma(sources[i][j], kernel[i], s);
        out[j] = s;
    }
}

constexpr int convolveBlocks = 8; // registers of sums per pass

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("avx2,fma"))) void convolveAvx2(float* out, const float* const* sources, const float* kernel,
                                                        int taps, int count) {
    int j = 0;
    for (; j + 8 * convolveBlocks <= count; j += 8 * convolveBlocks) {
        __m256 s[convolveBlocks];
        for (int b = 0; b < convolveBlocks; b++) s[b] = _mm256_setzero_ps();
        for (int i = 0; i < taps; i++) {
            const __m256 k = _mm256_set1_ps(kernel[i]);
            const float* source = sources[i] + j;
            for (int b = 0; b < convolveBlocks; b++) s[b] = _mm256_fmadd_ps(_mm256_loadu_ps(source + 8 * b), k, s[b]);
        }
        for (int b = 0; b < convolveBlocks; b++) _mm256_storeu_ps(out + j + 8 * b, s[b]);
    }
    for (; j + 8 <= count; j += 8) {
        __m256 s = _mm256_setzero_ps();
        for (int i = 0; i < taps; i++) {
            s = _mm256_fmadd_ps(_mm256_loadu_ps(sources[i] + j), _mm256_set1_ps(kernel[i]), s);
        }
        _mm256_storeu_ps(out + j, s);
    }
    convolvePlain(out, sources, kernel, taps, j, count);
}

const bool hasAvx2Fma = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#endif

void convolve(float* out, const float* const* sources, const float* kernel, int taps, int count) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (hasAvx2Fma) {
        convolveAvx2(out, sources, kernel, taps, count);
        return;
    }
#elif defined(__aarch64__)
    int j = 0;
    for (; j + 4 * convolveBlocks <= count; j += 4 * convolveBlocks) {
        float32x4_t s[convolveBlocks];
        for (int b = 0; b < convolveBlocks; b++) s[b] = vdupq_n_f32(0);
        for (int i = 0; i < taps; i++) {
            const float k = kernel[i];
            const float* source = sources[i] + j;
            for (int b = 0; b < convolveBlocks; b++) s[b] = vfmaq_n_f32(s[b], vld1q_f32(source + 4 * b), k);
        }
        for (int b = 0; b < convolveBlocks; b++) vst1q_f32(out + j + 4 * b, s[b]);
    }
    for (; j + 4 <= count; j += 4) {
        float32x4_t s = vdupq_n_f32(0);
        for (int i = 0; i < taps; i++) s = vfmaq_n_f32(s, vld1q_f32(sources[i] + j), kernel[i]);
        vst1q_f32(out + j, s);
    }
    convolvePlain(out, sources, kernel, taps, j, count);
    return;
#endif
    convolvePlain(out, sources, kernel, taps, 0, count);
}

// The Lab of one colour, as labOf() and labFrame() give it.
inline void labInto(uint8_t r, uint8_t g, uint8_t b, float* out) {

    // The 8 corners of the cube, each weighted by the product of its three
    // axis weights; integer sums, so their order doesn't matter.
    const LabScale::Axis* axis = labScale.axis;
    const uint8_t v[3] = {r, g, b};
    int offset[3][2], weight[3][2];
    for (int k = 0; k < 3; k++) {
        offset[k][0] = axis[k].low[v[k]];
        offset[k][1] = axis[k].high[v[k]];
        weight[k][0] = axis[k].lowWeight[v[k]];
        weight[k][1] = axis[k].highWeight[v[k]];
    }
    int sum[3] = {0, 0, 0};
    for (int db = 0; db < 2; db++) {
        for (int dg = 0; dg < 2; dg++) {
            const int wbg = weight[2][db] * weight[1][dg];
            const int16_t* row = &labTable[offset[2][db] + offset[1][dg]];
            for (int dr = 0; dr < 2; dr++) {
                const int w = wbg * weight[0][dr];
                const int16_t* entry = row + offset[0][dr];
                sum[0] += entry[0] * w;
                sum[1] += entry[1] * w;
                sum[2] += entry[2] * w;
            }
        }
    }
    const int shift = trilinearShift * 3; // CV_DESCALE: round half up
    const int iL = (sum[0] + (1 << (shift - 1))) >> shift;
    const int ia = (sum[1] + (1 << (shift - 1))) >> shift;
    const int ib = (sum[2] + (1 << (shift - 1))) >> shift;
    const float L = iL * 1.0f / (1 << labBaseShift), a = ia * 1.0f / (1 << labBaseShift),
                bb = ib * 1.0f / (1 << labBaseShift);
    out[0] = L * 100.0f;
    out[1] = a * 256.0f - 128.0f;
    out[2] = bb * 256.0f - 128.0f;
}

} // namespace

std::array<float, 3> labOf(uint8_t r, uint8_t g, uint8_t b) {
    std::array<float, 3> lab;
    labInto(r, g, b, lab.data());
    return lab;
}

void labFrame(const uint8_t* rgb, int width, int height, int stride, float* lab) {
    parallelFor(height, 32, threadsFor(size_t(width) * height), [&](int from, int to, int) {
        for (int y = from; y < to; y++) {
            const uint8_t* p = rgb + size_t(y) * stride;
            float* out = lab + size_t(y) * width * 3;
            for (int x = 0; x < width; x++, p += 3, out += 3) labInto(p[0], p[1], p[2], out);
        }
    });
}

int gaussianTaps(double sigma) {
    return int(std::nearbyint(sigma * 4 * 2 + 1)) | 1;
}

std::vector<float> gaussianKernel(int taps, double sigma) {
    // OpenCV 4.10.0 modules/imgproc/src/smooth.dispatch.cpp,
    // getGaussianKernelBitExact, for sigma > 0.
    const double scale2 = fromBits(0xbfc0000000000000ull) / (sigma * sigma); // -0.125 / σ²
    const int half = (taps - 1) / 2;
    std::vector<double> values(half + 1);
    double sum = 0;
    for (int i = 0, x = 1 - taps; i < half; i++, x += 2) {
        const double t = softExp(double(x * x) * scale2);
        values[i] = t;
        sum += t;
    }
    sum *= 2.0;
    sum += 1.0;
    if ((taps & 1) == 0) sum += 1.0;
    const double mul = 1.0 / sum;
    std::vector<double> kernel(taps);
    for (int i = 0; i < half; i++) {
        const double t = values[i] * mul;
        kernel[i] = t;
        kernel[taps - 1 - i] = t;
    }
    kernel[half] = 1.0 * mul;
    if ((taps & 1) == 0) kernel[half + 1] = kernel[half];
    return std::vector<float>(kernel.begin(), kernel.end());
}

void gaussianBlur(float* values, int width, int height, double sigma) {
    const int taps = gaussianTaps(sigma), radius = taps / 2;
    if (width <= 0 || height <= 0 || taps <= 1) return;
    const std::vector<float> kernel = gaussianKernel(taps, sigma);
    const int rowFloats = width * 3;
    const int threads = threadsFor(size_t(width) * height);
    // Per thread: a padded row and the taps' source pointers.
    std::vector<std::vector<float>> padded(threads, std::vector<float>(size_t(width + 2 * radius) * 3));
    std::vector<std::vector<const float*>> sources(threads, std::vector<const float*>(taps));

    // Rows: each row padded with its reflection, tap i starting i pixels in.
    // Every row is written before the columns read it, so `rows` is left
    // uninitialised.
    std::unique_ptr<float[]> rows(new float[size_t(rowFloats) * height]);
    parallelFor(height, 16, threads, [&](int fromY, int toY, int slot) {
        float* pad = padded[slot].data();
        const float** source = sources[slot].data();
        for (int i = 0; i < taps; i++) source[i] = pad + size_t(i) * 3;
        for (int y = fromY; y < toY; y++) {
            const float* row = &values[size_t(y) * rowFloats];
            std::copy_n(row, rowFloats, pad + size_t(radius) * 3);
            for (int x = 1; x <= radius; x++) {
                std::copy_n(row + reflect101(-x, width) * 3, 3, pad + size_t(radius - x) * 3);
                std::copy_n(row + reflect101(width - 1 + x, width) * 3, 3, pad + size_t(radius + width - 1 + x) * 3);
            }
            convolve(&rows[size_t(y) * rowFloats], source, kernel.data(), taps, rowFloats);
        }
    });
    // Columns: tap i is the reflected row y + i - radius. In strips of 256
    // floats, so the taps' rows of a strip stay in the cache from one output
    // row to the next (43 taps at 2048 px would otherwise read 1 MB per row).
    const int strip = 256, strips = (rowFloats + strip - 1) / strip;
    parallelFor(strips, 1, threads, [&](int fromStrip, int toStrip, int slot) {
        const float** source = sources[slot].data();
        for (int from = fromStrip * strip; from < std::min(rowFloats, toStrip * strip); from += strip) {
            const int count = std::min(strip, rowFloats - from);
            for (int y = 0; y < height; y++) {
                for (int i = 0; i < taps; i++) {
                    source[i] = &rows[size_t(reflect101(y + i - radius, height)) * rowFloats + from];
                }
                convolve(&values[size_t(y) * rowFloats + from], source, kernel.data(), taps, count);
            }
        }
    });
}

} // namespace mosaic::core
