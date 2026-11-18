package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.util.Locale

class DiagnosticReportTest {
    @get:Rule
    val folder = TemporaryFolder()

    private fun photo(totalMs: Double, source: PhotoSource = PhotoSource.CAMERA, tessels: Int = 1392, grain: Int = 32) =
        PhotoTiming(source, 2048, 1536, totalMs, 210.0, 70.5, 540.25, tessels, grain)

    private fun data(
        preview: PreviewSummary? = PreviewSummary(fpsMedian = 29.5, coreMs = 22.25, workMs = 22.75, seconds = 60),
        photos: List<PhotoTiming> = listOf(photo(1012.4), photo(950.0, PhotoSource.GALLERY, 2472, 64)),
        cameraError: CameraError? = CameraError(CameraErrorKind.CAMERA_STATE, "PENDING_OPEN: CAMERA_IN_USE (recoverable)", 1_795_000_000_000L),
        coreHash: Long? = FINGERS_GOLDEN_HASH,
        adpf: Boolean? = true,
    ) = ReportData(
        timeMillis = 1_795_036_800_000L,
        versionName = "0.1.0",
        versionCode = 100,
        language = "it-IT",
        adpf = adpf,
        manufacturer = "Google",
        model = "Pixel 8a",
        fingerprint = "google/akita/akita:17/AP4A/123:user/release-keys",
        soc = "Google Tensor G3",
        api = 37,
        securityPatch = "2026-11-05",
        ramBytes = 8_000_000_000L,
        cpuMaxKhz = listOf(1704000L, 1704000L, null, 2914000L),
        thermalStatus = 0,
        cameras = listOf(CameraLine("0", "back", "FULL"), CameraLine("1", "front", "LIMITED")),
        analysisFrame = "320x240 -> 240x320",
        preview = preview,
        photos = photos,
        coreHash = coreHash,
        cameraError = cameraError,
    )

    private fun lines(text: String) = text.lines().filter { it.isNotEmpty() }

    private fun valueOf(text: String, key: String): String {
        val line = lines(text).single { it.startsWith("$key: ") }
        return line.removePrefix("$key: ")
    }

    @Test
    fun theReportHasTheHeaderAndEveryKeyOnceAsKeyColonValue() {
        val text = formatReport(data())
        val all = lines(text)
        assertTrue(all.first(), all.first().startsWith("Hipsterismo diagnostic report"))
        assertTrue(all.first(), "no personal data" in all.first())
        for (key in REPORT_KEYS) assertEquals(key, 1, all.count { it.startsWith("$key: ") })
        // Every line after the header is one key: value.
        for (line in all.drop(1)) assertTrue(line, Regex("^[a-z0-9_.]+: .+$").matches(line))
    }

    @Test
    fun theValuesAreTheData() {
        val text = formatReport(data())
        assertEquals("2026-11-18T21:20:00Z", valueOf(text, "report.time"))
        assertEquals("0.1.0", valueOf(text, "app.version_name"))
        assertEquals("100", valueOf(text, "app.version_code"))
        assertEquals("it-IT", valueOf(text, "app.language"))
        assertEquals("yes", valueOf(text, "app.adpf"))
        assertEquals("Google", valueOf(text, "device.manufacturer"))
        assertEquals("Pixel 8a", valueOf(text, "device.model"))
        assertEquals("google/akita/akita:17/AP4A/123:user/release-keys", valueOf(text, "device.fingerprint"))
        assertEquals("Google Tensor G3", valueOf(text, "device.soc"))
        assertEquals("37", valueOf(text, "device.api"))
        assertEquals("2026-11-05", valueOf(text, "device.security_patch"))
        assertEquals("7629", valueOf(text, "device.ram_mb"))
        assertEquals("1704000 1704000 ? 2914000", valueOf(text, "device.cpu_max_freq_khz"))
        assertEquals("none (0)", valueOf(text, "thermal.status"))
        assertEquals("0 back FULL, 1 front LIMITED", valueOf(text, "camera.list"))
        assertEquals("320x240 -> 240x320", valueOf(text, "camera.analysis_frame"))
        assertEquals("29.5", valueOf(text, "preview.fps_median"))
        assertEquals("60", valueOf(text, "preview.seconds"))
        assertEquals("22.3", valueOf(text, "core.mean_ms"))
        assertEquals("22.8", valueOf(text, "work.mean_ms"))
        assertEquals("2", valueOf(text, "photos.count"))
        assertEquals("981", valueOf(text, "photos.median_ms"))
        assertEquals(
            "camera 2048x1536: 1012 ms (capture 210, decode 71, core 540), 1392 Tessels, grain 32",
            valueOf(text, "photo.1"),
        )
        assertEquals(
            "gallery 2048x1536: 950 ms (capture 210, decode 71, core 540), 2472 Tessels, grain 64",
            valueOf(text, "photo.2"),
        )
        assertEquals("0xe0d145733a499498 (golden)", valueOf(text, "core.hash_fingers"))
        assertEquals(
            "CAMERA_STATE PENDING_OPEN: CAMERA_IN_USE (recoverable) at 2026-11-18T11:06:40Z",
            valueOf(text, "camera.last_error"),
        )
    }

