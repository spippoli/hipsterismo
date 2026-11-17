package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
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

    @Test
    fun theShutterFollowsHowThePhoneIsHeld() {
        // Upright, turned right (top of the phone to the right), upside down, turned left.
        assertEquals(0, captureRotationFor(0))
        assertEquals(3, captureRotationFor(90))
        assertEquals(2, captureRotationFor(180))
        assertEquals(1, captureRotationFor(270))
        // Each turn covers the 90° around it.
        assertEquals(0, captureRotationFor(44))
        assertEquals(3, captureRotationFor(45))
        assertEquals(3, captureRotationFor(134))
        assertEquals(2, captureRotationFor(135))
        assertEquals(2, captureRotationFor(224))
        assertEquals(1, captureRotationFor(225))
        assertEquals(1, captureRotationFor(314))
        assertEquals(0, captureRotationFor(315))
        assertEquals(0, captureRotationFor(359))
        // Lying flat: OrientationEventListener.ORIENTATION_UNKNOWN keeps the last rotation.
        assertEquals(null, captureRotationFor(-1))
    }

    private fun sizes(vararg s: String) = s.map { it.split("x").let { (w, h) -> PixelSize(w.toInt(), h.toInt()) } }

    @Test
    fun aHugeSensorIsCappedNear12Megapixels() {
        // A 200 MP sensor: its 12.5 MP binned 4:3 size, not the 200 or 112 MP ones.
        assertEquals(
            PixelSize(4080, 3060),
            captureSize(sizes("16320x12240", "12240x9180", "8160x6120", "4080x3060", "4000x3000", "4080x2296", "3264x2448", "1920x1080")),
        )
        // A 50 MP sensor: 8160×6144 is 50 MP, 4080×3072 is 12.5 MP.
        assertEquals(PixelSize(4080, 3072), captureSize(sizes("8160x6144", "4080x3072", "4000x3000", "2048x1536")))
        // 16 MP (4624×3472) is over the cap, 4032×3024 (12.2 MP) under it.
        assertEquals(PixelSize(4032, 3024), captureSize(sizes("4624x3472", "4032x3024", "4000x3000", "4624x2600", "1920x1440")))
    }

    @Test
    fun aSensorUnderTheCapKeepsItsLargest4by3Size() {
        assertEquals(PixelSize(4000, 3000), captureSize(sizes("4000x3000", "4000x2250", "3264x2448", "640x480")))
        assertEquals(PixelSize(3264, 2448), captureSize(sizes("1600x1200", "3264x2448", "3264x1836", "2592x1944")))
        // 13 MP (4160×3120) is still near 12 MP.
        assertEquals(PixelSize(4160, 3120), captureSize(sizes("4160x3120", "4160x2340")))
    }

    @Test
    fun theSizeIs4by3WhenTheListHasOne() {
        // A larger 16:9 or square size under the cap loses to a smaller 4:3 one.
        assertEquals(PixelSize(3264, 2448), captureSize(sizes("4000x2250", "3000x3000", "3264x2448", "1920x1080")))
        // Every 4:3 size is over the cap: the smallest of them, not a 16:9 one under the cap.
        assertEquals(PixelSize(8160, 6120), captureSize(sizes("16320x12240", "8160x6120", "4080x2296", "1920x1080")))
        // Without any 4:3 size, the largest under the cap of all of them, else the smallest.
        assertEquals(PixelSize(4000, 2250), captureSize(sizes("8000x4500", "4000x2250", "1920x1080")))
        assertEquals(PixelSize(8000, 4500), captureSize(sizes("16000x9000", "8000x4500")))
    }

    @Test
    fun theOrderAndOrientationOfTheListDoNotMatter() {
        assertEquals(PixelSize(4080, 3060), captureSize(sizes("1920x1080", "4080x3060", "16320x12240", "4000x3000")))
        assertEquals(PixelSize(3060, 4080), captureSize(sizes("3060x4080", "12240x16320", "2250x4000")))
    }

    @Test
    fun anEmptyListHasNoSize() {
        assertNull(captureSize(emptyList()))
    }

    @Test
    fun whatCountsAs4by3() {
        assertTrue(isFourByThree(PixelSize(4000, 3000)))
        assertTrue(isFourByThree(PixelSize(3000, 4000)))
        // Sensor sizes are a few pixels off: 4624×3472 is 1.3318, 4080×3072 is 1.328.
        assertTrue(isFourByThree(PixelSize(4624, 3472)))
        assertTrue(isFourByThree(PixelSize(4080, 3072)))
        assertFalse(isFourByThree(PixelSize(4000, 2250)))
        assertFalse(isFourByThree(PixelSize(3000, 3000)))
        assertFalse(isFourByThree(PixelSize(3000, 2000)))
    }

    @Test
    fun anotherCaptureCap() {
        assertEquals(PixelSize(2048, 1536), captureSize(sizes("4000x3000", "2048x1536", "640x480"), capPixels = 4_000_000))
    }
}
