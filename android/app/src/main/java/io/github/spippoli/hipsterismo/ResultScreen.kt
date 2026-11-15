package io.github.spippoli.hipsterismo

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.shape.RoundedCornerShape
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
 * The result screen of variant A (#57): a photo's final Mosaic, fit to the
 * screen without cropping, under a top bar with back, the `min patch area` it
 * was made with and the overlay toggle; Scarta, Condividi and Salva below it
 * in portrait, in a column on the right in landscape. Scarta (and back) go
 * back to the camera without asking. Once [saved], Salva is a disabled
 * "Salvato" (#58). While [busy] (a PNG being made) Condividi and Salva ignore
 * taps. With [snackbar] the mock's snackbar "Salvato in Immagini/Hipsterismo"
 * shows at the bottom, its Apri calling [onOpen]. [onFirstDraw] runs once,
 * after the Mosaic's first draw.
 */
@Composable
fun ResultScreen(
    mosaic: Mosaic,
    minPatchArea: Int,
    overlay: Boolean,
    onOverlay: () -> Unit,
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
    val actions = @Composable { modifier: Modifier ->
        ActionButton(stringResource(R.string.discard), Glyph.DELETE, onDiscard, modifier)
        ActionButton(stringResource(R.string.share), Glyph.SHARE, { if (!busy) onShare() }, modifier)
        if (saved) {
            ActionButton(stringResource(R.string.saved), Glyph.CHECK, {}, modifier, filled = true, enabled = false)
        } else {
            ActionButton(stringResource(R.string.save), Glyph.DOWNLOAD, { if (!busy) onSave() }, modifier, filled = true)
        }
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
        if (snackbar) {
            Snackbar(
                stringResource(R.string.saved_in), stringResource(R.string.open), onOpen,
                Modifier.align(Alignment.BottomCenter)
                    .padding(bottom = 18.dp)
                    .then(if (landscape) Modifier.fillMaxWidth(0.5f) else Modifier.fillMaxWidth().padding(horizontal = 12.dp)),
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
