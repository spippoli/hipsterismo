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

/**
 * Draws [mosaic] centred with its aspect ratio kept: covering the whole space
 * and cropped if [crop] (the live preview), or whole inside it. With
 * [showContours] its Contours are drawn over it. [onDrawn] runs after each
 * draw, for the fps meter.
 */
@Composable
fun MosaicCanvas(
    mosaic: Mosaic,
    modifier: Modifier = Modifier,
    crop: Boolean = false,
    showContours: Boolean = false,
    onDrawn: (Mosaic) -> Unit = {},
) {
    // drawVertices() modulates the vertex colours with the paint's, which
    // must be white to leave them as they are.
    val paint = remember { Paint().apply { color = android.graphics.Color.WHITE } }
    val contourPaint = remember { contourPaint() }
    Canvas(modifier) {
        val place = placement(mosaic.width, mosaic.height, size.width, size.height, crop)
        translate(place.dx, place.dy) {
            scale(place.scale, pivot = Offset.Zero) {
                drawIntoCanvas {
                    it.nativeCanvas.drawMosaic(mosaic, paint)
                    if (showContours) it.nativeCanvas.drawContours(mosaic, contourPaint)
                }
            }
        }
        onDrawn(mosaic)
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

/** The Contour overlay's paint: white lines, slightly see-through, with round ends. */
fun contourPaint() = Paint().apply {
    color = android.graphics.Color.argb(230, 255, 255, 255)
    style = Paint.Style.STROKE
    strokeCap = Paint.Cap.ROUND
    isAntiAlias = true
}

/** Draws [mosaic]'s Contours in Frame pixel coordinates, [overlayStrokeWidth] wide. */
fun Canvas.drawContours(mosaic: Mosaic, paint: Paint) {
    paint.strokeWidth = overlayStrokeWidth(mosaic.width, mosaic.height)
    drawLines(mosaic.contours, paint)
}
