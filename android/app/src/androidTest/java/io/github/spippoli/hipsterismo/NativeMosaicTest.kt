package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer

/**
 * The JNI paths give the core's Mosaic: the hash of each fixture's Mosaic, at
 * the default min patch area, is the golden of tests/core/main.cpp, from a
 * Bitmap and from a ByteBuffer whose rows are padded.
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

    @Test
    fun bitmapGivesTheGoldenHash() {
        for ((name, golden) in goldens) {
            val bitmap = frame(name)
            assertEquals(Bitmap.Config.ARGB_8888, bitmap.config)
            assertEquals(name, hex(golden), hex(NativeMosaic.fromBitmap(bitmap).hash))
        }
    }

    @Test
    fun paddedByteBufferGivesTheGoldenHash() {
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
}
