package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorSpace
import android.graphics.ImageDecoder
import android.graphics.Matrix
import android.graphics.Paint
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import kotlin.math.abs

/**
 * The photo path's decode ([PhotoDecoder]): a JPEG stored sideways or upside
 * down with an EXIF orientation comes out upright, a 4000×3000 photo is
 * capped to 2048×1536, a small one keeps its size, and the Bitmap is
 * ARGB_8888 in sRGB, what the JNI takes. The JPEGs are made here, from a
 * labelled image, so no photo is checked in.
 */
@RunWith(AndroidJUnit4::class)
class PhotoDecoderTest {
    // The upright image: 320×240, red top left, green top right, blue bottom
    // left, yellow bottom right.
    private val width = 320
    private val height = 240
    private val quadrants = listOf(
        Triple(0.25f, 0.25f, Color.RED),
        Triple(0.75f, 0.25f, Color.GREEN),
        Triple(0.25f, 0.75f, Color.BLUE),
        Triple(0.75f, 0.75f, Color.YELLOW),
    )

    private fun upright(): Bitmap {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val paint = Paint()
        for ((fx, fy, color) in quadrants) {
            paint.color = color
            val left = if (fx < 0.5f) 0f else width / 2f
            val top = if (fy < 0.5f) 0f else height / 2f
            canvas.drawRect(left, top, left + width / 2f, top + height / 2f, paint)
        }
        return bitmap
    }

    /**
     * A JPEG of [bitmap] turned by [storedTurn]° clockwise, tagged with the
     * EXIF [orientation] that turns it back: 6 for a stored turn of -90°, 3
     * for 180°, 8 for 90°.
     */
    private fun jpeg(bitmap: Bitmap, storedTurn: Float, orientation: Int): ByteArray {
        val stored = Bitmap.createBitmap(bitmap, 0, 0, bitmap.width, bitmap.height, Matrix().apply { postRotate(storedTurn) }, false)
        val out = ByteArrayOutputStream()
        stored.compress(Bitmap.CompressFormat.JPEG, 95, out)
        return withOrientation(out.toByteArray(), orientation)
    }

    /** [jpeg] with an APP1 Exif segment holding only the Orientation tag, right after SOI. */
    private fun withOrientation(jpeg: ByteArray, orientation: Int): ByteArray {
        check(jpeg[0] == 0xFF.toByte() && jpeg[1] == 0xD8.toByte())
        val tiff = byteArrayOf(
            'M'.code.toByte(), 'M'.code.toByte(), 0, 42, 0, 0, 0, 8, // big-endian, IFD0 at 8
            0, 1, // one entry
            0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, orientation.toByte(), 0, 0, // Orientation, SHORT, 1
            0, 0, 0, 0, // no next IFD
        )
        val payload = "Exif".toByteArray() + byteArrayOf(0, 0) + tiff
        val length = payload.size + 2
        val app1 = byteArrayOf(0xFF.toByte(), 0xE1.toByte(), (length shr 8).toByte(), length.toByte()) + payload
        return jpeg.copyOfRange(0, 2) + app1 + jpeg.copyOfRange(2, jpeg.size)
    }

    private fun decode(bytes: ByteArray) = PhotoDecoder.decode(ImageDecoder.createSource(ByteBuffer.wrap(bytes)))

    private fun assertNear(expected: Int, actual: Int, what: String) {
        val distance = abs(Color.red(expected) - Color.red(actual)) +
            abs(Color.green(expected) - Color.green(actual)) +
            abs(Color.blue(expected) - Color.blue(actual))
        assertTrue("$what: expected %08x, got %08x".format(expected, actual), distance < 40)
    }

    private fun assertUpright(bitmap: Bitmap, what: String) {
        assertEquals("$what width", width, bitmap.width)
        assertEquals("$what height", height, bitmap.height)
        for ((fx, fy, color) in quadrants) {
            assertNear(color, bitmap.getPixel((fx * width).toInt(), (fy * height).toInt()), "$what at ($fx, $fy)")
        }
    }

    @Test
    fun anExifRotatedJpegComesOutUpright() {
        val image = upright()
        for ((turn, orientation) in listOf(0f to 1, -90f to 6, 180f to 3, 90f to 8)) {
            val bitmap = decode(jpeg(image, turn, orientation))
            assertUpright(bitmap, "stored turned $turn°, EXIF orientation $orientation")
        }
    }

