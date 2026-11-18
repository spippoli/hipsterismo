package io.github.spippoli.hipsterismo

import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.view.OrientationEventListener
import android.view.Surface
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
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
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

// The controls, drawn like the stock Pixel camera app: white and greys on
// black, round buttons, a mode row with the selection in a pill.

val Accent = Color(0xFFA8C7FA)
val PanelDark = Color.Black
val OnDark = Color(0xFFE3E3E3)
val MetaGrey = Color(0xFFC4C7C5)

/** The dark grey of the camera's round buttons and the selected pill. */
val ButtonGrey = Color(0xFF303030)

/** How a disabled control is dimmed. */
const val DISABLED_ALPHA = 0.4f

fun labelStyle(size: Float = 12f, color: Color = OnDark) = TextStyle(color = color, fontSize = size.sp)

/**
 * The angle the icons turn by ([iconAngle]) to read upright, following how
 * the phone is held relative to the window, animated the short way. On a
 * phone the window stays in portrait; on a large screen it may turn with the
 * device, so the display's rotation is read at every turn of either.
 */
@Composable
fun rememberIconAngle(): State<Float> {
    val context = LocalContext.current
    val view = LocalView.current
    var target by remember { mutableFloatStateOf(0f) }
    DisposableEffect(context, view) {
        var held: Int? = null
        fun update() {
            val rotation = held ?: return
            val next = iconAngle(target, rotation, view.display?.rotation ?: Surface.ROTATION_0)
            if (next != target) target = next
        }
        val listener = object : OrientationEventListener(context) {
            override fun onOrientationChanged(orientation: Int) {
                held = captureRotationFor(orientation) ?: return
                update()
            }
        }
        val displays = context.getSystemService(DisplayManager::class.java)
        val displayListener = object : DisplayManager.DisplayListener {
            override fun onDisplayAdded(displayId: Int) {}
            override fun onDisplayRemoved(displayId: Int) {}
            override fun onDisplayChanged(displayId: Int) {
                if (displayId == view.display?.displayId) update()
            }
        }
        displays.registerDisplayListener(displayListener, Handler(Looper.getMainLooper()))
        if (listener.canDetectOrientation()) listener.enable()
        onDispose {
            listener.disable()
            displays.unregisterDisplayListener(displayListener)
        }
    }
    return animateFloatAsState(target, tween(320), label = "icons")
}

/**
 * The `min patch area` row, in place of the camera's mode row: the steps of
 * [MinPatchArea] side by side, the selected one ([index]) centred in a grey
 * pill and reading "32 px²", the others fading towards the edges. A tap picks
 * a step, a swipe moves along the row. Dimmed and inert unless [enabled].
 */
