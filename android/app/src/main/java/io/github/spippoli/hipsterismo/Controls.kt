package io.github.spippoli.hipsterismo

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.drag
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.roundToInt

// The camera screen's controls, drawn like the mock's variant A (#57).

val Accent = Color(0xFF7CC4FF)
val PanelDark = Color(0xFF0E0F11)
val OnDark = Color(0xFFF1F1F3)

val MetaGrey = Color(0xFFC9C9CF)

/** How the mock dims a disabled control. */
const val DISABLED_ALPHA = 0.4f

fun labelStyle(size: Float = 12f, color: Color = OnDark) = TextStyle(color = color, fontSize = size.sp)

/**
 * A slider over [count] discrete steps, at step [index]: horizontal with the
 * first step on the left, or [vertical] with the first step at the bottom.
 * Taps and drags snap to the nearest step. Unless [enabled], it is dimmed
 * and ignores them.
 */
@Composable
fun StepSlider(
    index: Int,
    count: Int,
    onIndex: (Int) -> Unit,
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    vertical: Boolean = false,
    enabled: Boolean = true,
) {
    val onIndexNow by rememberUpdatedState(onIndex)
    val thumb = 9.dp
    Canvas(
        modifier
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .semantics {
                contentDescription = label
                stateDescription = value
                progressBarRangeInfo = ProgressBarRangeInfo(index.toFloat(), 0f..(count - 1).toFloat(), count - 2)
                if (enabled) {
                    setProgress { target ->
                        onIndexNow(target.roundToInt().coerceIn(0, count - 1))
                        true
                    }
                } else {
                    disabled()
                }
            }
            .pointerInput(count, vertical, enabled) {
                if (!enabled) return@pointerInput
                val pad = thumb.toPx()
                fun step(position: Offset): Int {
                    val length = if (vertical) size.height else size.width
                    val along = if (vertical) position.y else position.x
                    var fraction = ((along - pad) / (length - 2 * pad)).coerceIn(0f, 1f)
                    if (vertical) fraction = 1 - fraction
                    return (fraction * (count - 1)).roundToInt()
                }
                awaitEachGesture {
                    val down = awaitFirstDown()
                    onIndexNow(step(down.position))
                    drag(down.id) { change ->
                        change.consume()
                        onIndexNow(step(change.position))
                    }
                }
            },
    ) {
        val pad = thumb.toPx()
        fun at(i: Int): Offset {
            val fraction = i.toFloat() / (count - 1)
            return if (vertical) Offset(size.width / 2, size.height - pad - fraction * (size.height - 2 * pad))
            else Offset(pad + fraction * (size.width - 2 * pad), size.height / 2)
        }
        val track = 4.dp.toPx()
        drawLine(Color.White.copy(alpha = 0.3f), at(0), at(count - 1), track, StrokeCap.Round)
        drawLine(Accent, at(0), at(index), track, StrokeCap.Round)
        for (i in 0 until count) drawCircle(if (i <= index) PanelDark.copy(alpha = 0.5f) else Color.White.copy(alpha = 0.6f), 1.5.dp.toPx(), at(i))
        drawCircle(Accent, pad, at(index))
    }
}

/**
 * The round white shutter. While [busy] (a photo is processed) its centre
 * empties, an accent ring spins around it and it ignores taps, at full
 * opacity like the mock.
 */
@Composable
fun Shutter(busy: Boolean, onShoot: () -> Unit, modifier: Modifier = Modifier) {
    Box(
        modifier
            .size(68.dp)
            .clip(CircleShape)
            .clickable(enabled = !busy, role = Role.Button, onClickLabel = "Scatta") { onShoot() }
            // Modifier.border draws over the content, so while busy the ring
            // draws the white circle itself, under its accent arc.
            .then(if (busy) Modifier else Modifier.border(4.dp, Color.White, CircleShape))
            .semantics {
                contentDescription = "Scatta"
                if (busy) stateDescription = "elaborazione"
            },
        contentAlignment = Alignment.Center,
    ) {
        if (busy) ShutterRing(Modifier.size(68.dp)) else Box(Modifier.size(52.dp).background(Color.White, CircleShape))
    }
}

/** The mock's ring: the shutter's white border with a quarter of an accent circle over it, a turn every 0.9 s. */
@Composable
private fun ShutterRing(modifier: Modifier = Modifier) {
    val turn = rememberInfiniteTransition(label = "ring")
    val angle by turn.animateFloat(
        0f, 360f,
        infiniteRepeatable(tween(900, easing = LinearEasing), RepeatMode.Restart),
        label = "angle",
    )
    Canvas(modifier.rotate(angle)) {
        val width = 4.dp.toPx()
        drawCircle(Color.White, radius = (size.minDimension - width) / 2, style = Stroke(width))
        drawArc(
            Accent, startAngle = -135f, sweepAngle = 90f, useCenter = false,
            topLeft = Offset(width / 2, width / 2), size = Size(size.width - width, size.height - width),
            style = Stroke(width),
        )
    }
}

/** The gallery thumbnail, which opens the system photo picker. Dimmed and inert unless [enabled]. */
@Composable
fun GalleryThumb(enabled: Boolean, onPick: () -> Unit, modifier: Modifier = Modifier) {
    Canvas(
        modifier
            .size(44.dp)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .clip(RoundedCornerShape(12.dp))
            .clickable(enabled = enabled, role = Role.Button, onClickLabel = "Galleria") { onPick() }
            .background(Color(0xFF2B2E33))
            .border(2.dp, Color.White.copy(alpha = 0.85f), RoundedCornerShape(12.dp))
            .semantics { contentDescription = "Galleria" },
    ) {
        val u = size.minDimension / 24
        val hills = Path().apply {
            moveTo(5 * u, 18 * u)
            lineTo(10 * u, 11 * u)
            lineTo(13 * u, 15 * u)
            lineTo(15 * u, 13 * u)
            lineTo(19 * u, 18 * u)
            close()
        }
        drawPath(hills, Color.White.copy(alpha = 0.7f))
        drawCircle(Color.White.copy(alpha = 0.7f), 1.8f * u, Offset(16 * u, 8 * u))
    }
}

