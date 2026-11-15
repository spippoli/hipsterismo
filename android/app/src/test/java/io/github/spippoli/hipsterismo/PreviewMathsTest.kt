package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PreviewMathsTest {
    @Test
    fun sliderStepsAreTheMocks() {
        assertEquals(listOf(8, 16, 32, 64, 128, 256, 512, 1024), MinPatchArea.steps)
        assertEquals(32, MinPatchArea.at(MinPatchArea.defaultIndex))
        assertEquals(NativeMosaic.DEFAULT_MIN_PATCH_AREA, MinPatchArea.DEFAULT)
        assertEquals(8, MinPatchArea.at(-3))
        assertEquals(1024, MinPatchArea.at(99))
    }

    @Test
    fun cropCoversTheViewAndFitStaysInside() {
        // A portrait 240×320 Frame on a 1080×2400 screen.
        val crop = placement(240, 320, 1080f, 2400f, crop = true)
        assertEquals(7.5f, crop.scale, 0f)
        assertEquals(-360f, crop.dx, 0f)
        assertEquals(0f, crop.dy, 0f)
        val fit = placement(240, 320, 1080f, 2400f, crop = false)
        assertEquals(4.5f, fit.scale, 0f)
        assertEquals(0f, fit.dx, 0f)
        assertEquals(480f, fit.dy, 0f)
        // The Frame is centred either way.
        for (p in listOf(crop, fit)) {
            assertEquals(1080f / 2, p.dx + 120 * p.scale, 1e-3f)
            assertEquals(2400f / 2, p.dy + 160 * p.scale, 1e-3f)
        }
    }

    @Test
    fun emptyFramePlacementIsIdentity() = assertEquals(Placement(1f, 0f, 0f), placement(0, 0, 100f, 100f, true))

    @Test
    fun overlayLineIsOnePixelAt320x240() {
        assertEquals(1f, overlayStrokeWidth(320, 240), 0f)
        assertEquals(1f, overlayStrokeWidth(240, 320), 0f)
        assertEquals(6.4f, overlayStrokeWidth(2048, 1536), 1e-5f)
    }

    @Test
    fun rollingWindowCountsTheLastSecond() {
        val window = RollingWindow()
        val ms = 1_000_000L
        assertTrue(window.mean(0).isNaN())
        // 30 events 33 ms apart, values 10..39.
        for (i in 0 until 30) window.add(i * 33 * ms, 10.0 + i)
        assertEquals(30.0, window.rate(29 * 33 * ms), 0.0)
        assertEquals(24.5, window.mean(29 * 33 * ms), 1e-9)
        // 505 ms later the first 15 have left (event 14 was at 462 ms).
        val later = 29 * 33 * ms + 505 * ms
        assertEquals(15.0, window.rate(later), 0.0)
        assertEquals(32.0, window.mean(later), 1e-9)
        assertEquals(0.0, window.rate(later + 2_000 * ms), 0.0)
        assertTrue(window.mean(later + 2_000 * ms).isNaN())
    }
}
