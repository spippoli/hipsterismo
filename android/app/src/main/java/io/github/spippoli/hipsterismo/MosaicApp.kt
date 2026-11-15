package io.github.spippoli.hipsterismo

import android.content.Context
import android.graphics.ImageDecoder
import android.net.Uri
import android.os.SystemClock
import android.util.Log
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.view.PreviewView
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import java.io.File

/** Where the app is: the live camera, a photo being processed, or a photo's Mosaic. */
sealed interface PhotoState {
    data object Live : PhotoState

    /** A photo from [source] is being taken or processed; [id] tells one photo from the next. */
    data class Processing(val id: Long, val source: PhotoSource) : PhotoState

    /**
     * The final Mosaic of a photo, made at [minPatchArea] and never recomputed
     * (#57). [timing] is logged at its first draw on the result screen.
     */
    data class Result(val mosaic: Mosaic, val minPatchArea: Int, val timing: PendingTiming) : PhotoState
}

/** A photo's times so far, in ms; [startNanos] is the shutter tap or the picker's answer. */
data class PendingTiming(
    val source: PhotoSource,
    val startNanos: Long,
    val captureMs: Double,
    val decodeMs: Double,
) {
    /** The whole [PhotoTiming] of [mosaic], ending [nowNanos] (its first draw). */
    fun complete(mosaic: Mosaic, minPatchArea: Int, nowNanos: Long) = PhotoTiming(
        source, mosaic.width, mosaic.height, (nowNanos - startNanos) / 1e6,
        captureMs, decodeMs, mosaic.coreMs.toDouble(), mosaic.tesselCount, minPatchArea,
    )
}

/** The shutter's JPEG goes here, in the cache; it is deleted once decoded, and the folder at startup. */
fun photoCache(context: Context) = File(context.cacheDir, "photos")

/**
 * The app once the camera is granted: the camera screen, and the result
 * screen of a photo from the shutter or the gallery. The slider's step and
 * the overlay toggle are shared by both, and survive rotation.
 *
 * A photo pauses the preview ([PreviewPipeline.paused]), then the pipeline's
 * thread decodes it (capped at 2048, upright) and makes its Mosaic at the
 * slider's value of that moment. The result screen shows it until Scarta (or
 * back), which resumes the live preview.
 */
@Composable
fun MosaicApp(pipeline: PreviewPipeline, previewView: PreviewView) {
    val context = LocalContext.current
    var index by rememberSaveable { mutableIntStateOf(MinPatchArea.defaultIndex) }
    var overlay by rememberSaveable { mutableStateOf(false) }
    var state by remember { mutableStateOf<PhotoState>(PhotoState.Live) }
    val main = context.mainExecutor

    fun backToLive() {
        state = PhotoState.Live
        pipeline.paused = false
    }

    fun failed(id: Long, e: Exception) {
        Log.e(PHOTO_TAG, "photo failed", e)
        main.execute {
            if ((state as? PhotoState.Processing)?.id != id) return@execute
            Toast.makeText(context, R.string.photo_failed, Toast.LENGTH_SHORT).show()
            backToLive()
        }
    }

    // Decode and core on the pipeline's thread; the result screen on the UI thread.
    fun process(id: Long, minPatchArea: Int, timing: PendingTiming, source: () -> ImageDecoder.Source, cleanup: () -> Unit) {
        pipeline.processPhoto(
            source, minPatchArea,
            onDone = { mosaic, decodeMs ->
                cleanup()
                main.execute {
                    if ((state as? PhotoState.Processing)?.id == id) {
                        state = PhotoState.Result(mosaic, minPatchArea, timing.copy(decodeMs = decodeMs))
                    }
                }
            },
            onError = { cleanup(); failed(id, it) },
        )
    }

    fun begin(source: PhotoSource): Long {
        val id = SystemClock.elapsedRealtimeNanos()
        pipeline.paused = true
        state = PhotoState.Processing(id, source)
        return id
    }

    val onShoot = shoot@{
        if (state != PhotoState.Live || pipeline.mosaic.value == null) return@shoot
        val minPatchArea = MinPatchArea.at(index)
        val id = begin(PhotoSource.CAMERA)
        val dir = photoCache(context).apply { mkdirs() }
        val file = File(dir, "shot-$id.jpg")
        pipeline.takePhoto(
            file,
            onSaved = {
                val captureMs = (SystemClock.elapsedRealtimeNanos() - id) / 1e6
                process(id, minPatchArea, PendingTiming(PhotoSource.CAMERA, id, captureMs, 0.0), { ImageDecoder.createSource(file) }) {
                    file.delete()
                }
            },
            onError = { file.delete(); failed(id, it) },
        )
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri == null || state != PhotoState.Live) return@rememberLauncherForActivityResult
        val minPatchArea = MinPatchArea.at(index)
        val id = begin(PhotoSource.GALLERY)
        process(id, minPatchArea, PendingTiming(PhotoSource.GALLERY, id, 0.0, 0.0), {
            ImageDecoder.createSource(context.contentResolver, uri)
        }) {}
    }
    val onPick = {
        if (state == PhotoState.Live) {
            picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
        }
    }

    // Back discards a result like Scarta; while a photo is processed it waits.
    BackHandler(enabled = state != PhotoState.Live) {
        if (state is PhotoState.Result) backToLive()
    }

    when (val now = state) {
        is PhotoState.Result -> ResultScreen(
            now.mosaic, now.minPatchArea, overlay, { overlay = !overlay },
            onDiscard = ::backToLive,
            onFirstDraw = { Log.i(PHOTO_TAG, now.timing.complete(now.mosaic, now.minPatchArea, SystemClock.elapsedRealtimeNanos()).line()) },
        )
        else -> CameraScreen(
            pipeline, previewView, index, { index = it }, overlay, { overlay = !overlay },
            busy = now is PhotoState.Processing, onShoot = onShoot, onPick = onPick,
        )
    }
}
