package io.github.spippoli.hipsterismo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import android.os.PerformanceHintManager
import android.os.Process
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeFalse
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer

/**
 * The core's steady-state time per 320×240 Frame on the phone, through the
 * preview's own path (a direct RGBA buffer and a FrameTransform), on one
 * thread like the preview's, without and with the ADPF hint session the
 * preview uses. It logs the first call, the median and the p90 under the tag
 * `hipsterismo-perf`, and asserts only the 15 fps floor (66 ms) on the median.
 * Skipped on emulators, whose ARM translator says nothing about a phone.
 */
@RunWith(AndroidJUnit4::class)
class CorePerformanceTest {
    private fun buffer(): Pair<ByteBuffer, FrameTransform> {
        val assets = InstrumentationRegistry.getInstrumentation().targetContext.assets
        val options = BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val bitmap = assets.open("fingers.png").use { BitmapFactory.decodeStream(it, null, options) }!!
        val buffer = ByteBuffer.allocateDirect(bitmap.byteCount)
        bitmap.copyPixelsToBuffer(buffer)
        return buffer to FrameTransform.of(bitmap.width, bitmap.height, bitmap.rowBytes)
    }

    private fun run(label: String, hinted: Boolean): Double {
        var median = 0.0
        val thread = Thread {
            Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY)
            val session = if (hinted && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                InstrumentationRegistry.getInstrumentation().targetContext
                    .getSystemService(PerformanceHintManager::class.java)
                    ?.createHintSession(intArrayOf(Process.myTid()), PreviewPipeline.TARGET_WORK_NANOS)
            } else null
            val (buffer, transform) = buffer()
            val core = DoubleArray(FRAMES)
            val work = DoubleArray(FRAMES)
            for (i in 0 until FRAMES) {
                val start = System.nanoTime()
                val mosaic = NativeMosaic.fromFrame(buffer, transform)
                val nanos = System.nanoTime() - start
                session?.reportActualWorkDuration(nanos)
                core[i] = mosaic.coreMs.toDouble()
                work[i] = nanos / 1e6
            }
            session?.close()
            val first = core[0]
            val steady = core.drop(WARMUP).sorted()
            val steadyWork = work.drop(WARMUP).sorted()
            median = steady[steady.size / 2]
            Log.i(
                TAG,
                "%s: first %.1f ms; steady core median %.1f ms, p90 %.1f ms, min %.1f ms; work median %.1f ms (%d Frames, hint %s)".format(
                    label, first, median, steady[steady.size * 9 / 10], steady.first(), steadyWork[steadyWork.size / 2],
                    steady.size, if (session != null) "on" else "off",
                ),
            )
        }
        thread.start()
        thread.join()
        return median
    }

    @Test
    fun steadyStateIsAbove15Fps() {
        assumeFalse("the floor is about phones, not the emulator's ARM translator", Emulator.isEmulator)
        val plain = run("plain thread", hinted = false)
        val hinted = run("ADPF hint", hinted = true)
        assertTrue("median $plain / $hinted ms", minOf(plain, hinted) < 66.0)
    }

    private companion object {
        const val TAG = "hipsterismo-perf"
        const val FRAMES = 200
        const val WARMUP = 20
    }
}
