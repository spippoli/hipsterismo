package io.github.spippoli.hipsterismo

/**
 * Runs [raise], a thread priority change, and returns true. If the system
 * refuses it (some OEMs restrict priorities: `Process.setThreadPriority`
 * throws `SecurityException` or `IllegalArgumentException`), [onRefused] gets
 * the exception and it returns false, so the thread runs on at its default
 * priority instead of dying (#101). Free of Android classes for the JVM tests.
 */
fun raisePriority(raise: () -> Unit, onRefused: (RuntimeException) -> Unit): Boolean =
    try {
        raise()
        true
    } catch (e: RuntimeException) {
        onRefused(e)
        false
    }
