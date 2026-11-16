package io.github.spippoli.hipsterismo

import android.graphics.Canvas
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.nativeCanvas

/**
 * Draws [mosaic] whole, centred inside the space with its aspect ratio kept
 * (the live preview and the result alike, never cropped). With [dim] it is darkened
 * and desaturated, the frozen preview while a photo is processed. [onDrawn]
 * runs after each draw, for the fps meter.
 */
@Composable
fun MosaicCanvas(
    mosaic: Mosaic,
    modifier: Modifier = Modifier,
    dim: Boolean = false,
    onDrawn: (Mosaic) -> Unit = {},
) {
    // drawVertices() modulates the vertex colours with the paint's, which
    // must be white to leave them as they are.
    val paint = remember { Paint().apply { color = android.graphics.Color.WHITE } }
    Canvas(modifier) {
        paint.colorFilter = if (dim) DIMMED else null
        val place = placement(mosaic.width, mosaic.height, size.width, size.height)
        translate(place.dx, place.dy) {
            scale(place.scale, pivot = Offset.Zero) {
                drawIntoCanvas { it.nativeCanvas.drawMosaic(mosaic, paint) }
            }
        }
        onDrawn(mosaic)
    }
}

/** The mock's frozen preview, CSS `brightness(.55) saturate(.6)`. */
private val DIMMED = ColorMatrixColorFilter(
    ColorMatrix().apply {
        setSaturation(0.6f)
        postConcat(ColorMatrix().apply { setScale(0.55f, 0.55f, 0.55f, 1f) })
    },
)

/** Draws [mosaic] in Frame pixel coordinates, one flat colour per Tessel. */
fun Canvas.drawMosaic(mosaic: Mosaic, paint: Paint) {
    drawVertices(
        Canvas.VertexMode.TRIANGLES,
        mosaic.vertices.size, mosaic.vertices, 0,
        null, 0,
        mosaic.colors, 0,
        null, 0, 0,
        paint,
    )
}
