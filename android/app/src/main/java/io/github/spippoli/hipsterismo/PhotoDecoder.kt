package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.ColorSpace
import android.graphics.ImageDecoder

/**
 * Decodes a photo, from the shutter's JPEG or the photo picker, into the
 * Frame the core takes: an ARGB_8888 software Bitmap in sRGB, upright (the
 * decoder applies the EXIF orientation itself) and capped at [cap] px on the
 * long side ([cappedSize]). Shutter and gallery share this one path (#50).
 */
object PhotoDecoder {
    fun decode(source: ImageDecoder.Source, cap: Int = PHOTO_CAP): Bitmap {
        val bitmap = ImageDecoder.decodeBitmap(source) { decoder, info, _ ->
            // info.size is already upright: ImageDecoder swaps it for a 90° or
            // 270° EXIF orientation, and the target size is in the same frame.
            val target = cappedSize(info.size.width, info.size.height, cap)
            decoder.setTargetSize(target.width, target.height)
            decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
            decoder.memorySizePolicy = ImageDecoder.MEMORY_POLICY_DEFAULT
            // The core reads sRGB, as on the desktop; a Display P3 photo is converted.
            decoder.setTargetColorSpace(ColorSpace.get(ColorSpace.Named.SRGB))
        }
        if (bitmap.config == Bitmap.Config.ARGB_8888) return bitmap
        // A 10-bit or HDR photo can come back as RGBA_F16 or RGBA_1010102.
        val argb = bitmap.copy(Bitmap.Config.ARGB_8888, false)
        bitmap.recycle()
        return argb
    }
}