@Composable
fun PatchAreaSelector(
    index: Int,
    onIndex: (Int) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val steps = MinPatchArea.steps
    val onIndexNow by rememberUpdatedState(onIndex)
    val indexNow by rememberUpdatedState(index)
    val widths = remember { mutableStateListOf<Float>().apply { repeat(steps.size) { add(0f) } } }
    var containerWidth by remember { mutableFloatStateOf(0f) }
    val gap = 4.dp
    val gapPx = with(LocalDensity.current) { gap.toPx() }
    val offset by animateFloatAsState(
        selectorOffset(widths, gapPx, index, containerWidth),
        spring(stiffness = Spring.StiffnessMediumLow),
        label = "selector",
    )
    val value = stringResource(R.string.area_px2, MinPatchArea.at(index))
    Box(
        modifier
            .fillMaxWidth()
            .height(44.dp)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .onSizeChanged { containerWidth = it.width.toFloat() }
            .clipToBounds()
            // The row fades out towards both edges, as the camera's modes do.
            .graphicsLayer { compositingStrategy = CompositingStrategy.Offscreen }
            .drawWithContent {
                drawContent()
                drawRect(
                    Brush.horizontalGradient(
                        0f to Color.Transparent, 0.18f to Color.Black, 0.82f to Color.Black, 1f to Color.Transparent,
                    ),
                    blendMode = BlendMode.DstIn,
                )
            }
            .semantics {
                contentDescription = label
                stateDescription = value
                progressBarRangeInfo = ProgressBarRangeInfo(index.toFloat(), 0f..steps.lastIndex.toFloat(), steps.size - 2)
                if (enabled) {
                    setProgress { target ->
                        onIndexNow(target.roundToInt().coerceIn(0, steps.lastIndex))
                        true
                    }
                } else {
                    disabled()
                }
            }
            .pointerInput(enabled) {
                if (!enabled) return@pointerInput
                val step = 56.dp.toPx()
                var start = 0
                var dragged = 0f
                detectHorizontalDragGestures(
                    onDragStart = { start = indexNow; dragged = 0f },
                ) { change, amount ->
                    change.consume()
                    dragged += amount
                    val next = swipedIndex(start, dragged, step, steps.size)
                    if (next != indexNow) onIndexNow(next)
                }
            },
    ) {
        Row(
            Modifier
                .align(Alignment.CenterStart)
                .wrapContentWidth(Alignment.Start, unbounded = true)
                .offset { IntOffset(offset.roundToInt(), 0) },
            horizontalArrangement = Arrangement.spacedBy(gap),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            steps.forEachIndexed { i, area ->
                val selected = i == index
                Box(
                    Modifier
                        .onSizeChanged { widths[i] = it.width.toFloat() }
                        .clip(RoundedCornerShape(50))
                        .background(if (selected) ButtonGrey else Color.Transparent)
                        .clickable(enabled = enabled, role = Role.Button) { onIndexNow(i) }
                        .padding(horizontal = 14.dp, vertical = 8.dp),
                ) {
                    BasicText(
                        if (selected) value else area.toString(),
                        style = labelStyle(14f, if (selected) Color.White else MetaGrey)
                            .copy(fontWeight = if (selected) FontWeight.Medium else FontWeight.Normal),
                        softWrap = false,
                    )
                }
            }
        }
    }
}

/**
 * The camera's shutter: a white disc inside a white ring, which shrinks
 * while pressed. Dimmed and inert unless [enabled].
 */
@Composable
fun Shutter(enabled: Boolean, onShoot: () -> Unit, modifier: Modifier = Modifier) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val disc by animateFloatAsState(if (pressed) 0.84f else 1f, tween(120), label = "shutter")
    val shoot = stringResource(R.string.a11y_shoot)
    Box(
        modifier
            .size(76.dp)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .clip(CircleShape)
            .border(3.dp, Color.White, CircleShape)
            .clickable(interaction, indication = null, enabled = enabled, role = Role.Button, onClickLabel = shoot) { onShoot() }
            .semantics { contentDescription = shoot },
        contentAlignment = Alignment.Center,
    ) {
        Box(Modifier.size(62.dp).scale(disc).background(Color.White, CircleShape))
    }
}

/** A quarter of a light ring over a dim one, a turn every 0.9 s: a photo being processed. */
@Composable
private fun ProgressRing(modifier: Modifier = Modifier) {
    val turn = rememberInfiniteTransition(label = "ring")
    val angle by turn.animateFloat(
        0f, 360f,
        infiniteRepeatable(tween(900, easing = LinearEasing), RepeatMode.Restart),
        label = "angle",
    )
    Canvas(modifier.rotate(angle)) {
        val width = 3.dp.toPx()
        drawCircle(Color.White.copy(alpha = 0.25f), radius = (size.minDimension - width) / 2, style = Stroke(width))
        drawArc(
            Color.White, startAngle = -135f, sweepAngle = 90f, useCenter = false,
            topLeft = Offset(width / 2, width / 2), size = Size(size.width - width, size.height - width),
            style = Stroke(width, cap = StrokeCap.Round),
        )
    }
}

/**
 * The round gallery button left of the shutter, which opens the system photo
 * picker; its glyph turns by [angle]. While [busy] (a photo is processed) a
 * ring spins around it, as the camera's thumbnail does while it processes.
 * Dimmed and inert unless [enabled].
 */
