package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Test

class MagnificationTest {
    // A portrait 1536×2048 Result in a 1080×1600 view: fit scale 0.703125,
    // 1080 wide and 1440 high, so bands of 80 above and below.
    private val w = 1536
    private val h = 2048
    private val vw = 1080f
    private val vh = 1600f

    private fun gesture(m: Magnification, px: Float, py: Float, factor: Float, panX: Float = 0f, panY: Float = 0f) =
        magnified(m, w, h, vw, vh, px, py, factor, panX, panY)

    /** Where the fit point (qx, qy) is drawn. */
    private fun drawn(m: Magnification, qx: Float, qy: Float) = Pair(qx * m.scale + m.tx, qy * m.scale + m.ty)

    @Test
    fun theScaleStaysBetweenFitAndEightTimes() {
        assertEquals(Magnification.FIT, gesture(Magnification.FIT, 540f, 800f, 0.5f))
        assertEquals(8f, gesture(Magnification.FIT, 540f, 800f, 20f).scale, 0f)
        val twice = gesture(Magnification.FIT, 540f, 800f, 2f)
        assertEquals(8f, gesture(twice, 540f, 800f, 5f).scale, 0f)
        assertEquals(1f, gesture(twice, 540f, 800f, 0.1f).scale, 0f)
    }

    @Test
    fun backToFitIsTheFitItself() {
        val m = gesture(gesture(Magnification.FIT, 100f, 300f, 4f), 900f, 1200f, 0.25f)
        assertEquals(1f, m.scale, 0f)
        assertEquals(0f, m.tx, 1e-3f)
        assertEquals(0f, m.ty, 1e-3f)
    }

    @Test
    fun aPinchKeepsThePointUnderTheFingers() {
        val m = gesture(Magnification.FIT, 300f, 700f, 2f)
        val (x, y) = drawn(m, 300f, 700f)
        assertEquals(300f, x, 1e-3f)
        assertEquals(700f, y, 1e-3f)
    }

    @Test
    fun theEdgesNeverComeAwayFromTheView() {
        val m = gesture(Magnification.FIT, 540f, 800f, 2f)
        // Mosaic 2160×2880 drawn: drag far right and down, then far left and up.
        val right = gesture(m, 0f, 0f, 1f, 5000f, 5000f)
        assertEquals(0f, drawn(right, 0f, 80f).first, 1e-3f)
        assertEquals(0f, drawn(right, 0f, 80f).second, 1e-3f)
        val left = gesture(m, 0f, 0f, 1f, -5000f, -5000f)
        assertEquals(vw, drawn(left, vw, 1520f).first, 1e-3f)
        assertEquals(vh, drawn(left, vw, 1520f).second, 1e-3f)
    }

    @Test
    fun anAxisSmallerThanTheViewStaysCentred() {
        // A landscape 2048×1536 Result in the same view: 1080 × 810, bands of 395.
        val m = magnified(Magnification.FIT, 2048, 1536, vw, vh, 540f, 800f, 1.5f, 300f, 300f)
        // 1620 × 1215 drawn: wider than the view, still shorter, so centred vertically.
        val top = 395f * m.scale + m.ty
        assertEquals((vh - 810f * m.scale) / 2, top, 1e-3f)
        assertEquals(0f, 0f * m.scale + m.tx, 1e-3f)
    }

    @Test
    fun aDoubleTapCentresTheTappedPointAtThreeTimes() {
        val m = doubleTapped(Magnification.FIT, w, h, vw, vh, 400f, 700f)
        assertEquals(3f, m.scale, 0f)
        val (x, y) = drawn(m, 400f, 700f)
        assertEquals(vw / 2, x, 1e-3f)
        assertEquals(vh / 2, y, 1e-3f)
    }

    @Test
    fun aDoubleTapNearAnEdgeStopsAtTheEdge() {
        val m = doubleTapped(Magnification.FIT, w, h, vw, vh, 10f, 210f)
        assertEquals(0f, drawn(m, 0f, 80f).first, 1e-3f)
        assertEquals(0f, drawn(m, 0f, 80f).second, 1e-3f)
    }

    @Test
    fun aDoubleTapWhileMagnifiedGoesBackToFit() {
        val m = gesture(Magnification.FIT, 540f, 800f, 1.2f)
        assertEquals(Magnification.FIT, doubleTapped(m, w, h, vw, vh, 100f, 100f))
    }

    @Test
    fun aSidewaysViewWorksTheSame() {
        // The view turned in landscape: 1600×1080 for the same portrait Mosaic.
        val m = magnified(Magnification.FIT, w, h, 1600f, 1080f, 800f, 540f, 8f, 0f, 0f)
        assertEquals(8f, m.scale, 0f)
        val fit = placement(w, h, 1600f, 1080f)
        val left = fit.dx * m.scale + m.tx
        assertEquals(800f, (800f - m.tx) / m.scale, 1e-3f) // the pivot stays put
        assert(left <= 0f)
    }

    @Test
    fun anEmptyMosaicStaysAtTheOrigin() {
        assertEquals(1f, magnified(Magnification.FIT, 0, 0, vw, vh, 1f, 1f, 1f, 50f, 50f).scale, 0f)
    }
}
