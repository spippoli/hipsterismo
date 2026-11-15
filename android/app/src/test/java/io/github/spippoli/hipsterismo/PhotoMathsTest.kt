package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class PhotoMathsTest {
    @Test
    fun aLargePhotoIsCappedOnItsLongSide() {
        assertEquals(PixelSize(2048, 1536), cappedSize(4000, 3000))
        assertEquals(PixelSize(1536, 2048), cappedSize(3000, 4000))
        assertEquals(PixelSize(2048, 2048), cappedSize(5000, 5000))
        // The Pixel 8a's 4:3 sensor size: 3472 · 2048 / 4624 = 1537.77.
        assertEquals(PixelSize(2048, 1538), cappedSize(4624, 3472))
        assertEquals(PixelSize(1538, 2048), cappedSize(3472, 4624))
        assertEquals(PixelSize(2048, 1152), cappedSize(4000, 2250))
    }

    @Test
    fun aPhotoAtOrBelowTheCapKeepsItsSize() {
        assertEquals(PixelSize(2048, 1536), cappedSize(2048, 1536))
        assertEquals(PixelSize(2048, 2048), cappedSize(2048, 2048))
        assertEquals(PixelSize(320, 240), cappedSize(320, 240))
        assertEquals(PixelSize(1, 1), cappedSize(1, 1))
        assertEquals(PixelSize(2047, 3), cappedSize(2047, 3))
    }

    @Test
    fun theShortSideIsRoundedAndNeverZero() {
        assertEquals(PixelSize(2048, 1), cappedSize(4096, 1))
        assertEquals(PixelSize(1, 2048), cappedSize(1, 10000))
        // 2049 · 2048 / 4097 = 1024.25 rounds down, 2051 · 2048 / 4097 = 1025.25 too, 3 · 2048 / 4 = 1536 exactly.
        assertEquals(PixelSize(2048, 1024), cappedSize(4097, 2049))
        assertEquals(PixelSize(1025, 2048), cappedSize(2051, 4097))
        assertEquals(PixelSize(2048, 1536), cappedSize(4096, 3072))
    }

    @Test
    fun anotherCap() {
        assertEquals(PixelSize(1024, 768), cappedSize(4000, 3000, cap = 1024))
    }

    @Test
    fun badSizesThrow() {
        assertThrows(IllegalArgumentException::class.java) { cappedSize(0, 100) }
        assertThrows(IllegalArgumentException::class.java) { cappedSize(100, -1) }
        assertThrows(IllegalArgumentException::class.java) { cappedSize(100, 100, cap = 0) }
    }

    @Test
    fun theTimingLine() {
        val timing = PhotoTiming(PhotoSource.CAMERA, 2048, 1536, 1000.0, 250.0, 100.0, 600.0, 2500, 32)
        assertEquals(60.0, timing.corePercent, 1e-9)
        assertEquals(
            "photo camera 2048x1536: 1000 ms to the result (capture 250, decode 100, core 600 = 60%, other 50), 2500 Tessels, min patch area 32",
            timing.line(),
        )
        assertEquals(0.0, timing.copy(totalMs = 0.0).corePercent, 0.0)
    }
}
