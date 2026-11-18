package io.github.spippoli.hipsterismo

import android.os.SystemClock
import android.util.Log
import androidx.camera.view.PreviewView
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay

/**
 * The camera screen, laid out like the stock Pixel camera app: a top bar
 * with the LIVE chip, the live Mosaic in a 3:4 viewfinder under it (whole,
 * not cropped) with the zoom chips at its bottom, then on black the `min
 * patch area` row where the camera has its modes, and the gallery button,
 * the shutter and the camera switch. The screen stays in portrait; held in
 * landscape, only the icons and the zoom labels turn ([rememberIconAngle]).
 *
 * The zoom chips jump to the ultrawide, 1× or 2×, and a pinch on the
 * viewfinder zooms freely.
 *
 * The row's step [index] belongs to the caller. While [busy] (a photo is being
 * processed) the last Mosaic stays frozen and dimmed, the chip reads "in
 * pausa", a ring spins around the gallery button and every control is
 * disabled, the pinch included.
 * [onShoot] and [onPick] are the shutter and the gallery thumbnail.
 */
@Composable
fun CameraScreen(
    pipeline: PreviewPipeline,
    previewView: PreviewView,
    index: Int,
    onIndex: (Int) -> Unit,
    busy: Boolean,
    onShoot: () -> Unit,
    onPick: () -> Unit,
) {
    var stats by rememberSaveable { mutableStateOf(false) }
    val mosaic by pipeline.mosaic.collectAsState()
    val camera by pipeline.camera.collectAsState()
    val busyNow by rememberUpdatedState(busy)
    val presets = zoomPresets(camera.minZoom, camera.maxZoom)
    val canZoom = camera.maxZoom > camera.minZoom
    val minPatchArea = MinPatchArea.at(index)
    SideEffect { pipeline.minPatchArea = minPatchArea }

    // fps: Mosaics drawn in the last second, counted once each.
    val drawn = remember { RollingWindow() }
    val lastDrawn = remember { arrayOfNulls<Mosaic>(1) }
    val onDrawn = { m: Mosaic ->
        if (m !== lastDrawn[0]) {
            lastDrawn[0] = m
            drawn.add(SystemClock.elapsedRealtimeNanos())
        }
    }
    var fps by remember { mutableDoubleStateOf(0.0) }
    var coreMs by remember { mutableDoubleStateOf(Double.NaN) }
    LaunchedEffect(pipeline) {
        var tick = 0
        while (true) {
            delay(250)
            val now = SystemClock.elapsedRealtimeNanos()
            fps = drawn.rate(now)
            coreMs = pipeline.coreMs.mean(now)
            val m = lastDrawn[0]
            if (++tick % 4 == 0 && m != null && fps > 0 && !pipeline.paused) {
                Log.i(
                    PreviewPipeline.TAG,
                    "preview %dx%d: %.1f fps, core %.1f ms, work %.1f ms, %d Tessels, min patch area %d".format(
                        java.util.Locale.ROOT,
                        m.width, m.height, fps, coreMs, pipeline.workMs.mean(now), m.tesselCount, pipeline.minPatchArea,
                    ),
                )
            }
        }
    }

    val live = mosaic != null && !busy
    val chipText = when {
        busy -> stringResource(R.string.paused)
        mosaic == null -> stringResource(R.string.starting)
        stats -> stringResource(R.string.live_stats, fps, coreMs, mosaic?.tesselCount ?: 0)
        else -> stringResource(R.string.live_fps, fps)
    }
    val angle by rememberIconAngle()

    BoxWithConstraints(
        Modifier.fillMaxSize().background(PanelDark).windowInsetsPadding(WindowInsets.safeDrawing),
    ) {
        // The 3:4 viewfinder under the top bar, as wide as the screen; on a
        // short screen it gives way so the controls keep their room.
        val previewHeight = minOf(maxWidth * 4 / 3, maxHeight - TOP_BAR - MIN_CONTROLS)
        Column(Modifier.fillMaxSize()) {
            Box(Modifier.fillMaxWidth().height(TOP_BAR).padding(horizontal = 16.dp), contentAlignment = Alignment.CenterStart) {
                LiveChip(chipText, live, { stats = !stats })
            }
            Box(
                Modifier.fillMaxWidth().height(previewHeight).pointerInput(pipeline) {
                    detectTransformGestures { _, _, zoom, _ ->
                        if (!busyNow && zoom != 1f) pipeline.pinch(zoom)
                    }
                },
            ) {
                // The raw camera until the first Mosaic arrives; then the Mosaic
                // covers it (same aspect ratio, both fit whole inside the view).
                AndroidView(factory = { previewView }, modifier = Modifier.fillMaxSize())
                mosaic?.let {
                    MosaicCanvas(
                        it, Modifier.fillMaxSize().background(Color.Black),
                        dim = busy, onDrawn = onDrawn,
                    )
                }
                if (canZoom) {
                    ZoomChips(
                        presets, camera.zoom, pipeline::setZoom, angle,
                        Modifier.align(Alignment.BottomCenter).padding(bottom = 16.dp), enabled = !busy,
                    )
                }
            }
            Column(
                Modifier.fillMaxWidth().weight(1f).padding(bottom = 8.dp),
                verticalArrangement = Arrangement.SpaceEvenly,
            ) {
                PatchAreaSelector(index, onIndex, stringResource(R.string.min_patch_area), enabled = !busy)
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 28.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(Modifier.size(SIDE_BUTTON), contentAlignment = Alignment.Center) {
                        GalleryThumb(!busy, busy, angle, onPick)
                    }
                    Shutter(!busy, onShoot)
                    // Without a second camera, the empty box keeps the shutter centred.
                    Box(Modifier.size(SIDE_BUTTON), contentAlignment = Alignment.Center) {
                        if (camera.canSwitch) SwitchCameraButton(!busy, angle, pipeline::switchCamera)
                    }
                }
            }
        }
    }
}

