package io.github.spippoli.hipsterismo

import kotlin.math.max
import kotlin.math.min

// The camera screen's plain arithmetic, with no Android classes, so the JVM
// tests cover it.

/** The `min patch area` slider: the mock's eight steps, doubling from 8 to 1024 px². */
object MinPatchArea {
    const val DEFAULT = 32
    val steps: List<Int> = List(8) { 8 shl it }
    val defaultIndex: Int = steps.indexOf(DEFAULT)

    /** The step for slider index [index], clamped to the ends. */
    fun at(index: Int): Int = steps[index.coerceIn(0, steps.lastIndex)]
}

/** A Frame drawn on screen: Frame pixel (x, y) lands at (dx + x · scale, dy + y · scale). */
data class Placement(val scale: Float, val dx: Float, val dy: Float)

/**
 * Where a [frameWidth] × [frameHeight] Frame goes in a [viewWidth] ×
 * [viewHeight] view, centred with its aspect ratio kept: covering the whole
 * view and cropped if [crop] (the live preview), or whole inside it.
 */
fun placement(frameWidth: Int, frameHeight: Int, viewWidth: Float, viewHeight: Float, crop: Boolean): Placement {
    if (frameWidth <= 0 || frameHeight <= 0) return Placement(1f, 0f, 0f)
    val sx = viewWidth / frameWidth
    val sy = viewHeight / frameHeight
    val scale = if (crop) max(sx, sy) else min(sx, sy)
    return Placement(scale, (viewWidth - frameWidth * scale) / 2, (viewHeight - frameHeight * scale) / 2)
}

/** The Contour overlay's line width in Frame pixels: the short side / 240, so 1 px at 320×240 (#59). */
fun overlayStrokeWidth(frameWidth: Int, frameHeight: Int): Float = min(frameWidth, frameHeight) / 240f

/**
 * The events of the last [windowNanos] and their values: the preview's fps
 * (Mosaics drawn in the last second) and the core's mean ms per Frame.
 * Thread-safe: the preview thread adds, the UI reads.
 */
class RollingWindow(private val windowNanos: Long = 1_000_000_000L) {
    private val times = ArrayDeque<Long>()
    private val values = ArrayDeque<Double>()
    private var sum = 0.0

    @Synchronized
    fun add(timeNanos: Long, value: Double = 0.0) {
        times.addLast(timeNanos)
        values.addLast(value)
        sum += value
        dropBefore(timeNanos)
    }

    /** Events per second over the window ending at [nowNanos]. */
    @Synchronized
    fun rate(nowNanos: Long): Double {
        dropBefore(nowNanos)
        return times.size * 1e9 / windowNanos
    }

    /** The mean value over the window ending at [nowNanos], or NaN if it is empty. */
    @Synchronized
    fun mean(nowNanos: Long): Double {
        dropBefore(nowNanos)
        return if (times.isEmpty()) Double.NaN else sum / times.size
    }

    private fun dropBefore(nowNanos: Long) {
        while (times.isNotEmpty() && times.first() <= nowNanos - windowNanos) {
            times.removeFirst()
            sum -= values.removeFirst()
        }
        if (times.isEmpty()) sum = 0.0
    }
}
