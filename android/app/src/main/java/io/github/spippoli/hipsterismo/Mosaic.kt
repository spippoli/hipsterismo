package io.github.spippoli.hipsterismo

/**
 * The Mosaic of one Frame, as [android.graphics.Canvas.drawVertices] takes it:
 * [vertices] holds x, y per vertex in Frame pixels, three vertices per Tessel,
 * and [colors] each vertex's ARGB colour (its Tessel's). [hash] is the core's
 * FNV-1a hash, the one the golden hashes of the tests use.
 */
class Mosaic(
    val width: Int,
    val height: Int,
    val vertices: FloatArray,
    val colors: IntArray,
    val hash: Long,
) {
    val tesselCount: Int get() = colors.size / 3
}
