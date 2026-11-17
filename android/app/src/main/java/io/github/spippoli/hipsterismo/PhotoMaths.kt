package io.github.spippoli.hipsterismo

import kotlin.math.max
import kotlin.math.roundToInt

// The photo path's plain arithmetic, with no Android classes, so the JVM tests
// cover it.

/** A width × height in pixels. */
data class PixelSize(val width: Int, val height: Int)

/** The long-side cap of a photo's Frame: 2048 px (#53). */
const val PHOTO_CAP = 2048

/**
 * The size a [width] × [height] photo is decoded at: scaled down so its long
 * side is [cap], the aspect ratio kept and the short side rounded to the
 * nearest pixel (at least 1); a photo whose long side is already at most [cap]
 * keeps its size. 4000×3000 gives 2048×1536.
 */
fun cappedSize(width: Int, height: Int, cap: Int = PHOTO_CAP): PixelSize {
    require(width > 0 && height > 0) { "the photo must have a size, not ${width}x$height" }
    require(cap > 0) { "the cap must be positive, not $cap" }
    val long = max(width, height)
    if (long <= cap) return PixelSize(width, height)
    fun scaled(side: Int) = if (side == long) cap else max(1, (side.toLong() * cap / long.toDouble()).roundToInt())
    return PixelSize(scaled(width), scaled(height))
}

/**
 * The shutter's pixel cap: 13 MP, so "near 12 MP" (#101). It takes the 12 MP
 * class of sizes (4000×3000, 4032×3024, 4080×3060 and 4080×3072 at 12.5 MP,
 * 4160×3120 at 13.0 MP), which every 48–200 MP sensor offers binned, but not
 * 16 MP and up. Photos are decoded to [PHOTO_CAP] anyway; a larger JPEG only
 * costs capture and decode time (#94).
 */
const val CAPTURE_CAP_PIXELS = 13_000_000L

/** Whether [size] is 4:3 either way round, within 1%: sensor sizes are a few pixels off (4624×3472). */
fun isFourByThree(size: PixelSize): Boolean {
    val long = max(size.width, size.height).toDouble()
    val short = minOf(size.width, size.height).toDouble()
    return kotlin.math.abs(long / short - 4.0 / 3.0) <= 0.01 * 4.0 / 3.0
}

/**
 * The shutter's JPEG size out of the camera's [sizes]: the largest 4:3 one of
 * at most [capPixels] pixels, else (every 4:3 size over the cap) the smallest
 * 4:3 one. Without any 4:3 size the same rule picks from all of them. A
 * camera whose largest 4:3 size is under the cap keeps it, as before #101.
 * Null for an empty list.
 */
fun captureSize(sizes: List<PixelSize>, capPixels: Long = CAPTURE_CAP_PIXELS): PixelSize? {
    val candidates = sizes.filter(::isFourByThree).ifEmpty { sizes }
    fun PixelSize.pixels() = width.toLong() * height
    return candidates.filter { it.pixels() <= capPixels }.maxByOrNull { it.pixels() }
        ?: candidates.minByOrNull { it.pixels() }
}

/**
 * The shutter's target rotation (a `Surface.ROTATION_*` value: 0, 1, 2 or 3)
 * for the device held at [degrees], the clockwise angle that
 * `OrientationEventListener` reports (0 upright, 90 turned right). It follows
 * the phone, not the display, so a photo taken in landscape is a landscape
 * photo even with auto-rotate off, as in the system camera. Each turn covers
 * the 90° around it; null for an unknown angle (the phone lying flat), when
 * the last rotation stays.
 */
fun captureRotationFor(degrees: Int): Int? {
    if (degrees < 0) return null
    return when (degrees % 360) {
        in 45 until 135 -> 3 // Surface.ROTATION_270
        in 135 until 225 -> 2 // Surface.ROTATION_180
        in 225 until 315 -> 1 // Surface.ROTATION_90
        else -> 0 // Surface.ROTATION_0
    }
}

/** Where a photo came from: the shutter or the system photo picker. */
enum class PhotoSource(val label: String) { CAMERA("camera"), GALLERY("gallery") }

/**
 * The times of one photo, in ms from the shutter tap (or the photo picker's
 * answer) to the result screen's first draw: [captureMs] until the JPEG is in
 * the cache (0 for a picked photo), [decodeMs] for `ImageDecoder`, [coreMs]
 * for `makeAdaptive()`, and [totalMs] for everything, the rest being the JNI
 * copy, the hand-over to the UI thread and the first draw.
 */
data class PhotoTiming(
    val source: PhotoSource,
    val width: Int,
    val height: Int,
    val totalMs: Double,
    val captureMs: Double,
    val decodeMs: Double,
    val coreMs: Double,
    val tessels: Int,
    val minPatchArea: Int,
) {
    /** The core's share of [totalMs], in percent. */
    val corePercent: Double get() = if (totalMs > 0) 100 * coreMs / totalMs else 0.0

    /** The logcat line, under [PHOTO_TAG]. */
    fun line(): String = "photo %s %dx%d: %.0f ms to the result (capture %.0f, decode %.0f, core %.0f = %.0f%%, other %.0f), %d Tessels, min patch area %d".format(
        java.util.Locale.ROOT,
        source.label, width, height, totalMs, captureMs, decodeMs, coreMs, corePercent,
        totalMs - captureMs - decodeMs - coreMs, tessels, minPatchArea,
    )
}

/** The logcat tag of the photo times. */
const val PHOTO_TAG = "hipsterismo-photo"
