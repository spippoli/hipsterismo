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
    fun zoomPresetsAreThoseTheCameraReaches() {
        // A back camera with an ultrawide, up to 8× digital.
        assertEquals(listOf(0.5f, 1f, 2f), zoomPresets(0.5f, 8f))
        // No ultrawide.
        assertEquals(listOf(1f, 2f), zoomPresets(1f, 8f))
        // A front camera that only reaches 1.8×.
        assertEquals(listOf(1f), zoomPresets(1f, 1.8f))
        // No zoom at all, or a range that misses 1× and 2×.
        assertEquals(listOf(1f), zoomPresets(1f, 1f))
        assertEquals(listOf(1f), zoomPresets(1.2f, 1.8f))
        // A ratio a hair under 1 is 1, not an ultrawide.
        assertEquals(listOf(1f, 2f), zoomPresets(0.995f, 4f))
    }

    @Test
    fun theLitChipIsTheLargestAtOrBelowTheZoom() {
        val presets = listOf(0.5f, 1f, 2f)
        assertEquals(0, activePreset(presets, 0.5f))
        assertEquals(0, activePreset(presets, 0.8f))
        assertEquals(1, activePreset(presets, 1f))
        assertEquals(1, activePreset(presets, 0.995f))
        assertEquals(1, activePreset(presets, 1.9f))
        assertEquals(2, activePreset(presets, 2f))
        assertEquals(2, activePreset(presets, 8f))
        assertEquals(0, activePreset(listOf(1f, 2f), 0.7f))
    }

    @Test
    fun zoomLabelsHaveOneItalianDecimal() {
        assertEquals("0,5×", zoomLabel(0.5f))
        assertEquals("0,6×", zoomLabel(0.56f))
        assertEquals("1×", zoomLabel(1f))
        assertEquals("1×", zoomLabel(1.04f))
        assertEquals("1,4×", zoomLabel(1.4f))
        assertEquals("2×", zoomLabel(1.98f))
        assertEquals("10×", zoomLabel(10f))
    }

    @Test
    fun aPinchScalesTheZoomWithinTheRange() {
        assertEquals(2f, pinchedZoom(1f, 2f, 0.5f, 8f), 1e-6f)
        assertEquals(0.75f, pinchedZoom(1.5f, 0.5f, 0.5f, 8f), 1e-6f)
        assertEquals(0.5f, pinchedZoom(0.6f, 0.5f, 0.5f, 8f), 0f)
        assertEquals(8f, pinchedZoom(6f, 2f, 0.5f, 8f), 0f)
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
