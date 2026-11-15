package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Picture
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * drawMosaic() through the hardware renderer (a Picture drawn into a
 * hardware Bitmap) paints every Tessel in its own flat colour.
 *
 * With the instrumentation argument `saveDrawn true` it also saves the
 * drawing as `mosaic-drawn.png` in the app's external files folder.
 */
@RunWith(AndroidJUnit4::class)
class MosaicDrawingTest {
    @Test
    fun drawnMosaicShowsTheTesselColours() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val options = BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val frame = context.assets.open("fingers.png").use { BitmapFactory.decodeStream(it, null, options) }!!
        val mosaic = NativeMosaic.fromBitmap(frame)

        val picture = Picture()
        picture.beginRecording(mosaic.width, mosaic.height)
            .drawMosaic(mosaic, Paint().apply { color = Color.WHITE })
        picture.endRecording()
        val hardware = Bitmap.createBitmap(picture)
        assertEquals(Bitmap.Config.HARDWARE, hardware.config)
        val drawn = hardware.copy(Bitmap.Config.ARGB_8888, false)

        // Every pixel has some Tessel's colour: nothing left black or blended.
        val tesselColours = mosaic.colors.toSet()
        val pixels = IntArray(drawn.width * drawn.height)
        drawn.getPixels(pixels, 0, drawn.width, 0, 0, drawn.width, drawn.height)
        val strays = pixels.count { it !in tesselColours }
        assertEquals("pixels without a Tessel's colour", 0, strays)

        // Each Tessel shows its own colour at its centroid (thin Tessels may
        // give that pixel to a neighbour).
        var matches = 0
        for (t in 0 until mosaic.tesselCount) {
            val v = mosaic.vertices
            val x = (v[6 * t] + v[6 * t + 2] + v[6 * t + 4]) / 3
            val y = (v[6 * t + 1] + v[6 * t + 3] + v[6 * t + 5]) / 3
            if (drawn.getPixel(x.toInt(), y.toInt()) == mosaic.colors[3 * t]) matches++
        }
        assertTrue("$matches of ${mosaic.tesselCount} centroids", matches >= mosaic.tesselCount * 9 / 10)

        if (InstrumentationRegistry.getArguments().getString("saveDrawn") == "true") {
            File(context.getExternalFilesDir(null), "mosaic-drawn.png").outputStream().use {
                drawn.compress(Bitmap.CompressFormat.PNG, 100, it)
            }
        }
    }
}
