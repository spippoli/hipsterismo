package io.github.spippoli.hipsterismo

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
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
import androidx.compose.ui.draw.clip
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
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.roundToInt

// The camera screen's controls, drawn like the mock's variant A (#57).

val Accent = Color(0xFF7CC4FF)
val PanelDark = Color(0xFF0E0F11)
val OnDark = Color(0xFFF1F1F3)

fun labelStyle(size: Float = 12f, color: Color = OnDark) = TextStyle(color = color, fontSize = size.sp)

/**
 * A slider over [count] discrete steps, at step [index]: horizontal with the
 * first step on the left, or [vertical] with the first step at the bottom.
 * Taps and drags snap to the nearest step.
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
) {
    val onIndexNow by rememberUpdatedState(onIndex)
    val thumb = 9.dp
    Canvas(
        modifier
            .semantics {
                contentDescription = label
                stateDescription = value
                progressBarRangeInfo = ProgressBarRangeInfo(index.toFloat(), 0f..(count - 1).toFloat(), count - 2)
                setProgress { target ->
                    onIndexNow(target.roundToInt().coerceIn(0, count - 1))
                    true
                }
            }
            .pointerInput(count, vertical) {
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

/** The round white shutter. A placeholder: photos come with the next ticket (#65). */
@Composable
fun Shutter(modifier: Modifier = Modifier) {
    Box(
        modifier.size(68.dp).border(4.dp, Color.White, CircleShape).semantics { contentDescription = "Scatta" },
        contentAlignment = Alignment.Center,
    ) {
        Box(Modifier.size(52.dp).background(Color.White, CircleShape))
    }
}

/** The gallery thumbnail. A placeholder: the photo picker comes with the next ticket (#65). */
@Composable
fun GalleryThumb(modifier: Modifier = Modifier) {
    Canvas(
        modifier
            .size(44.dp)
            .clip(RoundedCornerShape(12.dp))
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

/** The Contour overlay toggle, a polyline icon in a circle that lights up when [on]. */
@Composable
fun OverlayButton(on: Boolean, onToggle: () -> Unit, modifier: Modifier = Modifier) {
    Box(
        modifier
            .size(40.dp)
            .clip(CircleShape)
            .background(if (on) Color.White.copy(alpha = 0.22f) else Color.Transparent)
            .clickable(role = Role.Switch, onClickLabel = "Overlay dei Contour") { onToggle() }
            .semantics {
                contentDescription = "Overlay dei Contour"
                stateDescription = if (on) "attivo" else "spento"
            },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(22.dp)) { polyline() }
    }
}

private fun DrawScope.polyline() {
    val u = size.minDimension / 24
    val nodes = listOf(Offset(4f, 18f), Offset(9f, 7f), Offset(15f, 16f), Offset(20f, 6f)).map { it * u }
    for (i in 0 until nodes.lastIndex) drawLine(OnDark, nodes[i], nodes[i + 1], 1.8f * u, StrokeCap.Round)
    for (n in nodes) drawRect(OnDark, n - Offset(2.2f * u, 2.2f * u), Size(4.4f * u, 4.4f * u))
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
 * camera starts. With [stats] (a tap toggles them) it adds the core's ms per
 * Frame and the Tessel count: the debug stats overlay of #59.
 */
@Composable
fun LiveChip(text: String, live: Boolean, onTap: () -> Unit, modifier: Modifier = Modifier) {
    Chip(modifier.clickable(onClickLabel = "Statistiche") { onTap() }) {
        Box(Modifier.size(6.dp).background(if (live) Color(0xFFFF5252) else Color(0xFFFFC107), CircleShape))
        BasicText(text, style = labelStyle(10.5f, Color.White))
    }
}
