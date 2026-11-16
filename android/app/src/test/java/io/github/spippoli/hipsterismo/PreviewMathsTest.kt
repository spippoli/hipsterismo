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
    fun theFrameFitsWholeAndCentred() {
        // A portrait 240×320 Frame on a 1080×2400 screen: bands above and below.
        val portrait = placement(240, 320, 1080f, 2400f)
        assertEquals(4.5f, portrait.scale, 0f)
        assertEquals(0f, portrait.dx, 0f)
        assertEquals(480f, portrait.dy, 0f)
        // A landscape 320×240 Frame on a 2400×1080 screen: bands left and right.
        val landscape = placement(320, 240, 2400f, 1080f)
        assertEquals(4.5f, landscape.scale, 0f)
        assertEquals(480f, landscape.dx, 0f)
        assertEquals(0f, landscape.dy, 0f)
        // Centred.
        assertEquals(1080f / 2, portrait.dx + 120 * portrait.scale, 1e-3f)
        assertEquals(2400f / 2, portrait.dy + 160 * portrait.scale, 1e-3f)
        assertEquals(2400f / 2, landscape.dx + 160 * landscape.scale, 1e-3f)
        assertEquals(1080f / 2, landscape.dy + 120 * landscape.scale, 1e-3f)
    }

    @Test
    fun emptyFramePlacementIsIdentity() = assertEquals(Placement(1f, 0f, 0f), placement(0, 0, 100f, 100f))

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