    @Test
    fun aMirroredExifJpegComesOutUpright() {
        // The front camera's photos are saved mirrored like the preview,
        // which CameraX writes as a mirrored EXIF orientation. Each stored
        // transform here is its own inverse.
        val image = upright()
        val stored = listOf(
            2 to Matrix().apply { postScale(-1f, 1f) }, // flipped left to right
            4 to Matrix().apply { postScale(1f, -1f) }, // flipped top to bottom
            5 to Matrix().apply { postRotate(90f); postScale(-1f, 1f) }, // transposed
            7 to Matrix().apply { postRotate(90f); postScale(1f, -1f) }, // transversed
        )
        for ((orientation, matrix) in stored) {
            val turned = Bitmap.createBitmap(image, 0, 0, image.width, image.height, matrix, false)
            val out = ByteArrayOutputStream()
            turned.compress(Bitmap.CompressFormat.JPEG, 95, out)
            assertUpright(decode(withOrientation(out.toByteArray(), orientation)), "EXIF orientation $orientation")
        }
    }

    @Test
    fun theDecodedBitmapIsWhatTheCoreTakes() {
        val bitmap = decode(jpeg(upright(), -90f, 6))
        assertEquals(Bitmap.Config.ARGB_8888, bitmap.config)
        assertEquals(ColorSpace.get(ColorSpace.Named.SRGB), bitmap.colorSpace)
        // The JNI locks it, so it must be a software Bitmap.
        val mosaic = NativeMosaic.fromBitmap(bitmap)
        assertEquals(width, mosaic.width)
        assertEquals(height, mosaic.height)
        assertTrue(mosaic.tesselCount > 0)
    }

    private fun largeJpeg(w: Int, h: Int): ByteArray {
        val big = Bitmap.createScaledBitmap(upright(), w, h, true)
        val out = ByteArrayOutputStream()
        big.compress(Bitmap.CompressFormat.JPEG, 90, out)
        big.recycle()
        return out.toByteArray()
    }

    @Test
    fun a4000x3000PhotoIsCappedTo2048x1536() {
        val landscape = decode(largeJpeg(4000, 3000))
        assertEquals(2048, landscape.width)
        assertEquals(1536, landscape.height)
        assertNear(Color.RED, landscape.getPixel(512, 384), "capped, top left")
        assertNear(Color.YELLOW, landscape.getPixel(1536, 1152), "capped, bottom right")
        landscape.recycle()

        // A portrait photo stored sideways: capped on its upright long side.
        val sideways = jpeg(Bitmap.createScaledBitmap(upright(), 4000, 3000, true), -90f, 6)
        val portrait = decode(sideways)
        assertEquals(2048, portrait.width)
        assertEquals(1536, portrait.height)
        assertNear(Color.RED, portrait.getPixel(512, 384), "capped from EXIF 6, top left")
        portrait.recycle()
    }

    @Test
    fun aSmallPhotoKeepsItsSize() {
        val small = decode(largeJpeg(1000, 750))
        assertEquals(1000, small.width)
        assertEquals(750, small.height)
    }

    /**
     * The photo path's decode and core times for a 4624×3472 JPEG (the Pixel
     * 8a's 4:3 capture size), fingers.png scaled up, logged under
     * hipsterismo-perf. With the screen locked the test process runs on an
     * A715, not the X3, so these are upper bounds of the app's times.
     */
    @Test
    fun aFullSizePhotoTimes() {
        val assets = InstrumentationRegistry.getInstrumentation().targetContext.assets
        val frame = assets.open("fingers.png").use { BitmapFactory.decodeStream(it) }!!
        val big = Bitmap.createScaledBitmap(frame, 4624, 3472, true)
        val out = ByteArrayOutputStream()
        big.compress(Bitmap.CompressFormat.JPEG, 95, out)
        big.recycle()
        val bytes = out.toByteArray()
        repeat(4) { run ->
            val start = System.nanoTime()
            val bitmap = decode(bytes)
            val decodeMs = (System.nanoTime() - start) / 1e6
            val mosaic = NativeMosaic.fromBitmap(bitmap, MinPatchArea.DEFAULT)
            val totalMs = (System.nanoTime() - start) / 1e6
            assertEquals(2048, mosaic.width)
            assertEquals(1538, mosaic.height)
            Log.i(
                "hipsterismo-perf",
                "photo 4624x3472 JPEG run %d -> %dx%d: decode %.0f ms, core %.0f ms, decode + JNI + core %.0f ms, %d Tessels".format(
                    java.util.Locale.ROOT,
                    run, mosaic.width, mosaic.height, decodeMs, mosaic.coreMs, totalMs, mosaic.tesselCount,
                ),
            )
            bitmap.recycle()
        }
    }
}
