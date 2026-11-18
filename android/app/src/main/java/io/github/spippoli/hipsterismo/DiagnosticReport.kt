package io.github.spippoli.hipsterismo

import java.io.File
import java.time.Instant
import java.util.Locale

/*
 * The diagnostic report (#106): plain text, one `key: value` per line, in
 * English with Locale.ROOT numbers, which a long-press on the LIVE chip
 * shares. This file is its Android-free part, so the JVM tests reach it: the
 * formatter, the Preview's last 60 s, and the last five photos kept on disk.
 * DiagnosticCollector gathers the rest on the phone.
 */

/** The adaptive hash of `fingers.png` at the default min patch area: the core tests' golden. */
const val FINGERS_GOLDEN_HASH: Long = -0x1f2eba8cc5b66b68L // 0xe0d145733a499498

/** The report's keys, in order; `photo.1` … `photo.5` follow `photos.median_ms`, one per photo kept. */
val REPORT_KEYS = listOf(
    "report.time",
    "app.version_name",
    "app.version_code",
    "app.language",
    "app.adpf",
    "device.manufacturer",
    "device.model",
    "device.fingerprint",
    "device.soc",
    "device.api",
    "device.security_patch",
    "device.ram_mb",
    "device.cpu_max_freq_khz",
    "thermal.status",
    "camera.list",
    "camera.analysis_frame",
    "camera.last_error",
    "preview.fps_median",
    "preview.seconds",
    "core.mean_ms",
    "work.mean_ms",
    "photos.count",
    "photos.median_ms",
    "core.hash_fingers",
)

/** The report's first line. */
const val REPORT_HEADER = "Hipsterismo diagnostic report. It contains no personal data: no serial number, account, location, file name or photo."

/** One camera: its Camera2 [id], its [facing] (back, front, external) and its hardware level (LEGACY … LEVEL_3). */
data class CameraLine(val id: String, val facing: String, val hardwareLevel: String)

/** The Preview over its last [seconds] seconds: the median fps, and the mean core and work ms per Frame. */
data class PreviewSummary(val fpsMedian: Double, val coreMs: Double, val workMs: Double, val seconds: Int)

/**
 * Everything the report says. [preview] is null if the Preview hasn't run
 * this session, [coreHash] if hashing `fingers.png` failed, [adpf] if no
 * Frame has asked for a hint session yet; [cpuMaxKhz] holds null for a CPU
 * whose frequency can't be read; [thermalStatus] is `PowerManager`'s.
 */
data class ReportData(
    val timeMillis: Long,
    val versionName: String,
    val versionCode: Long,
    val language: String,
    val adpf: Boolean?,
    val manufacturer: String,
    val model: String,
    val fingerprint: String,
    val soc: String,
    val api: Int,
    val securityPatch: String,
    val ramBytes: Long,
    val cpuMaxKhz: List<Long?>,
    val thermalStatus: Int,
    val cameras: List<CameraLine>,
    val analysisFrame: String?,
    val preview: PreviewSummary?,
    val photos: List<PhotoTiming>,
    val coreHash: Long?,
    val cameraError: CameraError?,
)

private const val NA = "n/a"

private fun Double.fixed(decimals: Int) = "%.${decimals}f".format(Locale.ROOT, this)

/** `PowerManager`'s thermal status by name, as `THERMAL_STATUS_*` reads. */
fun thermalName(status: Int): String = when (status) {
    0 -> "none"
    1 -> "light"
    2 -> "moderate"
    3 -> "severe"
    4 -> "critical"
    5 -> "emergency"
    6 -> "shutdown"
    else -> "unknown"
}

/** One photo's line: where from, its size, its times, its Tessels and its Grain (the min patch area). */
fun photoLine(p: PhotoTiming): String = "%s %dx%d: %.0f ms (capture %.0f, decode %.0f, core %.0f), %d Tessels, grain %d".format(
    Locale.ROOT, p.source.label, p.width, p.height, p.totalMs, p.captureMs, p.decodeMs, p.coreMs, p.tessels, p.minPatchArea,
)