private val TOP_BAR = 56.dp
private val MIN_CONTROLS = 170.dp
private val SIDE_BUTTON = 60.dp

/**
 * Shown instead of the camera while the CAMERA permission is denied: why the
 * app needs it, and a button that asks again, or opens the app's settings
 * once Android no longer shows the request ([canAsk] false).
 */
@Composable
fun PermissionScreen(canAsk: Boolean, onAsk: () -> Unit) {
    MessageScreen(
        stringResource(R.string.permission_title),
        stringResource(R.string.permission_body),
        stringResource(if (canAsk) R.string.permission_ask else R.string.permission_settings),
        onAsk,
    )
}

/**
 * Shown instead of the camera screen while the camera can't be opened or has
 * dropped out ([CameraStatus.Failed], #104): "Couldn't open the camera" and
 * Retry, which binds again. The gallery button stays where the camera screen
 * has it and works, since a gallery photo needs no camera; while [busy] (that
 * photo is processed) its ring spins and Retry waits.
 */
@Composable
fun CameraErrorScreen(busy: Boolean, onRetry: () -> Unit, onPick: () -> Unit) {
    val angle by rememberIconAngle()
    Box(Modifier.fillMaxSize()) {
        MessageScreen(stringResource(R.string.camera_error), null, stringResource(R.string.retry), { if (!busy) onRetry() })
        Box(
            Modifier.align(Alignment.BottomStart).windowInsetsPadding(WindowInsets.safeDrawing)
                .padding(start = 28.dp, bottom = 40.dp).size(SIDE_BUTTON),
            contentAlignment = Alignment.Center,
        ) {
            GalleryThumb(!busy, busy, angle, onPick)
        }
    }
}

// A title, an optional body and one white pill button, centred on the dark panel.
@Composable
private fun MessageScreen(title: String, body: String?, button: String, onClick: () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(PanelDark).windowInsetsPadding(WindowInsets.safeDrawing).padding(32.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(16.dp)) {
            BasicText(title, style = labelStyle(20f).copy(textAlign = TextAlign.Center))
            if (body != null) {
                BasicText(body, style = labelStyle(14f, OnDark.copy(alpha = 0.8f)).copy(textAlign = TextAlign.Center))
            }
            Box(
                Modifier.padding(top = 8.dp)
                    .background(Color.White, RoundedCornerShape(50))
                    .clickable { onClick() }
                    .padding(horizontal = 20.dp, vertical = 10.dp),
            ) {
                BasicText(button, style = labelStyle(14f, Color(0xFF111111)))
            }
        }
    }
}
