package io.github.spippoli.hipsterismo

/**
 * The Mosaic of one Frame, as [android.graphics.Canvas] draws it: [vertices]
 * holds x, y per vertex in Frame pixels, three vertices per Tessel, for
 * `drawVertices`, and [colors] each vertex's ARGB colour (its Tessel's).
 * [contours] holds the Contours as line segments for `drawLines`: x0, y0, x1,
 * y1 per segment, a closed Contour including its closing segment. [hash] is
 * the core's FNV-1a hash, the one the golden hashes of the tests use, and
 * [coreMs] the time `makeAdaptive()` took.
 */
class Mosaic(
    val width: Int,
    val height: Int,
    val vertices: FloatArray,
    val colors: IntArray,
    val contours: FloatArray,
    val hash: Long,
    val coreMs: Float,
) {
    val tesselCount: Int get() = colors.size / 3
}