/** The report of [data]: [REPORT_HEADER], then one `key: value` per line. */
fun formatReport(data: ReportData): String {
    val lines = mutableListOf<Pair<String, String>>()
    fun put(key: String, value: Any?) {
        // A value stays on its line, and is never empty.
        val text = value?.toString()?.replace(Regex("[\\r\\n]+"), " ")?.trim().orEmpty()
        lines += key to text.ifEmpty { NA }
    }
    fun yesNo(value: Boolean?) = when (value) {
        true -> "yes"
        false -> "no"
        null -> NA
    }

    put("report.time", Instant.ofEpochMilli(data.timeMillis))
    put("app.version_name", data.versionName)
    put("app.version_code", data.versionCode)
    put("app.language", data.language)
    put("app.adpf", yesNo(data.adpf))
    put("device.manufacturer", data.manufacturer)
    put("device.model", data.model)
    put("device.fingerprint", data.fingerprint)
    put("device.soc", data.soc)
    put("device.api", data.api)
    put("device.security_patch", data.securityPatch)
    put("device.ram_mb", data.ramBytes / (1024 * 1024))
    put("device.cpu_max_freq_khz", data.cpuMaxKhz.joinToString(" ") { it?.toString() ?: "?" })
    put("thermal.status", "${thermalName(data.thermalStatus)} (${data.thermalStatus})")
    put(
        "camera.list",
        if (data.cameras.isEmpty()) "none" else data.cameras.joinToString(", ") { "${it.id} ${it.facing} ${it.hardwareLevel}" },
    )
    put("camera.analysis_frame", data.analysisFrame)
    put("camera.last_error", data.cameraError?.let { "${it.kind} ${it.message} at ${Instant.ofEpochMilli(it.atMillis)}" } ?: "none")
    val preview = data.preview
    put("preview.fps_median", preview?.fpsMedian?.fixed(1))
    put("preview.seconds", preview?.seconds ?: 0)
    put("core.mean_ms", preview?.coreMs?.fixed(1))
    put("work.mean_ms", preview?.workMs?.fixed(1))
    put("photos.count", data.photos.size)
    put("photos.median_ms", PhotoHistory.medianMs(data.photos)?.fixed(0))
    data.photos.forEachIndexed { i, p -> put("photo.${i + 1}", photoLine(p)) }
    put(
        "core.hash_fingers",
        data.coreHash?.let { "0x%016x (%s)".format(Locale.ROOT, it, if (it == FINGERS_GOLDEN_HASH) "golden" else "not golden") },
    )
    return buildString {
        appendLine(REPORT_HEADER)
        for ((key, value) in lines) appendLine("$key: $value")
    }
}

/** The median of [values], the mean of the middle two for an even count; null if empty. */
fun median(values: List<Double>): Double? {
    if (values.isEmpty()) return null
    val sorted = values.sorted()
    val mid = sorted.size / 2
    return if (sorted.size % 2 == 1) sorted[mid] else (sorted[mid - 1] + sorted[mid]) / 2
}

/** The seconds of Preview the report covers. */
const val PREVIEW_SECONDS = 60

/**
 * The Preview's last [PREVIEW_SECONDS] seconds: one sample a second, the
 * numbers of the camera screen's `preview …` logcat line, so the report's
 * median matches those lines. Seconds without a Preview (a photo, the result
 * screen, the app in the background) add nothing, so after a pause the report
 * still covers the last minute the Preview ran.
 */
class PreviewSamples(private val capacity: Int = PREVIEW_SECONDS) {
    private val fps = ArrayDeque<Double>()
    private val core = ArrayDeque<Double>()
    private val work = ArrayDeque<Double>()

    @Synchronized
    fun add(fps: Double, coreMs: Double, workMs: Double) {
        this.fps.addLast(fps)
        core.addLast(coreMs)
        work.addLast(workMs)
        if (this.fps.size > capacity) {
            this.fps.removeFirst()
            core.removeFirst()
            work.removeFirst()
        }
    }