@Composable
fun GalleryThumb(enabled: Boolean, busy: Boolean, angle: Float, onPick: () -> Unit, modifier: Modifier = Modifier) {
    val gallery = stringResource(R.string.a11y_gallery)
    Box(modifier.size(60.dp), contentAlignment = Alignment.Center) {
        if (busy) ProgressRing(Modifier.size(60.dp))
        Canvas(
            Modifier
                .size(50.dp)
                .alpha(if (enabled || busy) 1f else DISABLED_ALPHA)
                .clip(CircleShape)
                .clickable(enabled = enabled, role = Role.Button, onClickLabel = gallery) { onPick() }
                .background(ButtonGrey)
                .semantics { contentDescription = gallery }
                .rotate(angle),
        ) {
            val u = size.minDimension / 40
            val stroke = Stroke(width = 1.8f * u, cap = StrokeCap.Round)
            drawRoundRect(
                OnDark, Offset(12 * u, 12 * u), Size(16 * u, 16 * u), style = stroke,
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(2.5f * u),
            )
            val hills = Path().apply {
                moveTo(14 * u, 25 * u)
                lineTo(18 * u, 20 * u)
                lineTo(21 * u, 23 * u)
                lineTo(23 * u, 21 * u)
                lineTo(26 * u, 25 * u)
                close()
            }
            drawPath(hills, OnDark)
            drawCircle(OnDark, 1.6f * u, Offset(23.5f * u, 16.5f * u))
        }
    }
}

/**
 * The switch between the back and the front camera, two arrows chasing each
 * other in a grey circle, as in the stock camera app: the arrows turn half a
 * turn at each tap, on top of [angle]. Dimmed and inert unless [enabled].
 */
@Composable
fun SwitchCameraButton(enabled: Boolean, angle: Float, onSwitch: () -> Unit, modifier: Modifier = Modifier) {
    var turns by remember { mutableFloatStateOf(0f) }
    val spin by animateFloatAsState(turns * 180f, tween(360), label = "switch")
    val switchCamera = stringResource(R.string.a11y_switch_camera)
    Box(
        modifier
            .size(50.dp)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .clip(CircleShape)
            .background(ButtonGrey)
            .clickable(enabled = enabled, role = Role.Button, onClickLabel = switchCamera) {
                turns += 1
                onSwitch()
            }
            .semantics { contentDescription = switchCamera },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(26.dp).rotate(angle + spin)) {
            val u = size.minDimension / 24
            val r = 7 * u
            val c = Offset(size.width / 2, size.height / 2)
            val stroke = Stroke(width = 1.8f * u, cap = StrokeCap.Round)
            for (start in listOf(200f, 20f)) {
                drawArc(OnDark, start, 140f, false, c - Offset(r, r), Size(2 * r, 2 * r), style = stroke)
                // An arrowhead at the arc's end, pointing on clockwise.
                val a = Math.toRadians((start + 140f).toDouble())
                val radial = Offset(cos(a).toFloat(), sin(a).toFloat())
                val tangent = Offset(-radial.y, radial.x)
                val end = c + radial * r
                val head = Path().apply {
                    moveTo((end + tangent * 3f * u).x, (end + tangent * 3f * u).y)
                    lineTo((end + radial * 2.6f * u).x, (end + radial * 2.6f * u).y)
                    lineTo((end - radial * 2.6f * u).x, (end - radial * 2.6f * u).y)
                    close()
                }
                drawPath(head, OnDark)
            }
        }
    }
}

/**
 * The zoom chips of the stock camera app, in a translucent capsule at the
 * preview's bottom: one per preset ([zoomPresets]). The lit one
 * ([activePreset]) has a dark disc and reads the current [zoom] ("1×",
 * "1,4×", "1.4×" in English: [zoomLabel] in the UI's locale); the others
 * read just their ratio ("0,5", "2"). Each label turns
 * by [angle]. A tap sets the chip's ratio. Dimmed and inert unless [enabled].
 */