    @Test
    fun numbersUseLocaleRootAlsoWhenTheDefaultLocaleIsItalian() {
        val before = Locale.getDefault()
        try {
            Locale.setDefault(Locale.ITALY)
            val text = formatReport(data())
            assertEquals("29.5", valueOf(text, "preview.fps_median"))
            assertEquals("22.3", valueOf(text, "core.mean_ms"))
            assertFalse(text, Regex("\\d,\\d").containsMatchIn(text))
        } finally {
            Locale.setDefault(before)
        }
    }

    @Test
    fun withoutAPreviewTheNumbersReadNa() {
        val text = formatReport(data(preview = null, photos = emptyList(), cameraError = null, coreHash = null, adpf = null))
        assertEquals("n/a", valueOf(text, "preview.fps_median"))
        assertEquals("0", valueOf(text, "preview.seconds"))
        assertEquals("n/a", valueOf(text, "core.mean_ms"))
        assertEquals("n/a", valueOf(text, "work.mean_ms"))
        assertEquals("0", valueOf(text, "photos.count"))
        assertEquals("n/a", valueOf(text, "photos.median_ms"))
        assertTrue(lines(text).none { it.startsWith("photo.") })
        assertEquals("n/a", valueOf(text, "core.hash_fingers"))
        assertEquals("none", valueOf(text, "camera.last_error"))
        assertEquals("n/a", valueOf(text, "app.adpf"))
        for (key in REPORT_KEYS) assertEquals(key, 1, lines(text).count { it.startsWith("$key: ") })
    }

    @Test
    fun aWrongHashIsMarkedAndTheThermalStatusNamed() {
        val text = formatReport(data(coreHash = 0x1234L).copy(thermalStatus = 3))
        assertEquals("0x0000000000001234 (not golden)", valueOf(text, "core.hash_fingers"))
        assertEquals("severe (3)", valueOf(text, "thermal.status"))
        assertEquals("unknown (9)", valueOf(formatReport(data().copy(thermalStatus = 9)), "thermal.status"))
    }

    @Test
    fun aLineBreakInAValueStaysOnItsLine() {
        val error = CameraError(CameraErrorKind.BIND_FAILED, "IllegalStateException: one\ntwo", 0L)
        val text = formatReport(data(cameraError = error))
        assertEquals("BIND_FAILED IllegalStateException: one two at 1970-01-01T00:00:00Z", valueOf(text, "camera.last_error"))
    }

    @Test
    fun theMedianOfOddAndEvenCounts() {
        assertNull(median(emptyList()))
        assertEquals(3.0, median(listOf(5.0, 1.0, 3.0))!!, 0.0)
        assertEquals(2.5, median(listOf(4.0, 1.0, 3.0, 2.0))!!, 0.0)
    }

