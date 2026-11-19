package io.github.spippoli.hipsterismo

import android.app.Application

/**
 * The process's first code: it puts the crash handler (#107) in front of
 * Android's before any activity runs, so the diagnostic report can show the
 * last crash.
 */
class HipsterismoApp : Application() {
    override fun onCreate() {
        super.onCreate()
        DiagnosticCollector.installCrashHandler(this)
    }
}