@Composable
fun ZoomChips(
    presets: List<Float>,
    zoom: Float,
    onZoom: (Float) -> Unit,
    angle: Float,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val active = activePreset(presets, zoom)
    val locale = LocalConfiguration.current.locales[0]
    Row(
        modifier
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .clip(RoundedCornerShape(50))
            .background(Color.Black.copy(alpha = 0.35f))
            .padding(4.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        for (i in presets.indices) {
            val lit = i == active
            val label = if (lit) zoomLabel(zoom, locale) else zoomLabel(presets[i], locale).removeSuffix("×")
            val chip = stringResource(R.string.a11y_zoom, zoomLabel(presets[i], locale))
            val current = stringResource(R.string.a11y_current_zoom, zoomLabel(zoom, locale))
            Box(
                Modifier
                    .size(40.dp)
                    .clip(CircleShape)
                    .clickable(enabled = enabled, role = Role.Button, onClickLabel = chip) { onZoom(presets[i]) }
                    .semantics {
                        contentDescription = chip
                        if (lit) stateDescription = current
                    },
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    Modifier
                        .size(if (lit) 38.dp else 28.dp)
                        .background(Color.Black.copy(alpha = if (lit) 0.55f else 0.3f), CircleShape)
                        .rotate(angle),
                    contentAlignment = Alignment.Center,
                ) {
                    BasicText(
                        label,
                        style = labelStyle(if (lit) 12.5f else 11f, Color.White)
                            .copy(fontWeight = if (lit) FontWeight.SemiBold else FontWeight.Medium),
                        softWrap = false,
                    )
                }
            }
        }
    }
}

/** A small dark pill. */
@Composable
fun Chip(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Row(
        modifier
            .clip(RoundedCornerShape(50))
            .background(ButtonGrey)
            .padding(horizontal = 10.dp, vertical = 5.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) { content() }
}

/**
 * The LIVE chip: a red dot and the preview fps, or an amber dot while the
 * camera starts or the preview is paused. With stats (a tap toggles them)
 * it adds the core's ms per Frame and the Tessel count: the debug stats
 * overlay of #59. A long-press shares the diagnostic report ([onLongPress],
 * #106), also while the camera starts or the preview is paused.
 */
@Composable
fun LiveChip(text: String, live: Boolean, onTap: () -> Unit, onLongPress: () -> Unit, modifier: Modifier = Modifier) {
    Chip(
        modifier.combinedClickable(
            onClickLabel = stringResource(R.string.a11y_statistics),
            onLongClickLabel = stringResource(R.string.a11y_report),
            onLongClick = onLongPress,
        ) { if (live) onTap() },
    ) {
        Box(Modifier.size(6.dp).background(if (live) Color(0xFFFF5252) else Color(0xFFFFC107), CircleShape))
        BasicText(text, style = labelStyle(11f, Color.White))
    }
}

/** The result screen's glyphs, drawn on a 24-unit grid like Material Symbols. */
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

/** A round icon button, 48 dp, its glyph turned by [angle]. */
@Composable
fun IconButton(glyph: Glyph, label: String, onClick: () -> Unit, modifier: Modifier = Modifier, angle: Float = 0f) {
    Box(
        modifier
            .size(48.dp)
            .clip(CircleShape)
            .clickable(role = Role.Button, onClickLabel = label) { onClick() }
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) { GlyphIcon(glyph, OnDark, Modifier.size(24.dp).rotate(angle)) }
}

/**
 * One of the result screen's actions, as in the Google Photos viewer: a
 * glyph over its label, turned together by [angle]. Unless [enabled] it is
 * dimmed and ignores taps ("Salvato").
 */
@Composable
fun BottomAction(
    text: String,
    glyph: Glyph,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    angle: Float = 0f,
    enabled: Boolean = true,
) {
    Box(
        modifier
            .width(88.dp)
            .height(64.dp)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .clip(RoundedCornerShape(16.dp))
            .clickable(enabled = enabled, role = Role.Button) { onClick() },
        contentAlignment = Alignment.Center,
    ) {
        Column(
            Modifier.rotate(angle),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            GlyphIcon(glyph, OnDark, Modifier.size(24.dp))
            BasicText(text, style = labelStyle(12f).copy(fontWeight = FontWeight.Medium), softWrap = false)
        }
    }
}
