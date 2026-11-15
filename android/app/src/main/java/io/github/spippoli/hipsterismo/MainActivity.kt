package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** The skeleton's only screen: the adaptive Mosaic of the bundled fingers.png. */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            var mosaic by remember { mutableStateOf<Mosaic?>(null) }
            LaunchedEffect(Unit) {
                mosaic = withContext(Dispatchers.Default) { fixtureMosaic(FRAME) }
            }
            Box(Modifier.fillMaxSize().background(Color.Black)) {
                mosaic?.let { MosaicCanvas(it, Modifier.fillMaxSize()) }
            }
        }
    }

    private fun fixtureMosaic(name: String): Mosaic {
        val options = BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val frame = assets.open(name).use { BitmapFactory.decodeStream(it, null, options) }
            ?: error("cannot decode $name")
        val start = SystemClock.elapsedRealtimeNanos()
        val mosaic = NativeMosaic.fromBitmap(frame)
        val ms = (SystemClock.elapsedRealtimeNanos() - start) / 1e6
        Log.i(
            TAG,
            "%s %dx%d: %d Tessels, hash 0x%016x, %.1f ms".format(
                name, mosaic.width, mosaic.height, mosaic.tesselCount, mosaic.hash, ms,
            ),
        )
        frame.recycle()
        return mosaic
    }

    private companion object {
        const val TAG = "hipsterismo"
        const val FRAME = "fingers.png"
    }
}