    /** The median fps and the mean core and work ms; null if the Preview hasn't run. */
    @Synchronized
    fun summary(): PreviewSummary? {
        val fpsMedian = median(fps.toList()) ?: return null
        return PreviewSummary(fpsMedian, core.average(), work.average(), fps.size)
    }
}

/**
 * The last five photos' [PhotoTiming]s, kept in a file of the app's
 * `filesDir` so they survive the process: one photo per line, its fields
 * separated by spaces, numbers in Locale.ROOT. Lines that don't parse are
 * skipped; a file that can't be read counts as empty.
 */
object PhotoHistory {
    /** How many photos the report shows. */
    const val KEEP = 5

    /** [photos] with [timing] added, the last [KEEP] of them. */
    fun added(photos: List<PhotoTiming>, timing: PhotoTiming): List<PhotoTiming> = (photos + timing).takeLast(KEEP)

    /** The median time to the result of [photos], in ms; null without photos. */
    fun medianMs(photos: List<PhotoTiming>): Double? = median(photos.map { it.totalMs })

    fun serialize(photos: List<PhotoTiming>): String = buildString {
        for (p in photos) {
            // Double.toString is locale-independent and round-trips exactly.
            appendLine(
                listOf(
                    p.source.label, p.width, p.height, p.totalMs, p.captureMs, p.decodeMs, p.coreMs, p.tessels, p.minPatchArea,
                ).joinToString(" "),
            )
        }
    }

    fun parse(text: String): List<PhotoTiming> = text.lines().mapNotNull(::parseLine).takeLast(KEEP)

    private fun parseLine(line: String): PhotoTiming? {
        val f = line.trim().split(' ')
        if (f.size != 9) return null
        val source = PhotoSource.entries.firstOrNull { it.label == f[0] } ?: return null
        val ints = listOf(f[1], f[2], f[7], f[8]).map { it.toIntOrNull()?.takeIf { v -> v >= 0 } ?: return null }
        val ms = listOf(f[3], f[4], f[5], f[6]).map { it.toDoubleOrNull()?.takeIf { v -> v.isFinite() && v >= 0 } ?: return null }
        return PhotoTiming(source, ints[0], ints[1], ms[0], ms[1], ms[2], ms[3], ints[2], ints[3])
    }

    /** The photos in [file]; none if it is missing or can't be read. */
    fun load(file: File): List<PhotoTiming> = try {
        if (file.isFile) parse(file.readText()) else emptyList()
    } catch (e: Exception) {
        emptyList()
    }

    /** Writes [photos] to [file] through a temporary file and a rename, so a crash leaves the old or the new one. */
    fun save(file: File, photos: List<PhotoTiming>) {
        file.parentFile?.mkdirs()
        val temp = File(file.parentFile, "${file.name}.tmp")
        temp.writeText(serialize(photos.takeLast(KEEP)))
        if (!temp.renameTo(file)) {
            temp.delete()
            throw java.io.IOException("couldn't replace $file")
        }
    }

    /** Adds [timing] to the photos in [file]. */
    fun record(file: File, timing: PhotoTiming) = save(file, added(load(file), timing))
}

/**
 * What the Preview pipeline and the camera screen learn for the report, as
 * they run: the Preview's last minute, the analysis Frame size CameraX
 * chose, and whether the ADPF hint session exists.
 */
class Diagnostics {
    val preview = PreviewSamples()

    /** The analysis image and the upright Frame, "320x240 -> 240x320"; null before the first Frame. */
    @Volatile
    var analysisFrame: String? = null

    /** Whether the Preview thread got an ADPF hint session; null before the first Frame. */
    @Volatile
    var adpf: Boolean? = null

    companion object {
        /** The app's one record, written by the pipeline and the camera screen, read by the report. */
        val app = Diagnostics()
    }
}
