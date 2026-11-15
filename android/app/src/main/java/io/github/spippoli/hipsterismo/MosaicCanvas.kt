package io.github.spippoli.hipsterismo

import android.graphics.Canvas
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
import kotlin.math.min

/** Draws [mosaic] fit to the available space, its aspect ratio kept and centred. */
@Composable
fun MosaicCanvas(mosaic: Mosaic, modifier: Modifier = Modifier) {
    // drawVertices() modulates the vertex colours with the paint's, which
    // must be white to leave them as they are.
    val paint = remember { Paint().apply { color = android.graphics.Color.WHITE } }
    Canvas(modifier) {
        val fit = min(size.width / mosaic.width, size.height / mosaic.height)
        translate((size.width - mosaic.width * fit) / 2, (size.height - mosaic.height * fit) / 2) {
            scale(fit, pivot = Offset.Zero) {
                drawIntoCanvas { it.nativeCanvas.drawMosaic(mosaic, paint) }
            }
        }
    }
}

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
