package io.github.spippoli.hipsterismo

import android.content.Intent
import android.os.Build
import android.provider.Settings
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * The diagnostic report built on the device (#106): every key, the golden
 * hash of `fingers.png` computed here, and none of the fields it must never
 * hold.
 */
@RunWith(AndroidJUnit4::class)
class DiagnosticCollectorTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    private fun report(photos: File, diagnostics: Diagnostics = Diagnostics()): String {
        val text = formatReport(DiagnosticCollector.collect(context, diagnostics, photos))
        text.lineSequence().forEach { Log.i("hipsterismo-perf", "report | $it") }
        return text
    }

    private fun valueOf(text: String, key: String) =
        text.lines().single { it.startsWith("$key: ") }.removePrefix("$key: ")

    private fun tempPhotos() = File(context.cacheDir, "diagnostic-test/photos.txt").apply {
        parentFile!!.deleteRecursively()
    }

    @Test
    fun theReportHasEveryKeyAndTheDevice() {
        val diagnostics = Diagnostics().apply {
            analysisFrame = "320x240 -> 240x320"
            adpf = true
            repeat(3) { preview.add(30.0, 22.0, 23.0) }
        }
        val photos = tempPhotos()
        PhotoHistory.record(photos, PhotoTiming(PhotoSource.CAMERA, 2048, 1536, 1000.0, 200.0, 70.0, 500.0, 2472, 32))
        val text = report(photos, diagnostics)
        assertTrue(text, text.startsWith(REPORT_HEADER))
        for (key in REPORT_KEYS) assertEquals(key, 1, text.lines().count { it.startsWith("$key: ") })
        assertEquals(Build.MODEL, valueOf(text, "device.model"))
        assertEquals(Build.VERSION.SDK_INT.toString(), valueOf(text, "device.api"))
        assertEquals("30.0", valueOf(text, "preview.fps_median"))
        assertEquals("3", valueOf(text, "preview.seconds"))
        assertEquals("1", valueOf(text, "photos.count"))
        assertTrue(valueOf(text, "photo.1"), valueOf(text, "photo.1").startsWith("camera 2048x1536: 1000 ms"))
        assertTrue(valueOf(text, "camera.list"), valueOf(text, "camera.list") != "none" || Emulator.isEmulator)
        assertTrue(valueOf(text, "device.cpu_max_freq_khz").split(' ').isNotEmpty())
        assertTrue(valueOf(text, "device.ram_mb").toLong() > 0)
        photos.parentFile!!.deleteRecursively()
    }

    @Test
    fun theHashIsFingersGolden() {
        assumeTrue("the ARM translator of this emulator changes the core's floats", Emulator.keepsGoldenHashes)
        val text = report(tempPhotos())
        assertEquals("0xe0d145733a499498 (golden)", valueOf(text, "core.hash_fingers"))
    }

    @Test
    fun withoutAPreviewItSaysNa() {
        val text = report(tempPhotos())
        assertEquals("n/a", valueOf(text, "preview.fps_median"))
        assertEquals("0", valueOf(text, "photos.count"))
    }

    @Test
    fun noForbiddenFieldAppears() {
        val photos = tempPhotos()
        PhotoHistory.record(photos, PhotoTiming(PhotoSource.GALLERY, 1536, 2048, 900.0, 0.0, 60.0, 480.0, 2300, 64))
        val text = report(photos, Diagnostics().apply { preview.add(29.0, 22.0, 23.0) })
        val lower = text.lowercase()
        val keys = text.lines().drop(1).filter { it.isNotEmpty() }.map { it.substringBefore(':').lowercase() }
        // The words of the keys ("device.security_patch" is device, security, patch).
        val words = keys.flatMap { it.split('.', '_') }.toSet()
        for (word in listOf("serial", "android", "id", "account", "accounts", "location", "gps", "uri", "file", "path", "email", "imei")) {
            assertFalse("a key names $word: $keys", word in words)
        }
        val androidId = Settings.Secure.getString(context.contentResolver, Settings.Secure.ANDROID_ID)
        if (!androidId.isNullOrEmpty()) assertFalse("ANDROID_ID in the report", androidId.lowercase() in lower)
        for (fragment in listOf("content://", "file://", "/data/", "/storage/", "/sdcard", ".jpg", ".png", "@")) {
            assertFalse("$fragment in the report", fragment in lower)
        }
        photos.parentFile!!.deleteRecursively()
    }

    @Test
    fun theShareIntentIsPlainText() {
        val chooser = DiagnosticCollector.shareIntent("a: b\n", "subject", "title")
        assertEquals(Intent.ACTION_CHOOSER, chooser.action)
        val send = androidx.core.content.IntentCompat.getParcelableExtra(chooser, Intent.EXTRA_INTENT, Intent::class.java)!!
        assertEquals(Intent.ACTION_SEND, send.action)
        assertEquals("text/plain", send.type)
        assertEquals("a: b\n", send.getStringExtra(Intent.EXTRA_TEXT))
        assertFalse(send.hasExtra(Intent.EXTRA_STREAM))
    }
}
