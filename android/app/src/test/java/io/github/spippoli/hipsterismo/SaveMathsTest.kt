package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.LocalDateTime

class SaveMathsTest {
    @Test
    fun theFileNameIsTheLocalTimeOfTheSave() {
        assertEquals("hipsterismo_20261115_093007.png", mosaicFileName(LocalDateTime.of(2026, 11, 15, 9, 30, 7)))
        // Zero-padded, on a 24-hour clock.
        assertEquals("hipsterismo_20270102_230000.png", mosaicFileName(LocalDateTime.of(2027, 1, 2, 23, 0, 0)))
        // Fractions of a second are dropped, not rounded.
        assertEquals("hipsterismo_20261231_235959.png", mosaicFileName(LocalDateTime.of(2026, 12, 31, 23, 59, 59, 999_000_000)))
    }

    @Test
    fun theAlbumAndType() {
        assertEquals("Pictures/Hipsterismo", SAVE_RELATIVE_PATH)
        assertEquals("image/png", MOSAIC_MIME)
    }

    @Test
    fun aCameraPhotoIsTakenAtTheShutter() {
        assertEquals(1_000L, mosaicDateTaken(PhotoSource.CAMERA, 1_000L, null))
        // A camera photo has no original to take a date from.
        assertEquals(1_000L, mosaicDateTaken(PhotoSource.CAMERA, 1_000L, 5L))
    }

    @Test
    fun aGalleryPhotoKeepsTheOriginalsDate() {
        assertEquals(5L, mosaicDateTaken(PhotoSource.GALLERY, 1_000L, 5L))
        // An original without a date: the picker's answer.
        assertEquals(1_000L, mosaicDateTaken(PhotoSource.GALLERY, 1_000L, null))
        assertEquals(1_000L, mosaicDateTaken(PhotoSource.GALLERY, 1_000L, 0L))
        assertEquals(1_000L, mosaicDateTaken(PhotoSource.GALLERY, 1_000L, -1L))
    }
}
