package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Locale

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
    fun aLargeLandscapeWindowGetsACentredPortraitColumn() {
        // A tablet held landscape, 1280×800 dp: a 600 dp column, bands of 340 dp.
        val tablet = portraitColumn(1280f, 800f)
        assertEquals(600f, tablet.width, 0f)
        assertEquals(340f, tablet.x, 0f)
        assertEquals(1280f / 2, tablet.x + tablet.width / 2, 1e-3f)
        // An unfolded foldable, 841×701 dp: width = height · 3/4.
        val fold = portraitColumn(841f, 701f)
        assertEquals(701f * 3 / 4, fold.width, 1e-3f)
        assertEquals((841f - 701f * 3 / 4) / 2, fold.x, 1e-3f)
        // Exactly 600 dp wide counts.
        assertEquals(PortraitColumn(90f, 420f), portraitColumn(600f, 560f))
    }

    @Test
    fun portraitAndSmallWindowsKeepTheWholeWidth() {
        // The Pixel 8a in portrait, a tablet held portrait, a square window.
        assertEquals(PortraitColumn(0f, 412f), portraitColumn(412f, 915f))
        assertEquals(PortraitColumn(0f, 800f), portraitColumn(800f, 1280f))
        assertEquals(PortraitColumn(0f, 700f), portraitColumn(700f, 700f))
        // A landscape window under 600 dp wide.
        assertEquals(PortraitColumn(0f, 599f), portraitColumn(599f, 400f))
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
    fun zoomLabelsHaveOneItalianDecimalInItalian() {
        val it = Locale.ITALIAN
        assertEquals("0,5×", zoomLabel(0.5f, it))
        assertEquals("0,6×", zoomLabel(0.56f, it))
        assertEquals("1×", zoomLabel(1f, it))
        assertEquals("1×", zoomLabel(1.04f, it))
        assertEquals("1,4×", zoomLabel(1.4f, it))
        assertEquals("2×", zoomLabel(1.98f, it))
        assertEquals("10×", zoomLabel(10f, it))
    }

    @Test
    fun zoomLabelsHaveOneEnglishDecimalInEnglish() {
        val en = Locale.ENGLISH
        assertEquals("0.5×", zoomLabel(0.5f, en))
        assertEquals("0.6×", zoomLabel(0.56f, en))
        assertEquals("1×", zoomLabel(1f, en))
        assertEquals("1×", zoomLabel(1.04f, en))
        assertEquals("1.4×", zoomLabel(1.4f, en))
        assertEquals("2×", zoomLabel(1.98f, en))
        assertEquals("10×", zoomLabel(10f, en))
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

    @Test
    fun iconsTurnAgainstThePhone() {
        // Turned right (ROTATION_270): the icons turn anticlockwise.
        assertEquals(-90f, iconAngle(0f, 3), 0f)
        // Turned left (ROTATION_90): clockwise.
        assertEquals(90f, iconAngle(0f, 1), 0f)
        // Back upright, the short way.
        assertEquals(0f, iconAngle(-90f, 0), 0f)
        assertEquals(0f, iconAngle(90f, 0), 0f)
        // From turned right to turned left through upside down, not back through 0.
        assertEquals(-180f, iconAngle(-90f, 2), 0f)
        assertEquals(-270f, iconAngle(-180f, 1), 0f)
        // Already there: no turn, whatever the laps.
        assertEquals(-450f, iconAngle(-450f, 3), 0f)
    }

    @Test
    fun iconsTurnAgainstThePhoneRelativeToTheWindow() {
        // A tablet whose natural orientation is landscape, held that way: upright.
        assertEquals(0f, iconAngle(0f, 0, 0), 0f)
        // A window turned with the phone (display ROTATION_90, held at ROTATION_90): no turn.
        assertEquals(0f, iconAngle(0f, 1, 1), 0f)
        assertEquals(0f, iconAngle(0f, 3, 3), 0f)
        // The window turned left but the phone held upright: the icons turn anticlockwise.
        assertEquals(-90f, iconAngle(0f, 0, 1), 0f)
        // The window turned right, the phone turned left: upside down.
        assertEquals(180f, iconAngle(90f, 1, 3), 0f)
        // A display at ROTATION_0 behaves as before.
        assertEquals(iconAngle(0f, 3), iconAngle(0f, 3, 0), 0f)
    }

    @Test
    fun theSelectedStepIsCentred() {
        val widths = listOf(20f, 30f, 40f)
        // Item 0's centre (10) at 50.
        assertEquals(40f, selectorOffset(widths, 10f, 0, 100f), 0f)
        // Item 2 starts at 20 + 10 + 30 + 10 = 70, its centre at 90.
        assertEquals(-40f, selectorOffset(widths, 10f, 2, 100f), 0f)
        assertEquals(-40f, selectorOffset(widths, 10f, 9, 100f), 0f)
        assertEquals(0f, selectorOffset(emptyList(), 10f, 0, 100f), 0f)
    }

    @Test
    fun aSwipeLeftPicksLargerSteps() {
        assertEquals(3, swipedIndex(2, -60f, 50f, 8))
        assertEquals(1, swipedIndex(2, 40f, 50f, 8))
        assertEquals(2, swipedIndex(2, 20f, 50f, 8))
        assertEquals(7, swipedIndex(2, -1000f, 50f, 8))
        assertEquals(0, swipedIndex(2, 1000f, 50f, 8))
    }
}
