package io.github.spippoli.hipsterismo

import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PerformanceHintManager
import android.os.Process
import android.os.SystemClock
import android.util.Log
import android.util.Size
import android.view.Display
import android.graphics.ImageDecoder
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * The live preview and the photos: CameraX binds Preview (the raw camera,
 * shown until the first Mosaic), ImageAnalysis (RGBA, 320×240, only the
 * latest Frame kept) and ImageCapture (the shutter's full-resolution JPEG).
 * One thread turns each analysis Frame into a Mosaic, rotated upright and
 * mirrored for the front camera, with an ADPF hint session asking for a fast
 * core. The latest Mosaic is [mosaic]; [coreMs] holds the core's times of the
 * last second.
 *
 * A photo ([takePhoto], [processPhoto]) runs on the same thread, with the
 * preview [paused]: the thread whose hint keeps it on the X3 does the photo's
 * decode and core, and no analysis Frame competes with it.
 */
class PreviewPipeline(private val context: Context) {
    private val _mosaic = MutableStateFlow<Mosaic?>(null)
    val mosaic: StateFlow<Mosaic?> = _mosaic

    /** `makeAdaptive()`'s ms per Frame over the last second. */
    val coreMs = RollingWindow()

    /** The whole per-Frame work (copy, core, arrays) in ms over the last second. */
    val workMs = RollingWindow()

    /** The `min patch area` of the next Frame; set from the UI thread. */
    @Volatile
    var minPatchArea: Int = MinPatchArea.DEFAULT

    /**
     * While true, analysis Frames are dropped unprocessed and [mosaic] keeps
     * the last one: during a photo and on the result screen. Set from the UI
     * thread.
     */
    @Volatile
    var paused: Boolean = false

    private val executor: ExecutorService = Executors.newSingleThreadExecutor { runnable ->
        Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY)
            runnable.run()
        }, "mosaic-preview")
    }
    private var hint: Any? = null // PerformanceHintManager.Session, API 31+
    private var hintTried = false
    private var loggedFrame = false

    private val analysis = ImageAnalysis.Builder()
        .setResolutionSelector(
            ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                .setResolutionStrategy(
                    ResolutionStrategy(ANALYSIS_SIZE, ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER),
                )
                .build(),
        )
        .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
        .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
        .build()

    private val preview = Preview.Builder()
        .setResolutionSelector(
            ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                .build(),
        )
        .build()

    // The shutter's photo: the largest 4:3 size (CameraX's default for
    // ImageCapture), latency over quality, saved as a JPEG with its EXIF
    // orientation.
    private val capture = ImageCapture.Builder()
        .setCaptureMode(ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY)
        .setResolutionSelector(
            ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                .build(),
        )
        .build()

    private var mirror = false
    private var provider: ProcessCameraProvider? = null

    // The rotation the Frames are made upright for follows the display, also
    // across a 180° turn, which recreates nothing.
    private val displayManager = context.getSystemService(DisplayManager::class.java)
    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) {}
        override fun onDisplayRemoved(displayId: Int) {}
        override fun onDisplayChanged(displayId: Int) {
            if (displayId == Display.DEFAULT_DISPLAY) updateRotation()
        }
    }

    /** Starts the camera for [owner]'s lifecycle, showing the raw preview through [surfaceProvider]. */
    fun start(owner: LifecycleOwner, surfaceProvider: Preview.SurfaceProvider) {
        preview.surfaceProvider = surfaceProvider
        updateRotation()
        displayManager.registerDisplayListener(displayListener, Handler(Looper.getMainLooper()))
        analysis.setAnalyzer(executor, ::analyze)
        val future = ProcessCameraProvider.getInstance(context)
        future.addListener({
            val cameras = future.get()
            provider = cameras
            val selector = when {
                cameras.hasCamera(CameraSelector.DEFAULT_BACK_CAMERA) -> CameraSelector.DEFAULT_BACK_CAMERA
                else -> CameraSelector.DEFAULT_FRONT_CAMERA
            }
            mirror = selector == CameraSelector.DEFAULT_FRONT_CAMERA
            cameras.unbindAll()
            cameras.bindToLifecycle(owner, selector, preview, analysis, capture)
        }, context.mainExecutor)
    }

    /** Stops the camera and the preview thread. */
    fun stop() {
        displayManager.unregisterDisplayListener(displayListener)
        provider?.unbindAll()
        analysis.clearAnalyzer()
        executor.execute {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) (hint as? PerformanceHintManager.Session)?.close()
        }
        executor.shutdown()
    }

    private fun updateRotation() {
        val rotation = displayManager.getDisplay(Display.DEFAULT_DISPLAY)?.rotation ?: return
        analysis.targetRotation = rotation
        preview.targetRotation = rotation
        capture.targetRotation = rotation
    }

    /**
     * Takes a photo into [file] (a JPEG, upright through its EXIF
     * orientation), then calls [onSaved] or [onError] on the pipeline's
     * thread.
     */
    fun takePhoto(file: File, onSaved: () -> Unit, onError: (Exception) -> Unit) {
        val options = ImageCapture.OutputFileOptions.Builder(file).build()
        capture.takePicture(options, executor, object : ImageCapture.OnImageSavedCallback {
            override fun onImageSaved(output: ImageCapture.OutputFileResults) = onSaved()
            override fun onError(exception: ImageCaptureException) = onError(exception)
        })
    }

    /**
     * Decodes [source] ([PhotoDecoder]: upright, capped at 2048) and makes its
     * Mosaic at [minPatchArea], scaled to the photo's size by the core, on the
     * pipeline's thread. [onDone] gets the Mosaic and the decode time in ms,
     * [onError] what went wrong; both run on that thread.
     */
    fun processPhoto(
        source: () -> ImageDecoder.Source,
        minPatchArea: Int,
        onDone: (mosaic: Mosaic, decodeMs: Double) -> Unit,
        onError: (Exception) -> Unit,
    ) = post {
        try {
            val start = System.nanoTime()
            val bitmap = PhotoDecoder.decode(source())
            val decodeMs = (System.nanoTime() - start) / 1e6
            val mosaic = try {
                NativeMosaic.fromBitmap(bitmap, minPatchArea)
            } finally {
                bitmap.recycle()
            }
            onDone(mosaic, decodeMs)
        } catch (e: Exception) {
            onError(e)
        }
    }

    /** Runs [task] on the pipeline's thread, unless the pipeline has stopped. */
    fun post(task: () -> Unit) {
        if (!executor.isShutdown) executor.execute(task)
    }

    private fun analyze(image: ImageProxy) {
        image.use {
            if (paused) return
            val start = System.nanoTime()
            val plane = image.planes[0]
            val transform = FrameTransform.of(
                image.width, image.height, plane.rowStride, plane.pixelStride,
                image.imageInfo.rotationDegrees, mirror,
            )
            val minPatchArea = minPatchArea
            val mosaic = NativeMosaic.fromFrame(plane.buffer, transform, minPatchArea)
            val end = System.nanoTime()
            val now = SystemClock.elapsedRealtimeNanos()
            coreMs.add(now, mosaic.coreMs.toDouble())
            workMs.add(now, (end - start) / 1e6)
            reportWork(end - start)
            if (!loggedFrame) {
                loggedFrame = true
                Log.i(
                    TAG,
                    "analysis %dx%d, row stride %d, pixel stride %d, rotation %d, mirror %b -> Frame %dx%d".format(
                        image.width, image.height, plane.rowStride, plane.pixelStride,
                        image.imageInfo.rotationDegrees, mirror, transform.width, transform.height,
                    ),
                )
            }
            _mosaic.value = mosaic
        }
    }

    // ADPF: a hint session for this thread, told the target and each Frame's
    // actual work time, so the scheduler keeps it on a core fast enough.
    private fun reportWork(nanos: Long) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return
        if (!hintTried) {
            hintTried = true
            val manager = context.getSystemService(PerformanceHintManager::class.java)
            hint = manager?.createHintSession(intArrayOf(Process.myTid()), TARGET_WORK_NANOS)
            Log.i(TAG, if (hint != null) "ADPF hint session, target ${TARGET_WORK_NANOS / 1_000_000} ms" else "no ADPF")
        }
        (hint as? PerformanceHintManager.Session)?.reportActualWorkDuration(nanos)
    }

    companion object {
        const val TAG = "hipsterismo"
        val ANALYSIS_SIZE = Size(320, 240)

        /**
         * The work target per Frame: 20 ms, a little above the core's 17.8 ms
         * on the X3 (#53), so the hint keeps the thread there; on an A715 the
         * core alone takes 27 ms.
         */
        const val TARGET_WORK_NANOS = 20_000_000L
    }
}
