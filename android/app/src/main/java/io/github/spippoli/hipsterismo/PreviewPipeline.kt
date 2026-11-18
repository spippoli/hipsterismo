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
import android.view.OrientationEventListener
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.CameraState as XCameraState
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.camera.core.ZoomState
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.Observer
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File
import java.util.concurrent.ExecutionException
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * The live preview and the photos: CameraX binds Preview (the raw camera,
 * shown until the first Mosaic), ImageAnalysis (RGBA, 320×240, only the
 * latest Frame kept) and ImageCapture (the shutter's 4:3 JPEG, capped near
 * 12 MP).
 * One thread turns each analysis Frame into a Mosaic, rotated upright and
 * mirrored for the front camera, with an ADPF hint session asking for a fast
 * core. The latest Mosaic is [mosaic]; [coreMs] holds the core's times of the
 * last second.
 *
 * [camera] says which camera is on, whether there is another one to
 * [switchCamera] to, and its zoom, which [setZoom] and [pinch] change. The
 * zoom acts on the whole camera, so the Mosaic and the photo are zoomed too.
 *
 * A photo ([takePhoto], [processPhoto]) runs on the same thread, with the
 * preview [paused]: the thread whose hint keeps it on the X3 does the photo's
 * decode and core, and no analysis Frame competes with it.
 *
 * [status] says whether the camera works (#104). Getting the provider, the
 * presence check and the bind are guarded, at start, on a switch and on
 * [retry], and the bound camera's CameraX state is observed, so a camera
 * that can't be opened or drops out later gives [CameraStatus.Failed]
 * ([nextCameraStatus]) instead of a crash or a frozen preview. The last
 * error stays in [LastCameraError.app].
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
            // One thread, so this logs at most once.
            raisePriority({ Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY) }) { e ->
                Log.w(TAG, "display priority refused, running at the default priority: $e")
            }
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

    // The shutter's photo: 4:3, capped near 12 MP ([captureSize]; the largest
    // 4:3 size on a camera under the cap), latency over quality, saved as a
    // JPEG with its EXIF orientation.
    private val capture = ImageCapture.Builder()
        .setCaptureMode(ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY)
        .setResolutionSelector(
            ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                .setResolutionStrategy(ResolutionStrategy.HIGHEST_AVAILABLE_STRATEGY)
                .setResolutionFilter(::captureFirst)
                .build(),
        )
        .build()

    // Puts [captureSize]'s choice first and keeps CameraX's order for the
    // rest, the fallbacks should that size not fit with the other use cases.
    private fun captureFirst(sizes: List<Size>, rotationDegrees: Int): List<Size> {
        val chosen = captureSize(sizes.map { PixelSize(it.width, it.height) }) ?: return sizes
        val first = sizes.first { it.width == chosen.width && it.height == chosen.height }
        Log.i(TAG, "capture sizes: ${sizes.size}, largest ${sizes.maxByOrNull { it.width.toLong() * it.height }}, chosen $first")
        return listOf(first) + (sizes - first)
    }

    /** The camera that is on and its zoom, for the camera screen's controls. */
    data class CameraState(
        val front: Boolean = false,
        val canSwitch: Boolean = false,
        val zoom: Float = 1f,
        val minZoom: Float = 1f,
        val maxZoom: Float = 1f,
    )

    private val _camera = MutableStateFlow(CameraState())
    val camera: StateFlow<CameraState> = _camera

    private val _status = MutableStateFlow<CameraStatus>(CameraStatus.Opening)

    /** Whether the camera works: the camera screen, or the camera-error screen. */
    val status: StateFlow<CameraStatus> = _status

    // On the main thread: the status after [event], the error kept for the report.
    private fun report(event: CameraEvent) {
        val before = _status.value
        val after = LastCameraError.app.track(nextCameraStatus(before, event, System.currentTimeMillis()))
        if (after == before) return
        _status.value = after
        if (after is CameraStatus.Failed) {
            Log.w(TAG, "camera error: ${after.error.kind} ${after.error.message}")
            // No stale Mosaic behind the error, and none to shoot once it is gone.
            _mosaic.value = null
        } else if (before is CameraStatus.Failed) {
            Log.i(TAG, "camera back: $after")
        }
    }

    private val stateObserver = Observer<XCameraState> { state -> report(cameraEvent(state)) }

    // Read by the analyzer's thread; set on the main thread before a bind.
    @Volatile
    private var mirror = false
    private var provider: ProcessCameraProvider? = null
    private var owner: LifecycleOwner? = null
    private var bound: Camera? = null

    // The last zoom asked for: a pinch builds on it, since the camera's
    // ZoomState arrives a little later than the pinch's next step.
    private var requestedZoom = 1f
    private val zoomObserver = Observer<ZoomState> { zoom ->
        _camera.value = _camera.value.copy(zoom = zoom.zoomRatio, minZoom = zoom.minZoomRatio, maxZoom = zoom.maxZoomRatio)
    }

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

    // The shutter's rotation follows how the phone is held, from the
    // accelerometer, not the display: with auto-rotate off the display stays
    // in portrait, and a photo taken in landscape would come out sideways.
    // The preview and the analysis keep following the display.
    private val orientationListener = object : OrientationEventListener(context) {
        override fun onOrientationChanged(orientation: Int) {
            val rotation = captureRotationFor(orientation) ?: return
            if (capture.targetRotation != rotation) capture.targetRotation = rotation
        }
    }

    /** Starts the camera for [owner]'s lifecycle, showing the raw preview through [surfaceProvider]. */
    fun start(owner: LifecycleOwner, surfaceProvider: Preview.SurfaceProvider) {
        preview.surfaceProvider = surfaceProvider
        updateRotation()
        displayManager.registerDisplayListener(displayListener, Handler(Looper.getMainLooper()))
        if (orientationListener.canDetectOrientation()) orientationListener.enable()
        analysis.setAnalyzer(executor, ::analyze)
        this.owner = owner
        open()
    }

    // Gets the provider if there is none yet, checks which cameras exist and
    // binds one; every step that can throw ends in [report]. Main thread.
    private fun open() {
        provider?.let {
            bindChosen(it)
            return
        }
        val future = try {
            ProcessCameraProvider.getInstance(context)
        } catch (e: Exception) {
            failed("camera provider", e)
            return
        }
        future.addListener({
            if (owner == null) return@addListener // stopped meanwhile
            val cameras = try {
                future.get()
            } catch (e: Exception) {
                failed("camera provider", e)
                return@addListener
            }
            provider = cameras
            bindChosen(cameras)
        }, context.mainExecutor)
    }

    // The camera last asked for (the pipeline outlives a rotation), else the
    // back one, else the front one; the error screen if there is neither.
    private fun bindChosen(cameras: ProcessCameraProvider) {
        val back: Boolean
        val front: Boolean
        try {
            back = cameras.hasCamera(CameraSelector.DEFAULT_BACK_CAMERA)
            front = cameras.hasCamera(CameraSelector.DEFAULT_FRONT_CAMERA)
        } catch (e: Exception) {
            failed("camera check", e)
            return
        }
        _camera.value = _camera.value.copy(canSwitch = back && front)
        when (cameraToBind(back, front, preferFront)) {
            Lens.FRONT -> bind(true)
            Lens.BACK -> bind(false)
            null -> {
                unbind(cameras)
                report(CameraEvent.NoCamera)
            }
        }
    }

    private fun failed(step: String, e: Exception) {
        // The provider's future wraps the camera's own exception.
        val cause = if (e is ExecutionException) e.cause ?: e else e
        Log.w(TAG, "$step failed", cause)
        report(CameraEvent.BindFailed(cause.javaClass.simpleName, cause.message))
    }

    // The camera last asked for; a retry binds it again.
    private var preferFront = false

    /** Binds the front camera if [front], else the back one; CameraX starts it at 1×. Main thread. */
    private fun bind(front: Boolean) {
        val cameras = provider ?: return
        val owner = owner ?: return
        val selector = if (front) CameraSelector.DEFAULT_FRONT_CAMERA else CameraSelector.DEFAULT_BACK_CAMERA
        unbind(cameras)
        mirror = front
        loggedFrame = false
        val camera = try {
            cameras.bindToLifecycle(owner, selector, preview, analysis, capture)
        } catch (e: Exception) {
            failed("camera bind", e)
            return
        }
        bound = camera
        report(CameraEvent.Bound)
        Log.i(TAG, "capture bound at ${capture.resolutionInfo?.resolution}")
        requestedZoom = camera.cameraInfo.zoomState.value?.zoomRatio ?: 1f
        _camera.value = _camera.value.copy(front = front, zoom = requestedZoom)
        camera.cameraInfo.zoomState.observe(owner, zoomObserver)
        camera.cameraInfo.cameraState.observe(owner, stateObserver)
    }

    private fun unbind(cameras: ProcessCameraProvider?) {
        bound?.cameraInfo?.zoomState?.removeObserver(zoomObserver)
        bound?.cameraInfo?.cameraState?.removeObserver(stateObserver)
        bound = null
        cameras?.unbindAll()
    }

    /** Switches between the back and the front camera, if the phone has both. Main thread. */
    fun switchCamera() {
        if (!_camera.value.canSwitch) return
        preferFront = !_camera.value.front
        bind(preferFront)
    }

    /** The camera-error screen's Retry: checks the cameras again and binds. Main thread. */
    fun retry() {
        if (owner == null) return
        report(CameraEvent.Retry)
        open()
    }

    /** Sets the zoom to [ratio], within the camera's range. Main thread. */
    fun setZoom(ratio: Float) {
        val camera = bound ?: return
        val state = _camera.value
        requestedZoom = ratio.coerceIn(state.minZoom, state.maxZoom)
        camera.cameraControl.setZoomRatio(requestedZoom)
    }

    /** Scales the zoom by a pinch's [factor]. Main thread. */
    fun pinch(factor: Float) {
        val state = _camera.value
        setZoom(pinchedZoom(requestedZoom, factor, state.minZoom, state.maxZoom))
    }

    /** Stops the camera and the preview thread. */
    fun stop() {
        displayManager.unregisterDisplayListener(displayListener)
        orientationListener.disable()
        unbind(provider)
        owner = null
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
        // The display's rotation until the accelerometer reports one (and on
        // a phone without one).
        if (!orientationListener.canDetectOrientation()) capture.targetRotation = rotation
    }

    /**
     * Takes a photo into [file] (a JPEG, upright through its EXIF
     * orientation; from the front camera mirrored like the preview, as the
     * stock camera saves selfies by default), then calls [onSaved] or [onError] on the pipeline's
     * thread.
     */
    fun takePhoto(file: File, onSaved: () -> Unit, onError: (Exception) -> Unit) {
        val metadata = ImageCapture.Metadata().apply { isReversedHorizontal = mirror }
        val options = ImageCapture.OutputFileOptions.Builder(file).setMetadata(metadata).build()
        capture.takePicture(options, executor, object : ImageCapture.OnImageSavedCallback {
            override fun onImageSaved(output: ImageCapture.OutputFileResults) {
                // The JPEG's stored size (before its EXIF rotation): only its header is read.
                val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeFile(file.path, bounds)
                Log.i(TAG, "photo captured at ${bounds.outWidth}x${bounds.outHeight}")
                onSaved()
            }
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
                        java.util.Locale.ROOT,
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
        /** CameraX's camera state as a [CameraEvent], its error code by name. */
        fun cameraEvent(state: XCameraState): CameraEvent.StateChanged {
            val type = when (state.type) {
                XCameraState.Type.PENDING_OPEN -> CameraStateType.PENDING_OPEN
                XCameraState.Type.OPENING -> CameraStateType.OPENING
                XCameraState.Type.OPEN -> CameraStateType.OPEN
                XCameraState.Type.CLOSING -> CameraStateType.CLOSING
                XCameraState.Type.CLOSED -> CameraStateType.CLOSED
            }
            val error = state.error?.let {
                val name = when (it.code) {
                    XCameraState.ERROR_MAX_CAMERAS_IN_USE -> "MAX_CAMERAS_IN_USE"
                    XCameraState.ERROR_CAMERA_IN_USE -> "CAMERA_IN_USE"
                    XCameraState.ERROR_OTHER_RECOVERABLE_ERROR -> "OTHER_RECOVERABLE_ERROR"
                    XCameraState.ERROR_STREAM_CONFIG -> "STREAM_CONFIG"
                    XCameraState.ERROR_CAMERA_DISABLED -> "CAMERA_DISABLED"
                    XCameraState.ERROR_CAMERA_FATAL_ERROR -> "CAMERA_FATAL_ERROR"
                    XCameraState.ERROR_DO_NOT_DISTURB_MODE_ENABLED -> "DO_NOT_DISTURB_MODE_ENABLED"
                    else -> "ERROR_${it.code}"
                }
                CameraStateError(name, it.type == XCameraState.ErrorType.CRITICAL)
            }
            return CameraEvent.StateChanged(type, error)
        }

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
