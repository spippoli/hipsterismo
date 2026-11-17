package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer

/**
 * The JNI paths give the core's Mosaic: the hash of each fixture's Mosaic, at
 * the default min patch area, is the golden of tests/core/main.cpp, from a
 * Bitmap, from a ByteBuffer whose rows are padded, and from a camera-like
 * buffer that holds the Frame turned or mirrored, read back upright through a
 * FrameTransform. The hash tests are skipped on an emulator whose ARM
 * translator changes the floats (API 30, see [Emulator.keepsGoldenHashes]).
 */
@RunWith(AndroidJUnit4::class)
class NativeMosaicTest {
    private val goldens = mapOf(
        "fingers.png" to 0xe0d145733a499498uL.toLong(),
        "fingers-003.png" to 0xf64474efb9eefad3uL.toLong(),
        "fingers-060.png" to 0x3d0b480c385887c9uL.toLong(),
    )

    private fun frame(name: String): Bitmap {
        val assets = InstrumentationRegistry.getInstrumentation().targetContext.assets
        val options = BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 }
        return assets.open(name).use { BitmapFactory.decodeStream(it, null, options) }!!
    }

    private fun hex(value: Long) = "0x%016x".format(value)

    private fun assumeGoldenHashes() =
        assumeTrue("this emulator's ARM translator changes the core's floats", Emulator.keepsGoldenHashes)

    @Test
    fun bitmapGivesTheGoldenHash() {
        assumeGoldenHashes()
        for ((name, golden) in goldens) {
            val bitmap = frame(name)
            assertEquals(Bitmap.Config.ARGB_8888, bitmap.config)
            assertEquals(name, hex(golden), hex(NativeMosaic.fromBitmap(bitmap).hash))
        }
    }

    @Test
    fun paddedByteBufferGivesTheGoldenHash() {
        assumeGoldenHashes()
        for ((name, golden) in goldens) {
            val bitmap = frame(name)
            val packed = ByteBuffer.allocate(bitmap.byteCount)
            bitmap.copyPixelsToBuffer(packed) // RGBA rows, bitmap.rowBytes apart
            val rowStride = bitmap.width * 4 + 36 // 9 pixels of padding per row
            val padded = ByteBuffer.allocateDirect(rowStride * bitmap.height)
            for (y in 0 until bitmap.height) {
                for (i in 0 until rowStride) padded.put(y * rowStride + i, 0x5a)
                for (i in 0 until bitmap.width * 4) padded.put(y * rowStride + i, packed.get(y * bitmap.rowBytes + i))
            }
            val mosaic = NativeMosaic.fromRgba(padded, bitmap.width, bitmap.height, rowStride)
            assertEquals(name, hex(golden), hex(mosaic.hash))
            assertEquals(bitmap.width, mosaic.width)
        }
    }

    @Test
    fun bothPathsGiveTheSameMosaic() {
        val bitmap = frame("fingers.png")
        val buffer = ByteBuffer.allocateDirect(bitmap.byteCount)
        bitmap.copyPixelsToBuffer(buffer)
        val fromBitmap = NativeMosaic.fromBitmap(bitmap)
        val fromBuffer = NativeMosaic.fromRgba(buffer, bitmap.width, bitmap.height, bitmap.rowBytes)
        assertEquals(fromBitmap.tesselCount, fromBuffer.tesselCount)
        assertArrayEquals(fromBitmap.vertices, fromBuffer.vertices, 0f)
        assertArrayEquals(fromBitmap.colors, fromBuffer.colors)
    }

    /**
     * A direct buffer holding [bitmap] as a sensor would deliver it before a
     * clockwise turn by [rotation] (and a mirror flip): pixel (x, y) of the
     * upright Frame is stored where [FrameTransform.of] reads it.
     */
    private fun cameraBuffer(bitmap: Bitmap, rotation: Int, mirror: Boolean): Pair<ByteBuffer, FrameTransform> {
        val quarter = rotation % 180 != 0
        val sourceWidth = if (quarter) bitmap.height else bitmap.width
        val sourceHeight = if (quarter) bitmap.width else bitmap.height
        val rowStride = sourceWidth * 4 + 64
        val transform = FrameTransform.of(sourceWidth, sourceHeight, rowStride, 4, rotation, mirror)
        assertEquals(bitmap.width, transform.width)
        val buffer = ByteBuffer.allocateDirect(rowStride * sourceHeight)
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        for (y in 0 until bitmap.height) for (x in 0 until bitmap.width) {
            val argb = pixels[y * bitmap.width + x]
            val at = transform.offsetOf(x, y)
            buffer.put(at, (argb shr 16).toByte())
            buffer.put(at + 1, (argb shr 8).toByte())
            buffer.put(at + 2, argb.toByte())
            buffer.put(at + 3, (argb ushr 24).toByte())
        }
        return buffer to transform
    }

    @Test
    fun turnedAndMirroredBuffersGiveTheGoldenHash() {
        assumeGoldenHashes()
        val bitmap = frame("fingers.png")
        for (rotation in listOf(0, 90, 180, 270)) for (mirror in listOf(false, true)) {
            val (buffer, transform) = cameraBuffer(bitmap, rotation, mirror)
            val mosaic = NativeMosaic.fromFrame(buffer, transform)
            assertEquals("rotation $rotation mirror $mirror", hex(goldens.getValue("fingers.png")), hex(mosaic.hash))
            assertEquals(320, mosaic.width)
            assertEquals(240, mosaic.height)
        }
    }

    @Test
    fun contoursAreSegmentsInsideTheFrame() {
        val mosaic = NativeMosaic.fromBitmap(frame("fingers.png"))
        assertTrue("no Contours", mosaic.contours.isNotEmpty())
        assertEquals(0, mosaic.contours.size % 4)
        for (i in mosaic.contours.indices step 2) {
            assertTrue(mosaic.contours[i] in 0f..320f && mosaic.contours[i + 1] in 0f..240f)
        }
        assertTrue(mosaic.coreMs > 0f)
    }

    @Test
    fun aTransformOutsideTheBufferIsRejected() {
        val buffer = ByteBuffer.allocateDirect(320 * 240 * 4)
        val tooTall = FrameTransform.of(320, 241, 320 * 4)
        assertThrows(IllegalArgumentException::class.java) { NativeMosaic.fromFrame(buffer, tooTall) }
        val negative = FrameTransform(320, 240, 0, -4, 320 * 4)
        assertThrows(IllegalArgumentException::class.java) { NativeMosaic.fromFrame(buffer, negative) }
    }
}