/** The mock's "texture" glyph in front of the slider. */
@Composable
fun TextureIcon(modifier: Modifier = Modifier) {
    Canvas(modifier.size(18.dp)) {
        val u = size.minDimension / 24
        val stroke = Stroke(width = 1.8f * u, cap = StrokeCap.Round)
        drawRoundRect(OnDark, Offset(3 * u, 3 * u), Size(18 * u, 18 * u), style = stroke,
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(2 * u))
        for (k in listOf(9f, 15f, 21f)) {
            drawLine(OnDark, Offset(3 * u, k * u), Offset(k * u, 3 * u), 1.8f * u, StrokeCap.Round)
        }
        drawLine(OnDark, Offset(9 * u, 21 * u), Offset(21 * u, 9 * u), 1.8f * u, StrokeCap.Round)
    }
}

/** A small dark pill, like the mock's chips. */
@Composable
fun Chip(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Row(
        modifier
            .clip(RoundedCornerShape(50))
            .background(Color.Black.copy(alpha = 0.55f))
            .padding(horizontal = 8.dp, vertical = 3.dp),
        horizontalArrangement = Arrangement.spacedBy(5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) { content() }
}

/**
 * The LIVE chip: a red dot and the preview fps, or an amber dot while the
 * camera starts or the preview is paused. With [stats] (a tap toggles them)
 * it adds the core's ms per Frame and the Tessel count: the debug stats
 * overlay of #59.
 */
@Composable
fun LiveChip(text: String, live: Boolean, onTap: () -> Unit, modifier: Modifier = Modifier) {
    Chip(modifier.clickable(enabled = live, onClickLabel = "Statistiche") { onTap() }) {
        Box(Modifier.size(6.dp).background(if (live) Color(0xFFFF5252) else Color(0xFFFFC107), CircleShape))
        BasicText(text, style = labelStyle(10.5f, Color.White))
    }
}

/** The result screen's glyphs, drawn on a 24-unit grid like the mock's Material Symbols. */
enum class Glyph { BACK, DELETE, SHARE, DOWNLOAD, CHECK }

@Composable
fun GlyphIcon(glyph: Glyph, color: Color, modifier: Modifier = Modifier) {
    Canvas(modifier.size(20.dp)) {
        val u = size.minDimension / 24
        val w = 2f * u
        fun line(x0: Float, y0: Float, x1: Float, y1: Float) =
            drawLine(color, Offset(x0 * u, y0 * u), Offset(x1 * u, y1 * u), w, StrokeCap.Round)
        fun poly(vararg xy: Float) {
            for (i in 0 until xy.size / 2 - 1) line(xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3])
        }
        when (glyph) {
            Glyph.BACK -> {
                line(20f, 12f, 4.5f, 12f)
                poly(11f, 5f, 4f, 12f, 11f, 19f)
            }
            Glyph.DELETE -> {
                line(4f, 6.5f, 20f, 6.5f)
                poly(9f, 6.5f, 9f, 3.5f, 15f, 3.5f, 15f, 6.5f)
                poly(6.5f, 6.5f, 7.5f, 20.5f, 16.5f, 20.5f, 17.5f, 6.5f)
                line(10f, 10.5f, 10f, 16.5f)
                line(14f, 10.5f, 14f, 16.5f)
            }
            Glyph.SHARE -> {
                line(6f, 12f, 18f, 5f)
                line(6f, 12f, 18f, 19f)
                for (c in listOf(Offset(18f, 5f), Offset(6f, 12f), Offset(18f, 19f))) drawCircle(color, 3f * u, c * u)
            }
            Glyph.DOWNLOAD -> {
                line(12f, 3.5f, 12f, 15f)
                poly(7f, 10f, 12f, 15f, 17f, 10f)
                poly(4.5f, 15.5f, 4.5f, 20f, 19.5f, 20f, 19.5f, 15.5f)
            }
            Glyph.CHECK -> poly(4.5f, 12.5f, 9.5f, 17.5f, 19.5f, 7f)
        }
    }
}

/** A round icon button, 40 dp like the mock's `.ib`. */
@Composable
fun IconButton(glyph: Glyph, label: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    Box(
        modifier
            .size(40.dp)
            .clip(CircleShape)
            .clickable(role = Role.Button, onClickLabel = label) { onClick() }
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) { GlyphIcon(glyph, OnDark, Modifier.size(22.dp)) }
}

/**
 * One of the result screen's actions, a pill 40 dp high: outlined in the
 * text colour, or [filled] white with dark text (Salva). Unless [enabled] it is
 * dimmed like the mock's disabled buttons and ignores taps ("Salvato").
 */
@Composable
fun ActionButton(
    text: String,
    glyph: Glyph,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    filled: Boolean = false,
    enabled: Boolean = true,
) {
    val content = if (filled) Color(0xFF111111) else OnDark
    val shape = RoundedCornerShape(50)
    Row(
        modifier
            .height(40.dp)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .clip(shape)
            .then(if (filled) Modifier.background(Color.White, shape) else Modifier.border(1.dp, OnDark, shape))
            .clickable(enabled = enabled, role = Role.Button) { onClick() }
            .padding(horizontal = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        GlyphIcon(glyph, content)
        BasicText(text, style = labelStyle(13.5f, content).copy(fontWeight = FontWeight.Medium), softWrap = false)
    }
}
