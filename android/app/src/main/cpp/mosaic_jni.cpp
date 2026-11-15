// JNI glue between the Kotlin app and the shared adaptive core
// (src/mosaic/core/). A Frame comes in as a locked ARGB_8888 Bitmap or as a
// direct ByteBuffer of RGBA pixels read through a FrameTransform (a start
// offset and a byte step per x and per y, which carry the row stride, the
// rotation and the mirroring of a camera image). Either is copied to packed
// RGB (alpha dropped) and turned into a Mosaic by makeAdaptive(). The Mosaic
// goes back as the arrays Canvas draws: x, y per vertex, three vertices per
// Tessel, and each vertex's colour as ARGB (the Tessel's) for drawVertices();
// the Contours as x0, y0, x1, y1 segments for drawLines().

#include "AdaptiveCore.h"

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace mosaic::core;

void throwIllegalArgument(JNIEnv* env, const std::string& message) {
    jclass type = env->FindClass("java/lang/IllegalArgumentException");
    if (type) env->ThrowNew(type, message.c_str());
}

// The core's warnings go to logcat.
void installLogHook() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    setLogHook([](LogLevel level, const std::string& message) {
        __android_log_write(level == LogLevel::Error ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN, "Mosaic",
                            message.c_str());
    });
}

// The Mosaic of the width x height Frame whose pixel (x, y) is the RGBA at
// data + start + x * xStep + y * yStep. The caller has checked the bounds.
jobject mosaicOf(JNIEnv* env, const uint8_t* data, int width, int height, jlong start, jlong xStep, jlong yStep,
                 int minPatchArea) {
    std::vector<uint8_t> rgb(size_t(width) * height * 3);
    for (int y = 0; y < height; y++) {
        const uint8_t* in = data + start + y * yStep;
        uint8_t* out = &rgb[size_t(y) * width * 3];
        for (int x = 0; x < width; x++, in += xStep, out += 3) {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
        }
    }
    installLogHook();
    const auto begin = std::chrono::steady_clock::now();
    const Mosaic mosaic = makeAdaptive(RgbImage{rgb.data(), width, height, width * 3}, minPatchArea);
    const float coreMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - begin).count();

    const jsize vertexCount = jsize(mosaic.tessels.size() * 3);
    std::vector<jfloat> vertices;
    std::vector<jint> colors;
    vertices.reserve(size_t(vertexCount) * 2);
    colors.reserve(size_t(vertexCount));
    for (const Tessel& t : mosaic.tessels) {
        const jint argb = jint(0xff000000u | uint32_t(t.color.r) << 16 | uint32_t(t.color.g) << 8 | t.color.b);
        for (const Point& p : t.triangle) {
            vertices.push_back(p.x);
            vertices.push_back(p.y);
            colors.push_back(argb);
        }
    }
    std::vector<jfloat> segments;
    for (const Contour& c : mosaic.contours) {
        const size_t n = c.points.size();
        if (n < 2) continue;
        const size_t count = c.closed ? n : n - 1;
        for (size_t i = 0; i < count; i++) {
            const Point& a = c.points[i];
            const Point& b = c.points[(i + 1) % n];
            segments.insert(segments.end(), {a.x, a.y, b.x, b.y});
        }
    }

    jfloatArray jvertices = env->NewFloatArray(vertexCount * 2);
    jintArray jcolors = env->NewIntArray(vertexCount);
    jfloatArray jsegments = env->NewFloatArray(jsize(segments.size()));
    if (!jvertices || !jcolors || !jsegments) return nullptr; // OutOfMemoryError pending
    env->SetFloatArrayRegion(jvertices, 0, vertexCount * 2, vertices.data());
    env->SetIntArrayRegion(jcolors, 0, vertexCount, colors.data());
    env->SetFloatArrayRegion(jsegments, 0, jsize(segments.size()), segments.data());

    jclass type = env->FindClass("io/github/spippoli/hipsterismo/Mosaic");
    if (!type) return nullptr;
    jmethodID constructor = env->GetMethodID(type, "<init>", "(II[F[I[FJF)V");
    if (!constructor) return nullptr;
    return env->NewObject(type, constructor, jint(width), jint(height), jvertices, jcolors, jsegments,
                          jlong(hash(mosaic)), jfloat(coreMs));
}

} // namespace

extern "C" JNIEXPORT jobject JNICALL Java_io_github_spippoli_hipsterismo_NativeMosaic_fromBitmap(
    JNIEnv* env, jobject, jobject bitmap, jint minPatchArea) {
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        throwIllegalArgument(env, "not a Bitmap");
        return nullptr;
    }
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        throwIllegalArgument(env, "the Bitmap must be ARGB_8888");
        return nullptr;
    }
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS || !pixels) {
        throwIllegalArgument(env, "cannot lock the Bitmap's pixels");
        return nullptr;
    }
    jobject mosaic = mosaicOf(env, static_cast<const uint8_t*>(pixels), int(info.width), int(info.height), 0, 4,
                              jlong(info.stride), minPatchArea);
    AndroidBitmap_unlockPixels(env, bitmap);
    return mosaic;
}

extern "C" JNIEXPORT jobject JNICALL Java_io_github_spippoli_hipsterismo_NativeMosaic_fromPixels(
    JNIEnv* env, jobject, jobject buffer, jint width, jint height, jint start, jint xStep, jint yStep,
    jint minPatchArea) {
    auto* data = static_cast<const uint8_t*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!data || capacity < 0) {
        throwIllegalArgument(env, "the ByteBuffer must be direct");
        return nullptr;
    }
    if (width < 0 || height < 0) {
        throwIllegalArgument(env, "bad size");
        return nullptr;
    }
    if (width > 0 && height > 0) {
        // Offsets are affine in x and y, so the extremes are at the corners;
        // each pixel reads 4 bytes from its offset.
        const jlong x1 = jlong(width - 1) * xStep, y1 = jlong(height - 1) * yStep;
        const jlong lowest = start + std::min<jlong>(x1, 0) + std::min<jlong>(y1, 0);
        const jlong highest = start + std::max<jlong>(x1, 0) + std::max<jlong>(y1, 0);
        if (lowest < 0 || highest + 4 > capacity) {
            throwIllegalArgument(env, "the Frame's pixels are not all inside the ByteBuffer");
            return nullptr;
        }
    }
    return mosaicOf(env, data, width, height, start, xStep, yStep, minPatchArea);
}
