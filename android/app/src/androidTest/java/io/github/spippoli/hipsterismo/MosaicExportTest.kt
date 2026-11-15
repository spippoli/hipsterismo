package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.ExifInterface
import android.provider.MediaStore
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayOutputStream
import java.time.LocalDateTime

/**
 * Salva and Condividi (#66): the PNG is the Mosaic at its working size, drawn
 * as the screen draws it, with no EXIF; Salva puts it in Pictures/Hipsterismo,
 * and Condividi's file is in the share cache, not
 * in the gallery. Every image the test saves is deleted again.
 */
@RunWith(AndroidJUnit4::class)
class MosaicExportTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    private fun fixture(): Bitmap {
        val options = BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 }
        return context.assets.open("fingers.png").use { BitmapFactory.decodeStream(it, null, options) }!!
    }

    private fun pixels(bitmap: Bitmap) = IntArray(bitmap.width * bitmap.height).also {
        bitmap.getPixels(it, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
    }

    private fun assertNoExif(bytes: ByteArray) {
        val exif = ExifInterface(bytes.inputStream())
        for (tag in listOf(
            ExifInterface.TAG_DATETIME, ExifInterface.TAG_DATETIME_ORIGINAL,
            ExifInterface.TAG_MAKE, ExifInterface.TAG_MODEL, ExifInterface.TAG_GPS_LATITUDE, ExifInterface.TAG_GPS_LONGITUDE,
        )) {
            assertNull("EXIF $tag", exif.getAttribute(tag))
        }
        assertNull("GPS", exif.getLatLong(FloatArray(2)).takeIf { it })
        // (ExifInterface reports Orientation "0" for any file without one.)
        // No eXIf chunk at all, and no "Exif" header hidden in another chunk.
        val text = String(bytes, Charsets.ISO_8859_1)
        assertTrue("an eXIf chunk", text.indexOf("eXIf") < 0)
        assertTrue("an Exif header", text.indexOf("Exif\u0000") < 0)
    }

    @Test
    fun thePngIsTheScreensDrawingAtTheWorkingSize() {
        val mosaic = NativeMosaic.fromBitmap(fixture())
        val plain = MosaicExport.render(mosaic, overlay = false)
        assertEquals(mosaic.width, plain.width)
        assertEquals(mosaic.height, plain.height)
        val plainPixels = pixels(plain)
        // Only Tessel colours, as in MosaicDrawingTest, and opaque.
        val tesselColours = mosaic.colors.toSet()
        assertEquals("pixels without a Tessel's colour", 0, plainPixels.count { it !in tesselColours })

        // The overlay draws over some pixels, and only lightens them.
        val withContours = pixels(MosaicExport.render(mosaic, overlay = true))
        val changed = plainPixels.indices.count { plainPixels[it] != withContours[it] }
        assertTrue("$changed pixels changed by the Contours", changed > plainPixels.size / 50)

        // The PNG decodes back to the same pixels.
        val png = ByteArrayOutputStream().also { MosaicExport.writePng(plain, it) }.toByteArray()
        val decoded = BitmapFactory.decodeByteArray(png, 0, png.size)
        assertArrayEquals(plainPixels, pixels(decoded))
        assertNoExif(png)
    }

    @Test
    fun aPhotoSizedMosaicKeepsItsSize() {
        // A 2048×1536 Frame, the shutter's working size.
        val frame = Bitmap.createScaledBitmap(fixture(), 2048, 1536, true)
        val mosaic = NativeMosaic.fromBitmap(frame)
        val drawn = MosaicExport.render(mosaic, overlay = true)
        assertEquals(2048, drawn.width)
        assertEquals(1536, drawn.height)
    }

    @Test
    fun salvaSavesToTheAlbumWithNoExif() {
        val mosaic = NativeMosaic.fromBitmap(fixture())
        val name = mosaicFileName(LocalDateTime.of(2001, 2, 3, 4, 5, 6))
        val dateTaken = 981_173_106_000L // 2001-02-03 04:05:06 UTC
        val uri = MosaicExport.saveToGallery(context, MosaicExport.render(mosaic, overlay = false), name, dateTaken)
        try {
            val resolver = context.contentResolver
            val columns = arrayOf(
                MediaStore.Images.Media.DISPLAY_NAME, MediaStore.Images.Media.RELATIVE_PATH,
                MediaStore.Images.Media.DATE_TAKEN, MediaStore.Images.Media.MIME_TYPE,
                MediaStore.Images.Media.WIDTH, MediaStore.Images.Media.HEIGHT, MediaStore.Images.Media.IS_PENDING,
            )
            resolver.query(uri, columns, null, null, null)!!.use { c ->
                assertTrue(c.moveToFirst())
                assertEquals(name, c.getString(0))
                assertEquals("Pictures/Hipsterismo/", c.getString(1))
                // MediaProvider's publish scan takes DATE_TAKEN only from
                // EXIF and clears it without (Android 17); it must never be another date.
                val taken = if (c.isNull(2)) null else c.getLong(2)
                Log.i("hipsterismo-perf", "saved DATE_TAKEN $taken (given $dateTaken)")
                assertTrue("DATE_TAKEN $taken", taken == null || taken == dateTaken)
                assertEquals("image/png", c.getString(3))
                assertEquals(320, c.getInt(4))
                assertEquals(240, c.getInt(5))
                assertEquals(0, c.getInt(6))
            }
            val bytes = resolver.openInputStream(uri)!!.use { it.readBytes() }
            val decoded = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
            assertEquals(320, decoded.width)
            assertEquals(240, decoded.height)
            assertNoExif(bytes)
        } finally {
            assertEquals(1, context.contentResolver.delete(uri, null, null))
        }
    }

    @Test
    fun condividiWritesToTheCacheAndNotToTheGallery() {
        val mosaic = NativeMosaic.fromBitmap(fixture())
        val name = mosaicFileName(LocalDateTime.of(2001, 2, 3, 4, 5, 7))
        val uri = MosaicExport.writeShareFile(context, MosaicExport.render(mosaic, overlay = false), name)
        try {
            assertEquals(MosaicExport.authority(context), uri.authority)
            val bytes = context.contentResolver.openInputStream(uri)!!.use { it.readBytes() }
            val decoded = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
            assertNotNull(decoded)
            assertEquals(320, decoded.width)
            assertNoExif(bytes)
            // A second share replaces the first file.
            MosaicExport.writeShareFile(context, MosaicExport.render(mosaic, overlay = true), name)
            assertEquals(listOf(name), MosaicExport.shareCache(context).list()!!.toList())

            // Nothing of that name in MediaStore.
            context.contentResolver.query(
                MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY),
                arrayOf(MediaStore.Images.Media._ID),
                "${MediaStore.Images.Media.DISPLAY_NAME} = ?", arrayOf(name), null,
            )!!.use { assertEquals(0, it.count) }

            val share = MosaicExport.shareIntent(uri)
            val send = share.getParcelableExtra(android.content.Intent.EXTRA_INTENT, android.content.Intent::class.java)!!
            assertEquals(android.content.Intent.ACTION_SEND, send.action)
            assertEquals("image/png", send.type)
            assertEquals(uri, send.getParcelableExtra(android.content.Intent.EXTRA_STREAM, android.net.Uri::class.java))
            assertTrue(send.flags and android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION != 0)
        } finally {
            MosaicExport.shareCache(context).deleteRecursively()
        }
    }
}
