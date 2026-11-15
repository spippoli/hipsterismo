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
