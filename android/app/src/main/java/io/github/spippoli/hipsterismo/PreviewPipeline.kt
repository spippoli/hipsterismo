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
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * The live preview: CameraX binds Preview (the raw camera, shown until the
 * first Mosaic) and ImageAnalysis (RGBA, 320×240, only the latest Frame
 * kept). One preview thread turns each analysis Frame into a Mosaic, rotated
 * upright and mirrored for the front camera, with an ADPF hint session asking
 * for a fast core. The latest Mosaic is [mosaic]; [coreMs] holds the core's
 * times of the last second.
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
            cameras.bindToLifecycle(owner, selector, preview, analysis)
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
    }

    private fun analyze(image: ImageProxy) {
        image.use {
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