    @Test
    fun thePreviewSamplesKeepTheLast60Seconds() {
        val samples = PreviewSamples()
        assertNull(samples.summary())
        for (i in 1..90) samples.add(fps = i.toDouble(), coreMs = 20.0, workMs = 21.0)
        val summary = samples.summary()!!
        // Seconds 31…90 remain: their median is 60.5.
        assertEquals(60, summary.seconds)
        assertEquals(60.5, summary.fpsMedian, 0.0)
        assertEquals(20.0, summary.coreMs, 0.0)
        assertEquals(21.0, summary.workMs, 0.0)
    }

    @Test
    fun thePreviewSamplesMedianIgnoresOutliers() {
        val samples = PreviewSamples()
        listOf(30.0, 29.0, 3.0, 30.0, 28.0).forEach { samples.add(it, 22.0, 23.0) }
        assertEquals(29.0, samples.summary()!!.fpsMedian, 0.0)
        // The core and work means are plain means.
        val means = PreviewSamples().apply { add(30.0, 20.0, 21.0); add(30.0, 30.0, 31.0) }.summary()!!
        assertEquals(25.0, means.coreMs, 0.0)
        assertEquals(26.0, means.workMs, 0.0)
    }

    @Test
    fun thePhotoHistoryKeepsTheLastFive() {
        var photos = emptyList<PhotoTiming>()
        for (i in 1..7) photos = PhotoHistory.added(photos, photo(i * 100.0))
        assertEquals(listOf(300.0, 400.0, 500.0, 600.0, 700.0), photos.map { it.totalMs })
        assertEquals(500.0, PhotoHistory.medianMs(photos)!!, 0.0)
        assertNull(PhotoHistory.medianMs(emptyList()))
    }

    @Test
    fun thePhotoHistorySurvivesARoundTrip() {
        val photos = listOf(photo(1012.4), photo(950.125, PhotoSource.GALLERY, 2472, 64))
        val before = Locale.getDefault()
        try {
            Locale.setDefault(Locale.ITALY)
            assertEquals(photos, PhotoHistory.parse(PhotoHistory.serialize(photos)))
        } finally {
            Locale.setDefault(before)
        }
        val file = File(folder.root, "diagnostics/photos.txt")
        PhotoHistory.save(file, photos)
        assertEquals(photos, PhotoHistory.load(file))
        PhotoHistory.record(file, photo(1.0))
        assertEquals(listOf(1012.4, 950.125, 1.0), PhotoHistory.load(file).map { it.totalMs })
        // No temporary file is left next to it.
        assertEquals(listOf("photos.txt"), file.parentFile!!.list()!!.toList())
    }

    @Test
    fun thePhotoHistoryCopesWithMissingOrCorruptData() {
        assertEquals(emptyList<PhotoTiming>(), PhotoHistory.load(File(folder.root, "missing.txt")))
        assertEquals(emptyList<PhotoTiming>(), PhotoHistory.parse(""))
        val good = PhotoHistory.serialize(listOf(photo(800.0)))
        val text = listOf(
            "garbage",
            "camera 2048 1536 x 1 2 3 4 5",
            "drone 2048 1536 1 1 1 1 1 1",
            "camera 2048 1536 NaN 1 1 1 1 1",
            "camera -5 1536 1 1 1 1 1 1",
            "camera 2048 1536 1 1 1 1 1",
            "\u0000\u0001",
            good.trim(),
        ).joinToString("\n")
        assertEquals(listOf(800.0), PhotoHistory.parse(text).map { it.totalMs })
        val corrupt = folder.newFile("corrupt.txt").apply { writeBytes(byteArrayOf(-1, 0, 10, 13, -128)) }
        assertEquals(emptyList<PhotoTiming>(), PhotoHistory.load(corrupt))
        // A corrupt file is replaced by the next photo.
        PhotoHistory.record(corrupt, photo(5.0))
        assertEquals(listOf(5.0), PhotoHistory.load(corrupt).map { it.totalMs })
        // More than five lines on disk: the last five.
        val many = PhotoHistory.serialize((1..8).map { photo(it.toDouble()) })
        assertEquals(listOf(4.0, 5.0, 6.0, 7.0, 8.0), PhotoHistory.parse(many).map { it.totalMs })
    }
}
