package io.github.spippoli.hipsterismo

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
 * [viewHeight] view: whole inside it, centred with its aspect ratio kept.
 */
fun placement(frameWidth: Int, frameHeight: Int, viewWidth: Float, viewHeight: Float): Placement {
    if (frameWidth <= 0 || frameHeight <= 0) return Placement(1f, 0f, 0f)
    val sx = viewWidth / frameWidth
    val sy = viewHeight / frameHeight
    val scale = min(sx, sy)
    return Placement(scale, (viewWidth - frameWidth * scale) / 2, (viewHeight - frameHeight * scale) / 2)
}

/**
 * The zoom chips above the shutter, as in the stock camera app: the camera's
 * widest ratio if it is under 1× (the ultrawide), then 1× and 2×, those the
 * camera reaches. A camera with no zoom at all gets just 1×.
 */
fun zoomPresets(minZoom: Float, maxZoom: Float): List<Float> {
    val presets = mutableListOf<Float>()
    if (minZoom < 1f - ZOOM_EPSILON) presets += minZoom
    for (ratio in listOf(1f, 2f)) if (ratio in minZoom - ZOOM_EPSILON..maxZoom + ZOOM_EPSILON) presets += ratio
    return presets.ifEmpty { listOf(1f) }
}

/**
 * Which of [presets] (ascending) is lit at [zoom]: the largest at or below
 * it, so 1.4× lights the 1× chip, which then reads "1,4×"; the first one
 * below them all.
 */
fun activePreset(presets: List<Float>, zoom: Float): Int =
    presets.indexOfLast { it <= zoom + ZOOM_EPSILON }.coerceAtLeast(0)

/** [zoom] as the chips write it, in Italian: "0,5×", "1×", "1,4×", "10×"; one decimal, none if it is .0. */
fun zoomLabel(zoom: Float): String {
    val tenths = Math.round(zoom * 10)
    val whole = tenths / 10
    val tenth = tenths % 10
    return if (tenth == 0) "$whole×" else "$whole,$tenth×"
}

/** The zoom after a pinch that scaled the fingers' distance by [factor], within the camera's range. */
fun pinchedZoom(zoom: Float, factor: Float, minZoom: Float, maxZoom: Float): Float =
    (zoom * factor).coerceIn(minZoom, maxZoom)

private const val ZOOM_EPSILON = 0.01f

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

/**
 * The angle (degrees, clockwise) the icons turn to read upright while the
 * phone is held at the `Surface.ROTATION_*` [rotation] ([captureRotationFor]):
 * the screen stays in portrait, as in the stock camera app, and only the
 * icons turn. Of the equivalent angles, the one nearest [current], so they
 * turn the short way (an upside-down turn goes anticlockwise from 0).
 */
fun iconAngle(current: Float, rotation: Int): Float {
    val target = rotation * 90f
    val delta = ((target - current) % 360f + 540f) % 360f - 180f
    return current + delta
}

/**
 * How far the `min patch area` row, items [widths] apart by [gap], shifts so
 * that item [index] is centred in [containerWidth], like the stock camera's
 * mode row.
 */
fun selectorOffset(widths: List<Float>, gap: Float, index: Int, containerWidth: Float): Float {
    if (widths.isEmpty()) return 0f
    val i = index.coerceIn(0, widths.lastIndex)
    val before = widths.subList(0, i).sum() + gap * i
    return containerWidth / 2 - (before + widths[i] / 2)
}

/** The row's index after a horizontal drag of [dragPx] from [start]: one step per [stepPx], leftwards to larger steps. */
fun swipedIndex(start: Int, dragPx: Float, stepPx: Float, count: Int): Int =
    (start - Math.round(dragPx / stepPx)).coerceIn(0, count - 1)
