package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import java.nio.ByteBuffer

/** The shared adaptive core (src/mosaic/core/), through JNI. */
object NativeMosaic {
    /** The default `min patch area`, in px² at 320×240, as on the desktop. */
    const val DEFAULT_MIN_PATCH_AREA = 32

    init {
        System.loadLibrary("hipsterismo")
    }

    /** The Mosaic of an ARGB_8888 [bitmap] (alpha ignored). */
    external fun fromBitmap(bitmap: Bitmap, minPatchArea: Int = DEFAULT_MIN_PATCH_AREA): Mosaic

    /**
     * The Mosaic of a direct [buffer] of RGBA pixels (alpha ignored), [height]
     * rows of [width] pixels, each row starting [rowStride] bytes after the
     * previous one.
     */
    external fun fromRgba(
        buffer: ByteBuffer,
        width: Int,
        height: Int,
        rowStride: Int,
        minPatchArea: Int = DEFAULT_MIN_PATCH_AREA,
    ): Mosaic
}
