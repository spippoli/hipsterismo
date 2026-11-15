package io.github.spippoli.hipsterismo

import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

// The saved Mosaic's plain arithmetic (#58), with no Android classes, so the
// JVM tests cover it.

/** The album a saved Mosaic goes to, MediaStore's `RELATIVE_PATH` ("Immagini/Hipsterismo" in Italian). */
const val SAVE_RELATIVE_PATH = "Pictures/Hipsterismo"

/** The saved and shared file's MIME type. */
const val MOSAIC_MIME = "image/png"

private val FILE_STAMP = DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss", java.util.Locale.ROOT)

/**
 * The saved (and shared) Mosaic's file name, `hipsterismo_YYYYMMDD_HHMMSS.png`
 * at [now], the local time of the save. MediaStore adds its own suffix on a clash.
 */
fun mosaicFileName(now: LocalDateTime): String = "hipsterismo_${now.format(FILE_STAMP)}.png"

/**
 * The saved Mosaic's `DATE_TAKEN`, in ms since the epoch, so it sorts next to
 * its source in the gallery: [startMillis] (the shutter tap) for a camera
 * photo; for a gallery photo the original's [originalMillis], or
 * [startMillis] (the picker's answer) if the original has none.
 */
fun mosaicDateTaken(source: PhotoSource, startMillis: Long, originalMillis: Long?): Long = when (source) {
    PhotoSource.CAMERA -> startMillis
    PhotoSource.GALLERY -> originalMillis?.takeIf { it > 0 } ?: startMillis
}
