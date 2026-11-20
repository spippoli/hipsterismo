package io.github.spippoli.hipsterismo

import androidx.compose.foundation.background
import kotlin.math.roundToInt
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.clickable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/**
 * The result screen, laid out like the Google Photos viewer the stock camera
 * opens: a photo's final Mosaic on black, fit without cropping, under a top
 * bar with back and the `min patch area` it was made with, and Condividi,
 * Salva and Scarta as glyphs over labels at the bottom. The screen stays in
 * portrait. The Mosaic stays turned by [mosaicAngle], how the phone was held
 * when the photo began, whatever the phone does afterwards; the glyphs turn
 * with the phone ([rememberIconAngle]). A pinch, a drag or a double tap magnifies the view
 * only (#126, [magnified], [doubleTapped]). Scarta (and back) go
 * back to the camera without asking. Once [saved], Salva is a disabled
 * "Salvato" (#58). While [busy] (a PNG being made) Condividi and Salva ignore
 * taps. With [snackbar] the mock's snackbar "Salvato in Immagini/Hipsterismo"
 * shows above the actions, its Apri calling [onOpen]. [onFirstDraw] runs once,
 * after the Mosaic's first draw.
 */
@Composable
fun ResultScreen(
    mosaic: Mosaic,
    minPatchArea: Int,
    mosaicAngle: Float = 0f,
    onDiscard: () -> Unit,
    onFirstDraw: () -> Unit,
    saved: Boolean = false,
    busy: Boolean = false,
    onShare: () -> Unit = {},
    onSave: () -> Unit = {},
    snackbar: Boolean = false,
    onOpen: () -> Unit = {},
) {
    val drawnOnce = remember(mosaic) { booleanArrayOf(false) }
    val onDrawn = { _: Mosaic ->
        if (!drawnOnce[0]) {
            drawnOnce[0] = true
            onFirstDraw()
        }
    }
    val angle by rememberIconAngle()

    Box(Modifier.fillMaxSize().background(PanelDark).windowInsetsPadding(WindowInsets.safeDrawing)) {
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier.fillMaxWidth().height(56.dp).padding(horizontal = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                IconButton(Glyph.BACK, stringResource(R.string.back), onDiscard, angle = angle)
                Spacer(Modifier.weight(1f))
                BasicText(stringResource(R.string.area_px2, minPatchArea), Modifier.padding(end = 16.dp), style = labelStyle(12f, MetaGrey))
            }
            // The Mosaic stays turned as the phone was held when the photo
            // began (a landscape photo reads upright and takes the long
            // side); only the icons follow the phone afterwards.
            BoxWithConstraints(Modifier.weight(1f).fillMaxWidth().clipToBounds(), contentAlignment = Alignment.Center) {
                val sideways = (mosaicAngle / 90f).roundToInt() % 2 != 0
                // Only the view is magnified (#126): Save and Share still take
                // the whole Mosaic.
                var magnification by remember(mosaic) { mutableStateOf(Magnification.FIT) }
                MosaicCanvas(
                    mosaic,
                    Modifier
                        .requiredSize(if (sideways) maxHeight else maxWidth, if (sideways) maxWidth else maxHeight)
                        .rotate(mosaicAngle)
                        // After the rotation, so the gestures come in the
                        // turned frame and a drag follows the finger.
                        .pointerInput(mosaic, mosaicAngle) {
                            detectTapGestures(onDoubleTap = { tap ->
                                magnification = doubleTapped(
                                    magnification, mosaic.width, mosaic.height,
                                    size.width.toFloat(), size.height.toFloat(), tap.x, tap.y,
                                )
                            })
                        }
                        .pointerInput(mosaic, mosaicAngle) {
                            detectTransformGestures { centroid, pan, zoom, _ ->
                                magnification = magnified(
                                    magnification, mosaic.width, mosaic.height,
                                    size.width.toFloat(), size.height.toFloat(),
                                    centroid.x, centroid.y, zoom, pan.x, pan.y,
                                )
                            }
                        },
                    magnification = magnification,
                    onDrawn = onDrawn,
                )
            }
            Row(
                Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 12.dp),
                horizontalArrangement = Arrangement.SpaceEvenly,
            ) {
                BottomAction(stringResource(R.string.share), Glyph.SHARE, { if (!busy) onShare() }, angle = angle)
                if (saved) {
                    BottomAction(stringResource(R.string.saved), Glyph.CHECK, {}, angle = angle, enabled = false)
                } else {
                    BottomAction(stringResource(R.string.save), Glyph.DOWNLOAD, { if (!busy) onSave() }, angle = angle)
                }
                BottomAction(stringResource(R.string.discard), Glyph.DELETE, onDiscard, angle = angle)
            }
        }
        if (snackbar) {
            Snackbar(
                stringResource(R.string.saved_in), stringResource(R.string.open), onOpen,
                Modifier.align(Alignment.BottomCenter)
                    .padding(start = 12.dp, end = 12.dp, bottom = 96.dp)
                    .fillMaxWidth(),
            )
        }
    }
}

/**
 * The mock's `.toast`: [text] on a dark grey card with rounded corners, and
 * [action] in light blue at its end. Full width less 12 dp each side in
 * portrait, the middle half in landscape.
 */
@Composable
fun Snackbar(text: String, action: String, onAction: () -> Unit, modifier: Modifier = Modifier) {
    val shape = RoundedCornerShape(6.dp)
    Row(
        modifier
            .shadow(6.dp, shape)
            .background(Color(0xFF313033), shape)
            .semantics { liveRegion = LiveRegionMode.Polite }
            .padding(start = 14.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        BasicText(text, Modifier.weight(1f).padding(vertical = 12.dp), style = labelStyle(12.5f, Color(0xFFF4EFF4)))
        BasicText(
            action,
            Modifier
                .clip(RoundedCornerShape(4.dp))
                .clickable(role = Role.Button) { onAction() }
                .padding(horizontal = 10.dp, vertical = 12.dp),
            style = labelStyle(12.5f, Color(0xFF9AD1FF)).copy(fontWeight = FontWeight.Medium),
        )
    }
}
