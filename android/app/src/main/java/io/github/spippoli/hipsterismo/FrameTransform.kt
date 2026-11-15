package io.github.spippoli.hipsterismo

/**
 * How to read an upright Frame out of a camera buffer: pixel (x, y) of the
 * [width] × [height] Frame is the 4 bytes (R, G, B, A) at byte offset
 * `start + x * xStep + y * yStep`. Rotation, mirroring, the pixel stride and
 * the row stride all fold into these three numbers, so the native side only
 * walks them (`NativeMosaic.fromFrame`).
 *
 * Plain Kotlin, no Android classes, so it runs in the JVM tests.
 */
data class FrameTransform(
    val width: Int,
    val height: Int,
    val start: Int,
    val xStep: Int,
    val yStep: Int,
) {
    /** The byte offset of Frame pixel ([x], [y]). */
    fun offsetOf(x: Int, y: Int): Int = start + x * xStep + y * yStep

    /**
     * The smallest buffer, in bytes, that holds every pixel read: one past the
     * last byte of the farthest pixel. Offsets are affine in x and y, so the
     * extremes are at the Frame's corners.
     */
    val requiredBytes: Int
        get() = if (width == 0 || height == 0) 0 else corners().max() + BYTES_PER_PIXEL

    /** The lowest byte offset read; never negative for a transform from [of]. */
    val lowestOffset: Int
        get() = if (width == 0 || height == 0) 0 else corners().min()

    private fun corners() = intArrayOf(
        offsetOf(0, 0), offsetOf(width - 1, 0), offsetOf(0, height - 1), offsetOf(width - 1, height - 1),
    )

    companion object {
        const val BYTES_PER_PIXEL = 4

        /**
         * The transform that turns a [width] × [height] buffer, rows
         * [rowStride] bytes apart and pixels [pixelStride] bytes apart, into
         * an upright Frame: rotated clockwise by [rotationDegrees] (CameraX's
         * `ImageInfo.rotationDegrees`), then flipped left to right if
         * [mirror] (the front camera, so the preview behaves like a mirror).
         */
        fun of(
            width: Int,
            height: Int,
            rowStride: Int,
            pixelStride: Int = BYTES_PER_PIXEL,
            rotationDegrees: Int = 0,
            mirror: Boolean = false,
        ): FrameTransform {
            require(width >= 0 && height >= 0) { "negative size ${width}x$height" }
            require(pixelStride >= BYTES_PER_PIXEL) { "pixel stride $pixelStride < $BYTES_PER_PIXEL" }
            require(rowStride >= width * pixelStride) { "row stride $rowStride < $width × $pixelStride" }
            val ps = pixelStride
            val rs = rowStride
            // Each case maps an output pixel (x, y) back to its source pixel.
            val upright = when (rotationDegrees) {
                0 -> FrameTransform(width, height, 0, ps, rs)
                // Clockwise by 90: output (x, y) is source (y, H-1-x).
                90 -> FrameTransform(height, width, (height - 1).coerceAtLeast(0) * rs, -rs, ps)
                // Output (x, y) is source (W-1-x, H-1-y).
                180 -> FrameTransform(
                    width, height,
                    (height - 1).coerceAtLeast(0) * rs + (width - 1).coerceAtLeast(0) * ps, -ps, -rs,
                )
                // Clockwise by 270: output (x, y) is source (W-1-y, x).
                270 -> FrameTransform(height, width, (width - 1).coerceAtLeast(0) * ps, rs, -ps)
                else -> throw IllegalArgumentException("rotation $rotationDegrees is not a multiple of 90 in 0..270")
            }
            return if (mirror) upright.mirrored() else upright
        }
    }

    /** The same Frame flipped left to right. */
    fun mirrored(): FrameTransform =
        copy(start = start + (width - 1).coerceAtLeast(0) * xStep, xStep = -xStep)
}
