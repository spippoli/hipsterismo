package io.github.spippoli.hipsterismo

import android.content.ClipData
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Picture
import android.net.Uri
import android.provider.MediaStore
import androidx.core.content.FileProvider
import java.io.File
import java.io.OutputStream

/**
 * Salva and Condividi (#58, #66): a photo's Mosaic as a PNG at its working
 * size, drawn by the screen's own [drawMosaic] and [drawContours], saved to
 * MediaStore or written to the share cache.
 */
object MosaicExport {
    /**
     * [mosaic] drawn at its own size, one Frame pixel per pixel, with its
     * Contours if [overlay]: the same calls and paints as [MosaicCanvas],
     * through the same hardware renderer (a [Picture] into a hardware
     * Bitmap), copied into an ARGB_8888 Bitmap the PNG encoder takes.
     */
    fun render(mosaic: Mosaic, overlay: Boolean): Bitmap {
        val picture = Picture()
        val canvas = picture.beginRecording(mosaic.width, mosaic.height)
        canvas.drawMosaic(mosaic, Paint().apply { color = Color.WHITE })
        if (overlay) canvas.drawContours(mosaic, contourPaint())
        picture.endRecording()
        val hardware = Bitmap.createBitmap(picture)
        return try {
            hardware.copy(Bitmap.Config.ARGB_8888, false)
        } finally {
            hardware.recycle()
        }
    }

    /** Encodes [bitmap] as a PNG: no EXIF, so no GPS or camera data. */
    fun writePng(bitmap: Bitmap, out: OutputStream) {
        check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)) { "the PNG encoder failed" }
    }

    /**
     * Saves [bitmap] as [name] in `Pictures/Hipsterismo` through MediaStore,
     * with no permission (the app's own file, minSdk 29), and [dateTakenMillis]
     * as its `DATE_TAKEN` while it is pending (see below). The row is pending
     * until the file is complete, and removed if writing fails. Returns its
     * content Uri.
     */
    fun saveToGallery(context: Context, bitmap: Bitmap, name: String, dateTakenMillis: Long): Uri {
        val resolver = context.contentResolver
        val collection = MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, name)
            put(MediaStore.Images.Media.MIME_TYPE, MOSAIC_MIME)
            put(MediaStore.Images.Media.RELATIVE_PATH, SAVE_RELATIVE_PATH)
            put(MediaStore.Images.Media.DATE_TAKEN, dateTakenMillis)
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }
        val uri = checkNotNull(resolver.insert(collection, values)) { "MediaStore refused the new image" }
        try {
            checkNotNull(resolver.openOutputStream(uri)) { "cannot open $uri" }.use { writePng(bitmap, it) }
            // Publishing scans the file. On Android 17 that scan takes
            // DATE_TAKEN only from EXIF, so with none it clears the one given
            // at the insert, and the row then refuses a new DATE_TAKEN (an
            // update changes 0 rows). See #66's PR.
            resolver.update(uri, ContentValues().apply { put(MediaStore.Images.Media.IS_PENDING, 0) }, null, null)
        } catch (e: Exception) {
            resolver.delete(uri, null, null)
            throw e
        }
        return uri
    }

    /** The share cache, `cacheDir/shared`; [MainActivity] empties it at every start. */
    fun shareCache(context: Context) = File(context.cacheDir, "shared")

    /**
     * Writes [bitmap] as [name] into the share cache and returns its
     * [FileProvider] Uri. Nothing goes to the gallery. Earlier share files are
     * removed first, so at most one is left.
     */
    fun writeShareFile(context: Context, bitmap: Bitmap, name: String): Uri {
        val dir = shareCache(context)
        dir.deleteRecursively()
        dir.mkdirs()
        val file = File(dir, name)
        file.outputStream().use { writePng(bitmap, it) }
        return FileProvider.getUriForFile(context, authority(context), file)
    }

    /** The FileProvider's authority, declared in the manifest. */
    fun authority(context: Context) = "${context.packageName}.fileprovider"

    /** The share sheet for the PNG at [uri]: `ACTION_SEND`, `image/png`, read permission granted. */
    fun shareIntent(uri: Uri): Intent {
        val send = Intent(Intent.ACTION_SEND).apply {
            type = MOSAIC_MIME
            putExtra(Intent.EXTRA_STREAM, uri)
            // The ClipData carries the grant to the chooser and its target, and the preview.
            clipData = ClipData.newRawUri(null, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        return Intent.createChooser(send, null)
    }

    /** Apri: the saved PNG at [uri] in the gallery app. */
    fun viewIntent(uri: Uri) = Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, MOSAIC_MIME)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }

    /**
     * The original's `DATE_TAKEN` for a photo picked at [uri], or null if the
     * picker or the provider has none.
     */
    fun originalDateTaken(context: Context, uri: Uri): Long? = try {
        context.contentResolver.query(uri, arrayOf(MediaStore.Images.Media.DATE_TAKEN), null, null, null)?.use { c ->
            if (c.moveToFirst() && !c.isNull(0)) c.getLong(0) else null
        }
    } catch (e: Exception) {
        null
    }
}
