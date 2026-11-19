package io.github.spippoli.hipsterismo

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.os.Build
import android.os.PowerManager
import android.util.Log
import androidx.annotation.RequiresApi
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

    /** The last crash (#107), in the app's private files. */
    fun crashFile(context: Context) = File(context.filesDir, "diagnostics/crash.txt")

    /**
     * The handler that writes a crash to [file] and then hands it on to
     * [previous], with the app's version read now, not in the crash.
     */
    fun crashHandler(context: Context, file: File, previous: Thread.UncaughtExceptionHandler?): CrashHandler {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        return CrashHandler(file, info.versionName ?: "", info.longVersionCode, previous)
    }

    /** Makes [crashHandler] the default uncaught-exception handler, in front of Android's; once per process. */
    fun installCrashHandler(context: Context) {
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        if (previous is CrashHandler) return
        Thread.setDefaultUncaughtExceptionHandler(crashHandler(context, crashFile(context), previous))
    }

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
        crash: File = crashFile(context),
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
            crash = CrashFile.load(crash),
            exit = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) lastExit(context) else null,
            exitsKnown = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R,
        )
    }

    // The most recent exit Android recorded for this package, with its ANR
    // trace (API 30+) or native tombstone (API 31+); null if there is none.
    @RequiresApi(Build.VERSION_CODES.R)
    private fun lastExit(context: Context): ExitRecord? = try {
        val manager = context.getSystemService(ActivityManager::class.java)
        manager.getHistoricalProcessExitReasons(context.packageName, 0, 1).firstOrNull()?.let { info ->
            ExitRecord(
                reason = info.reason,
                timeMillis = info.timestamp,
                status = info.status,
                importance = info.importance,
                pssKb = info.pss,
                rssKb = info.rss,
                description = info.description,
                trace = exitTrace(info),
            )
        }
    } catch (e: Exception) {
        Log.w(PreviewPipeline.TAG, "couldn't read the last exit: $e")
        null
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun exitTrace(info: ApplicationExitInfo): List<String> = try {
        when (info.reason) {
            EXIT_REASON_ANR -> info.traceInputStream?.bufferedReader()?.useLines(::anrExcerpt)
            EXIT_REASON_CRASH_NATIVE -> info.traceInputStream?.use { stream ->
                val bytes = readAtMost(stream, MAX_TOMBSTONE_BYTES)
                tombstoneExcerpt(bytes) ?: listOf("unreadable tombstone, ${bytes.size} bytes")
            }
            else -> null
        }.orEmpty()
    } catch (e: Exception) {
        listOf("unreadable trace: ${e.javaClass.simpleName}")
    }

    // A tombstone is tens or hundreds of kB, its threads before its memory
    // maps and logs, so a cut one still parses (tombstoneExcerpt).
    private const val MAX_TOMBSTONE_BYTES = 4 shl 20

    // InputStream.readNBytes(int) is API 33.
    private fun readAtMost(stream: java.io.InputStream, max: Int): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(64 * 1024)
        while (out.size() < max) {
            val n = stream.read(buffer, 0, minOf(buffer.size, max - out.size()))
            if (n < 0) break
            out.write(buffer, 0, n)
        }
        return out.toByteArray()
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
