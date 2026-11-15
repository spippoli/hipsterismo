package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class FrameTransformTest {
    // A 3 × 2 source whose pixels are labelled by their index:
    //   0 1 2
    //   3 4 5
    private val w = 3
    private val h = 2

    /** The source labels the transform reads, row by row, for a source with these strides. */
    private fun read(t: FrameTransform, rowStride: Int, pixelStride: Int): List<List<Int>> =
        List(t.height) { y ->
            List(t.width) { x ->
                val offset = t.offsetOf(x, y)
                assertEquals("offset $offset is not on a pixel", 0, offset % rowStride % pixelStride)
                (offset / rowStride) * w + (offset % rowStride) / pixelStride
            }
        }

    private fun check(rotation: Int, mirror: Boolean, expected: List<List<Int>>) {
        // Packed, padded rows, and a pixel stride wider than RGBA.
        for ((rowStride, pixelStride) in listOf(12 to 4, 20 to 4, 30 to 8)) {
            val t = FrameTransform.of(w, h, rowStride, pixelStride, rotation, mirror)
            assertEquals("rotation $rotation mirror $mirror stride $rowStride", expected, read(t, rowStride, pixelStride))
            // The buffer holds exactly the rows: the last row needs only its pixels.
            assertEquals((h - 1) * rowStride + (w - 1) * pixelStride + 4, t.requiredBytes)
            assertEquals(0, t.lowestOffset)
        }
    }

    @Test
    fun upright() = check(0, false, listOf(listOf(0, 1, 2), listOf(3, 4, 5)))

    @Test
    fun clockwise90() = check(90, false, listOf(listOf(3, 0), listOf(4, 1), listOf(5, 2)))

    @Test
    fun clockwise180() = check(180, false, listOf(listOf(5, 4, 3), listOf(2, 1, 0)))

    @Test
    fun clockwise270() = check(270, false, listOf(listOf(2, 5), listOf(1, 4), listOf(0, 3)))

    @Test
    fun mirroredFlipsTheUprightFrameLeftToRight() {
        check(0, true, listOf(listOf(2, 1, 0), listOf(5, 4, 3)))
        check(90, true, listOf(listOf(0, 3), listOf(1, 4), listOf(2, 5)))
        check(180, true, listOf(listOf(3, 4, 5), listOf(0, 1, 2)))
        check(270, true, listOf(listOf(5, 2), listOf(4, 1), listOf(3, 0)))
    }

    @Test
    fun quarterTurnsSwapTheSize() {
        val t = FrameTransform.of(320, 240, 1280 + 64, 4, 90)
        assertEquals(240, t.width)
        assertEquals(320, t.height)
        assertEquals(239 * (1280 + 64) + 319 * 4 + 4, t.requiredBytes)
    }

    @Test
    fun everySourcePixelIsReadOnce() {
        for (rotation in listOf(0, 90, 180, 270)) for (mirror in listOf(false, true)) {
            val t = FrameTransform.of(5, 4, 5 * 4 + 8, 4, rotation, mirror)
            val offsets = (0 until t.height).flatMap { y -> (0 until t.width).map { x -> t.offsetOf(x, y) } }
            val expected = (0 until 4).flatMap { y -> (0 until 5).map { x -> y * 28 + x * 4 } }
            assertEquals(expected.sorted(), offsets.sorted())
        }
    }

    @Test
    fun emptyFrameNeedsNoBytes() {
        assertEquals(0, FrameTransform.of(0, 0, 0).requiredBytes)
        assertEquals(0, FrameTransform.of(0, 5, 0, rotationDegrees = 90).requiredBytes)
    }

    @Test
    fun badArgumentsAreRejected() {
        assertThrows(IllegalArgumentException::class.java) { FrameTransform.of(4, 4, 15) }
        assertThrows(IllegalArgumentException::class.java) { FrameTransform.of(4, 4, 16, pixelStride = 3) }
        assertThrows(IllegalArgumentException::class.java) { FrameTransform.of(4, 4, 16, rotationDegrees = 45) }
        assertThrows(IllegalArgumentException::class.java) { FrameTransform.of(-1, 4, 16) }
    }
}
