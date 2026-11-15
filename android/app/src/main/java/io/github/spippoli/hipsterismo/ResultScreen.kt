package io.github.spippoli.hipsterismo

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * The result screen of variant A (#57): a photo's final Mosaic, fit to the
 * screen without cropping, under a top bar with back, the `min patch area` it
 * was made with and the overlay toggle; Scarta, Condividi and Salva below it
 * in portrait, in a column on the right in landscape. Scarta (and back) go
 * back to the camera without asking. Condividi and Salva come with #66 and do
 * nothing yet. [onFirstDraw] runs once, after the Mosaic's first draw.
 */
@Composable
fun ResultScreen(
    mosaic: Mosaic,
    minPatchArea: Int,
    overlay: Boolean,
    onOverlay: () -> Unit,
    onDiscard: () -> Unit,
    onFirstDraw: () -> Unit,
) {
    val drawnOnce = remember(mosaic) { booleanArrayOf(false) }
    val onDrawn = { _: Mosaic ->
        if (!drawnOnce[0]) {
            drawnOnce[0] = true
            onFirstDraw()
        }
    }
    val actions = @Composable { modifier: Modifier ->
        ActionButton(stringResource(R.string.discard), Glyph.DELETE, onDiscard, modifier)
        ActionButton(stringResource(R.string.share), Glyph.SHARE, {}, modifier)
        ActionButton(stringResource(R.string.save), Glyph.DOWNLOAD, {}, modifier, filled = true)
    }

    BoxWithConstraints(Modifier.fillMaxSize().background(PanelDark).windowInsetsPadding(WindowInsets.safeDrawing)) {
        val landscape = maxWidth > maxHeight
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier.fillMaxWidth().height(52.dp).padding(horizontal = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                IconButton(Glyph.BACK, stringResource(R.string.back), onDiscard)
                BasicText(stringResource(R.string.result_title), Modifier.weight(1f).padding(start = 6.dp), style = labelStyle(18f))
                BasicText(stringResource(R.string.area_px2, minPatchArea), Modifier.padding(end = 6.dp), style = labelStyle(11.5f, MetaGrey))
                OverlayButton(overlay, onOverlay)
            }
            val picture = @Composable { modifier: Modifier ->
                Box(modifier) {
                    MosaicCanvas(mosaic, Modifier.fillMaxSize(), crop = false, showContours = overlay, onDrawn = onDrawn)
                }
            }
            if (landscape) {
                Row(Modifier.fillMaxSize()) {
                    picture(Modifier.weight(1f).fillMaxHeight().padding(bottom = 10.dp))
                    Column(
                        Modifier.fillMaxHeight().padding(start = 8.dp, end = 12.dp, bottom = 16.dp).width(130.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterVertically),
                    ) { actions(Modifier.fillMaxWidth()) }
                }
            } else {
                picture(Modifier.weight(1f).fillMaxWidth())
                Row(
                    Modifier.fillMaxWidth().padding(top = 32.dp, bottom = 24.dp),
                    horizontalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterHorizontally),
                ) { actions(Modifier) }
            }
        }
    }
}
