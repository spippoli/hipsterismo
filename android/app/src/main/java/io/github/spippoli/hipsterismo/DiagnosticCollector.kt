package io.github.spippoli.hipsterismo

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.os.Build
import android.os.PowerManager
import android.util.Log
import java.io.File

/**
 * Gathers the diagnostic report's data on the phone (#106) and shares it.
 * The formatting and the numbers kept over time are in [formatReport],
 * [PreviewSamples] and [PhotoHistory]; this only reads Android.
 *
 * It never reads the serial number, ANDROID_ID, accounts or location, and
 * names no file or photo Uri.
 */
object DiagnosticCollector {
    /** The last five photos' times, in the app's private files. */
    fun photosFile(context: Context) = File(context.filesDir, "diagnostics/photos.txt")

    /** Adds a photo's times to [photosFile]; a failure is logged, never thrown. */
    fun recordPhoto(context: Context, timing: PhotoTiming) {
        try {
            PhotoHistory.record(photosFile(context), timing)
        } catch (e: Exception) {
            Log.w(PreviewPipeline.TAG, "couldn't keep the photo's times: $e")
        }
    }

    /**
     * The report's data now. It hashes `fingers.png` with the core (about
     * 15 ms), so call it off the UI thread.
     */
    fun collect(
        context: Context,
        diagnostics: Diagnostics = Diagnostics.app,
        photos: File = photosFile(context),
    ): ReportData {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        val memory = ActivityManager.MemoryInfo().also {
            context.getSystemService(ActivityManager::class.java).getMemoryInfo(it)
        }
        return ReportData(
            timeMillis = System.currentTimeMillis(),
            versionName = info.versionName ?: "",
            versionCode = info.longVersionCode,
            language = context.resources.configuration.locales[0].toLanguageTag(),
            adpf = diagnostics.adpf,
            manufacturer = Build.MANUFACTURER,
            model = Build.MODEL,
            fingerprint = Build.FINGERPRINT,
            soc = soc(),
            api = Build.VERSION.SDK_INT,
            securityPatch = Build.VERSION.SECURITY_PATCH,
            ramBytes = memory.totalMem,
            cpuMaxKhz = cpuMaxKhz(),
            thermalStatus = context.getSystemService(PowerManager::class.java).currentThermalStatus,
            cameras = cameras(context),
            analysisFrame = diagnostics.analysisFrame,
            preview = diagnostics.preview.summary(),
            photos = PhotoHistory.load(photos),
            coreHash = fingersHash(context),
            cameraError = LastCameraError.app.value,
        )
    }

    /** The share sheet with [report] as plain text. */
    fun shareIntent(report: String, subject: String, title: String): Intent {
        val send = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_TEXT, report)
            putExtra(Intent.EXTRA_SUBJECT, subject)
        }
        return Intent.createChooser(send, title)
    }

    // API 31+ names the SoC; before, HARDWARE and BOARD, and the kernel's
    // "Hardware" line if it has one.
    private fun soc(): String {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return "${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}".trim()
        }
        val hardware = try {
            File("/proc/cpuinfo").useLines { lines ->
                lines.firstOrNull { it.startsWith("Hardware") }?.substringAfter(':')?.trim()
            }
        } catch (e: Exception) {
            null
        }
        return listOfNotNull(Build.HARDWARE, Build.BOARD, hardware).joinToString(" / ")
    }

    // Each CPU's cpuinfo_max_freq in kHz, cpu0 first; null where it can't be read.
    private fun cpuMaxKhz(): List<Long?> {
        val cpus = File("/sys/devices/system/cpu").listFiles { f -> f.name.matches(Regex("cpu\\d+")) }.orEmpty()
        return cpus.sortedBy { it.name.removePrefix("cpu").toInt() }.map { cpu ->
            try {
                File(cpu, "cpufreq/cpuinfo_max_freq").readText().trim().toLong()
            } catch (e: Exception) {
                null
            }
        }
    }

    private fun cameras(context: Context): List<CameraLine> = try {
        val manager = context.getSystemService(CameraManager::class.java)
        manager.cameraIdList.map { id ->
            val c = manager.getCameraCharacteristics(id)
            val facing = when (c.get(CameraCharacteristics.LENS_FACING)) {
                CameraCharacteristics.LENS_FACING_BACK -> "back"
                CameraCharacteristics.LENS_FACING_FRONT -> "front"
                CameraCharacteristics.LENS_FACING_EXTERNAL -> "external"
                else -> "unknown"
            }
            val level = when (val l = c.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)) {
                CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY -> "LEGACY"
                CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED -> "LIMITED"
                CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_FULL -> "FULL"
                CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_3 -> "LEVEL_3"
                CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL -> "EXTERNAL"
                else -> "level $l"
            }
            CameraLine(id, facing, level)
        }
    } catch (e: Exception) {
        Log.w(PreviewPipeline.TAG, "couldn't list the cameras: $e")
        emptyList()
    }

    // The core's hash of the bundled fingers.png at the default min patch
    // area: the golden if this phone computes what the desktop does.
    private fun fingersHash(context: Context): Long? = try {
        val options = BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val bitmap = context.assets.open("fingers.png").use { BitmapFactory.decodeStream(it, null, options) }
            ?: error("fingers.png didn't decode")
        try {
            NativeMosaic.fromBitmap(bitmap, MinPatchArea.DEFAULT).hash
        } finally {
            bitmap.recycle()
        }
    } catch (e: Exception) {
        Log.w(PreviewPipeline.TAG, "couldn't hash fingers.png: $e")
        null
    }
}
