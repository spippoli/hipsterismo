// JNI glue between the Kotlin app and the shared adaptive core
// (src/mosaic/core/). A Frame comes in as a locked ARGB_8888 Bitmap or as a
// direct ByteBuffer of RGBA rows with a row stride; both are copied to packed
// RGB (alpha dropped) and turned into a Mosaic by makeAdaptive(). The Mosaic
// goes back as the arrays Canvas.drawVertices() takes: x, y per vertex, three
// vertices per Tessel, and each vertex's colour as ARGB (the Tessel's).

#include "AdaptiveCore.h"

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

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

// The Mosaic of `height` rows of RGBA pixels `stride` bytes apart.
jobject mosaicOfRgba(JNIEnv* env, const uint8_t* rgba, int width, int height, int stride, int minPatchArea) {
    std::vector<uint8_t> rgb(size_t(width) * height * 3);
    for (int y = 0; y < height; y++) {
        const uint8_t* in = rgba + size_t(y) * stride;
        uint8_t* out = &rgb[size_t(y) * width * 3];
        for (int x = 0; x < width; x++, in += 4, out += 3) {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
        }
    }
    installLogHook();
    const Mosaic mosaic = makeAdaptive(RgbImage{rgb.data(), width, height, width * 3}, minPatchArea);

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
    jfloatArray jvertices = env->NewFloatArray(vertexCount * 2);
    jintArray jcolors = env->NewIntArray(vertexCount);
    if (!jvertices || !jcolors) return nullptr; // OutOfMemoryError pending
    env->SetFloatArrayRegion(jvertices, 0, vertexCount * 2, vertices.data());
    env->SetIntArrayRegion(jcolors, 0, vertexCount, colors.data());

    jclass type = env->FindClass("io/github/spippoli/hipsterismo/Mosaic");
    if (!type) return nullptr;
    jmethodID constructor = env->GetMethodID(type, "<init>", "(II[F[IJ)V");
    if (!constructor) return nullptr;
    return env->NewObject(type, constructor, jint(width), jint(height), jvertices, jcolors, jlong(hash(mosaic)));
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
    jobject mosaic = mosaicOfRgba(env, static_cast<const uint8_t*>(pixels), int(info.width), int(info.height),
                                  int(info.stride), minPatchArea);
    AndroidBitmap_unlockPixels(env, bitmap);
    return mosaic;
}

extern "C" JNIEXPORT jobject JNICALL Java_io_github_spippoli_hipsterismo_NativeMosaic_fromRgba(
    JNIEnv* env, jobject, jobject buffer, jint width, jint height, jint rowStride, jint minPatchArea) {
    auto* data = static_cast<const uint8_t*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!data || capacity < 0) {
        throwIllegalArgument(env, "the ByteBuffer must be direct");
        return nullptr;
    }
    if (width < 0 || height < 0 || rowStride < 4 * jlong(width)) {
        throwIllegalArgument(env, "bad size or row stride");
        return nullptr;
    }
    if (height > 0 && jlong(rowStride) * (height - 1) + 4 * jlong(width) > capacity) {
        throwIllegalArgument(env, "the ByteBuffer is smaller than its rows");
        return nullptr;
    }
    return mosaicOfRgba(env, data, width, height, rowStride, minPatchArea);
}
