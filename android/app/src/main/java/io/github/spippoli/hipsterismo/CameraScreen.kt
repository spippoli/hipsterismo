package io.github.spippoli.hipsterismo

import android.os.SystemClock
import android.util.Log
import androidx.camera.view.PreviewView
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay

/**
 * The camera screen, variant A "Fotocamera" (#57): the live Mosaic full
 * screen, cropped to fill, with the LIVE chip and the controls over it. In
 * portrait the slider and the gallery / shutter / overlay row sit in a bottom
 * panel; in landscape the slider turns vertical on the left and the buttons
 * move to a column on the right.
 */
@Composable
fun CameraScreen(pipeline: PreviewPipeline, previewView: PreviewView) {
    var index by rememberSaveable { mutableIntStateOf(MinPatchArea.defaultIndex) }
    var overlay by rememberSaveable { mutableStateOf(false) }
    var stats by rememberSaveable { mutableStateOf(false) }
    val mosaic by pipeline.mosaic.collectAsState()
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
            if (++tick % 4 == 0 && m != null && fps > 0) {
                Log.i(
                    PreviewPipeline.TAG,
                    "preview %dx%d: %.1f fps, core %.1f ms, work %.1f ms, %d Tessels, min patch area %d".format(
                        m.width, m.height, fps, coreMs, pipeline.workMs.mean(now), m.tesselCount, pipeline.minPatchArea,
                    ),
                )
            }
        }
    }

    val live = mosaic != null
    val chipText = when {
        !live -> stringResource(R.string.starting)
        stats -> stringResource(R.string.live_stats, fps, coreMs, mosaic?.tesselCount ?: 0)
        else -> stringResource(R.string.live_fps, fps)
    }
    val areaText = stringResource(R.string.area_px2, minPatchArea)
    val sliderLabel = stringResource(R.string.min_patch_area)

    val previewLayer = @Composable { modifier: Modifier ->
        Box(modifier) {
            // The raw camera until the first Mosaic arrives; then the Mosaic
            // covers it (same aspect ratio, both cropped to fill).
            AndroidView(factory = { previewView }, modifier = Modifier.fillMaxSize())
            mosaic?.let {
                MosaicCanvas(it, Modifier.fillMaxSize().background(Color.Black), crop = true, showContours = overlay, onDrawn = onDrawn)
            }
        }
    }

    BoxWithConstraints(Modifier.fillMaxSize().background(PanelDark)) {
        if (maxWidth > maxHeight) {
            Row(Modifier.fillMaxSize()) {
                Box(Modifier.weight(1f).fillMaxHeight()) {
                    previewLayer(Modifier.fillMaxSize())
                    val start = Modifier.windowInsetsPadding(
                        WindowInsets.safeDrawing.only(WindowInsetsSides.Start + WindowInsetsSides.Vertical),
                    )
                    LiveChip(chipText, live, { stats = !stats }, start.align(Alignment.TopStart).padding(start = 46.dp, top = 12.dp))
                    Column(
                        start.align(Alignment.CenterStart).fillMaxHeight().padding(start = 4.dp, top = 44.dp, bottom = 30.dp).width(42.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(6.dp),
                    ) {
                        BasicText(stringResource(R.string.coarse), style = labelStyle(10f), softWrap = false)
                        StepSlider(index, MinPatchArea.steps.size, { index = it }, sliderLabel, areaText, Modifier.weight(1f).fillMaxWidth(), vertical = true)
                        BasicText(stringResource(R.string.fine), style = labelStyle(10f), softWrap = false)
                    }
                    Chip(start.align(Alignment.BottomStart).padding(start = 46.dp, bottom = 14.dp)) {
                        BasicText(areaText, style = labelStyle(10.5f, Color.White))
                    }
                }
                Column(
                    Modifier.fillMaxHeight()
                        .background(PanelDark)
                        .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.End + WindowInsetsSides.Vertical))
                        .width(110.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.SpaceEvenly,
                ) {
                    OverlayButton(overlay, { overlay = !overlay })
                    Shutter()
                    GalleryThumb()
                }
            }
        } else {
            previewLayer(Modifier.fillMaxSize())
            LiveChip(
                chipText, live, { stats = !stats },
                Modifier.align(Alignment.TopStart)
                    .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal))
                    .padding(start = 14.dp, top = 8.dp),
            )
            Column(
                Modifier.align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .background(Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = 0.75f))))
                    .windowInsetsPadding(WindowInsets.navigationBars)
                    .padding(start = 18.dp, end = 18.dp, top = 40.dp, bottom = 26.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    TextureIcon()
                    StepSlider(index, MinPatchArea.steps.size, { index = it }, sliderLabel, areaText, Modifier.weight(1f).height(28.dp))
                    BasicText(areaText, Modifier.widthIn(min = 58.dp), style = labelStyle().copy(textAlign = TextAlign.End))
                }
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    GalleryThumb()
                    Shutter()
                    OverlayButton(overlay, { overlay = !overlay })
                }
            }
        }
    }
}

/**
 * Shown instead of the camera while the CAMERA permission is denied: why the
 * app needs it, and a button that asks again, or opens the app's settings
 * once Android no longer shows the request ([canAsk] false).
 */
@Composable
fun PermissionScreen(canAsk: Boolean, onAsk: () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(PanelDark).windowInsetsPadding(WindowInsets.safeDrawing).padding(32.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(16.dp)) {
            BasicText(stringResource(R.string.permission_title), style = labelStyle(20f).copy(textAlign = TextAlign.Center))
            BasicText(stringResource(R.string.permission_body), style = labelStyle(14f, OnDark.copy(alpha = 0.8f)).copy(textAlign = TextAlign.Center))
            Box(
                Modifier.padding(top = 8.dp)
                    .background(Color.White, RoundedCornerShape(50))
                    .clickable { onAsk() }
                    .padding(horizontal = 20.dp, vertical = 10.dp),
            ) {
                BasicText(
                    stringResource(if (canAsk) R.string.permission_ask else R.string.permission_settings),
                    style = labelStyle(14f, Color(0xFF111111)),
                )
            }
        }
    }
}
