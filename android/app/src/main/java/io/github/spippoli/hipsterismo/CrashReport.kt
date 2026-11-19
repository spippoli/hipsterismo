package io.github.spippoli.hipsterismo

import java.io.File
import java.io.IOException
import java.util.Locale

/*
 * The diagnostic report's crash and exit section (#107), its Android-free
 * part, so the JVM tests reach it: the last Java crash, which [CrashHandler]
 * writes to a file in `filesDir`, and the last process exit Android recorded
 * (`ApplicationExitInfo`, API 30+), which DiagnosticCollector reads. Every
 * trace is cut by [cutTrace], so the report stays far under the share
 * intent's size limit, and scrubbed by [scrubLine], so it names no file or Uri.
 */

/** The lines of a trace the report keeps, the "... N more lines" marker included. */
const val TRACE_LINES = 40

/** The characters of a trace line the report keeps. */
const val TRACE_LINE_CHARS = 200

// The extensions of code files, whose names a native frame keeps.
private val CODE_FILES = listOf(".so", ".apk", ".oat", ".odex", ".vdex", ".art", ".jar", ".dex")

private val URI = Regex("\\b[a-zA-Z][a-zA-Z0-9+.-]*://\\S+")

// An absolute path: a slash not right after a word character (so not the
// "a/b" of a class descriptor or a fingerprint), up to a space or bracket.
private val PATH = Regex("(?<![\\w.~-])/[^\\s:()\\[\\]{}\"',;]+")

/**
 * [line] with every Uri replaced by `<uri>` and every absolute path by
 * `<path>`, except a code file's (a library, an APK), which keeps only its
 * name: a native frame's `/apex/…/libc.so` becomes `libc.so`.
 */
fun scrubLine(line: String): String = PATH.replace(URI.replace(line, "<uri>")) { match ->
    val name = match.value.substringAfterLast('/')
    if (CODE_FILES.any { name.endsWith(it) }) name else "<path>"
}

/**
 * The report's excerpt of a trace: blank lines dropped, each line scrubbed,
 * right-trimmed and cut to [TRACE_LINE_CHARS], and at most [TRACE_LINES]
 * lines, the last of them then saying how many were cut, the [notRead]
 * lines the caller didn't read included.
 */
fun cutTrace(lines: List<String>, notRead: Int = 0): List<String> {
    val kept = lines.map { it.trimEnd() }.filter { it.isNotBlank() }.map { line ->
        val clean = scrubLine(line)
        if (clean.length > TRACE_LINE_CHARS) clean.take(TRACE_LINE_CHARS - 3) + "..." else clean
    }
    if (kept.size <= TRACE_LINES && notRead == 0) return kept
    val shown = kept.take(TRACE_LINES - 1)
    return shown + "... ${kept.size - shown.size + notRead} more lines"
}

private fun oneLine(text: String) = text.replace(Regex("[\\r\\n]+"), " ").trim()

/** The last Java crash: when, in which app version and thread, and its cut [trace]. */
data class CrashRecord(
    val timeMillis: Long,
    val versionName: String,
    val versionCode: Long,
    val thread: String,
    val trace: List<String>,
) {
    companion object {
        /** The record of [error] thrown on [thread]: its stack, causes included, cut by [cutTrace]. */
        fun of(thread: Thread, error: Throwable, timeMillis: Long, versionName: String, versionCode: Long) = CrashRecord(
            timeMillis, oneLine(versionName), versionCode, oneLine(thread.name), cutTrace(error.stackTraceToString().lines()),
        )
    }
}

/**
 * The last crash, kept in a file of the app's `filesDir` so it survives the
 * process it killed: four `name value` lines (time, version_name,
 * version_code, thread), then the trace. Each crash replaces the last. A file
 * that is missing, can't be read or doesn't parse holds no crash.
 */
object CrashFile {
    fun serialize(record: CrashRecord): String = buildString {
        appendLine("time ${record.timeMillis}")
        appendLine("version_name ${record.versionName}")
        appendLine("version_code ${record.versionCode}")
        appendLine("thread ${record.thread}")
        for (line in record.trace) appendLine(line)
    }

    fun parse(text: String): CrashRecord? {
        val lines = text.lines()
        if (lines.size < 5) return null
        fun field(index: Int, name: String) = lines[index].takeIf { it.startsWith("$name ") }?.removePrefix("$name ")
        val time = field(0, "time")?.toLongOrNull()?.takeIf { it >= 0 } ?: return null
        val versionName = field(1, "version_name") ?: return null
        val versionCode = field(2, "version_code")?.toLongOrNull() ?: return null
        val thread = field(3, "thread") ?: return null
        val trace = cutTrace(lines.drop(4))
        if (trace.isEmpty()) return null
        return CrashRecord(time, versionName, versionCode, thread, trace)
    }

    /** The crash in [file]; null if there is none or it can't be read. */
    fun load(file: File): CrashRecord? = try {
        if (file.isFile) parse(file.readText()) else null
    } catch (e: Exception) {
        null
    }

    /** Writes [record] to [file] through a temporary file and a rename, so it holds the old crash or the new one. */
    fun save(file: File, record: CrashRecord) {
        file.parentFile?.mkdirs()
        val temp = File(file.parentFile, "${file.name}.tmp")
        temp.writeText(serialize(record))
        if (!temp.renameTo(file)) {
            temp.delete()
            throw IOException("couldn't replace $file")
        }
    }
}

/**
 * The default uncaught-exception handler: it writes the crash to [file]
 * (never throwing, whatever goes wrong), then hands it on to [previous],
 * Android's, which logs it and kills the process.
 */
class CrashHandler(
    private val file: File,
    private val versionName: String,
    private val versionCode: Long,
    private val previous: Thread.UncaughtExceptionHandler?,
    private val clock: () -> Long = System::currentTimeMillis,
) : Thread.UncaughtExceptionHandler {
    override fun uncaughtException(thread: Thread, error: Throwable) {
        try {
            CrashFile.save(file, CrashRecord.of(thread, error, clock(), versionName, versionCode))
        } catch (t: Throwable) {
            // Nothing to report it to: the process is going.
        }
        previous?.uncaughtException(thread, error)
    }
}

/**
 * The last process exit, from `ApplicationExitInfo`: its [reason] (the
 * `REASON_*` code), when, the exit [status] (the signal of a native crash),
 * the process importance, its last PSS and RSS in kB, Android's description,
 * and the cut ANR trace or native tombstone, if any.
 */
data class ExitRecord(
    val reason: Int,
    val timeMillis: Long,
    val status: Int,
    val importance: Int,
    val pssKb: Long,
    val rssKb: Long,
    val description: String?,
    val trace: List<String>,
)

/** `ApplicationExitInfo.REASON_*` by name. */
fun exitReasonName(reason: Int): String = when (reason) {
    0 -> "unknown"
    1 -> "exit_self"
    2 -> "signaled"
    3 -> "low_memory"
    4 -> "crash"
    5 -> "crash_native"
    6 -> "anr"
    7 -> "initialization_failure"
    8 -> "permission_change"
    9 -> "excessive_resource_usage"
    10 -> "user_requested"
    11 -> "user_stopped"
    12 -> "dependency_died"
    13 -> "other"
    14 -> "freezer"
    15 -> "package_state_change"
    16 -> "package_updated"
    else -> "reason $reason"
}

/** `REASON_ANR` and `REASON_CRASH_NATIVE`: the reasons with a trace. */
const val EXIT_REASON_ANR = 6
const val EXIT_REASON_CRASH_NATIVE = 5

/**
 * The excerpt of an ANR trace: from the main thread's stack (the line that
 * starts `"main"`), where the ANR is, else from the top; only the lines
 * [cutTrace] needs are read, so a trace of megabytes costs nothing.
 */
fun anrExcerpt(lines: Sequence<String>): List<String> {
    val head = mutableListOf<String>()
    var headNotRead = 0
    var main: MutableList<String>? = null
    var mainNotRead = 0
    for (line in lines) {
        val stack = main
        if (stack == null) {
            when {
                line.startsWith("\"main\"") -> main = mutableListOf(line)
                head.size <= TRACE_LINES -> head += line
                line.isNotBlank() -> headNotRead++
            }
        } else {
            // The main thread's stack ends at its blank line.
            if (line.isBlank()) break
            if (stack.size <= TRACE_LINES) stack += line else mainNotRead++
        }
    }
    return main?.let { cutTrace(it, mainNotRead) } ?: cutTrace(head, headNotRead)
}

/**
 * The excerpt of a native tombstone, the protobuf (`tombstone.proto` of
 * debuggerd) that `ApplicationExitInfo` gives on API 31+: the signal, the
 * abort message, and the crashing thread's backtrace, one frame per line as
 * the text tombstone prints it, library names only. A tombstone cut short
 * keeps the fields before the cut. Null if it doesn't parse.
 */
fun tombstoneExcerpt(bytes: ByteArray): List<String>? = try {
    Tombstone.lines(bytes)
} catch (e: Exception) {
    null
}

private object Tombstone {
    private class Malformed : Exception()

    // A message's fields: a varint or fixed number as a Long, a
    // length-delimited field as its byte range.
    private class Message(private val bytes: ByteArray, private val fields: Map<Int, List<Any>>) {
        fun long(n: Int): Long? = fields[n]?.lastOrNull() as? Long
        fun string(n: Int): String? = (fields[n]?.lastOrNull() as? IntRange)?.let { String(bytes, it.first, it.count(), Charsets.UTF_8) }
        fun messages(n: Int): List<Message> = fields[n].orEmpty().filterIsInstance<IntRange>().map { parse(bytes, it.first, it.last + 1) }
        fun message(n: Int): Message? = messages(n).lastOrNull()
    }

    // [lenient]: a field cut by the end of the bytes ends the message, with the fields before it.
    private fun parse(bytes: ByteArray, start: Int, end: Int, lenient: Boolean = false): Message {
        var pos = start
        fun varint(): Long {
            var result = 0L
            var shift = 0
            while (true) {
                if (pos >= end || shift > 63) throw Malformed()
                val b = bytes[pos++].toInt() and 0xff
                result = result or ((b and 0x7f).toLong() shl shift)
                if (b and 0x80 == 0) return result
                shift += 7
            }
        }
        fun fixed(size: Int): Long {
            if (end - pos < size) throw Malformed()
            var result = 0L
            for (i in 0 until size) result = result or ((bytes[pos + i].toLong() and 0xff) shl (8 * i))
            pos += size
            return result
        }
        val fields = mutableMapOf<Int, MutableList<Any>>()
        while (pos < end) {
            val fieldStart = pos
            val key = try {
                varint()
            } catch (e: Malformed) {
                if (lenient) break else throw e
            }
            val number = (key ushr 3).toInt()
            if (number <= 0) throw Malformed()
            val value: Any = when ((key and 7).toInt()) {
                0 -> varint()
                1 -> fixed(8)
                2 -> {
                    val length = varint()
                    if (length < 0) throw Malformed()
                    if (length > end - pos) {
                        if (lenient) {
                            pos = fieldStart
                            break
                        }
                        throw Malformed()
                    }
                    (pos until pos + length.toInt()).also { pos += length.toInt() }
                }
                5 -> fixed(4)
                else -> throw Malformed()
            }
            fields.getOrPut(number) { mutableListOf() } += value
        }
        return Message(bytes, fields)
    }

    fun lines(bytes: ByteArray): List<String> {
        val tombstone = parse(bytes, 0, bytes.size, lenient = true)
        val lines = mutableListOf<String>()
        // Signal: number 1, name 2, code 3, code_name 4, has_fault_address 8, fault_address 9.
        tombstone.message(10)?.let { s ->
            val fault = if (s.long(8) == 1L) ", fault addr 0x%x".format(Locale.ROOT, s.long(9) ?: 0L) else ""
            lines += "signal ${s.long(1)?.toInt() ?: 0} (${s.string(2).orEmpty()}), code ${s.long(3)?.toInt() ?: 0} (${s.string(4).orEmpty()})$fault"
        }
        tombstone.string(14)?.takeIf { it.isNotEmpty() }?.let { lines += "abort message: $it" }
        // threads: a map entry (key 1, Thread 2); Thread: id 1, name 2, current_backtrace 4.
        val tid = tombstone.long(6)
        val threads = tombstone.messages(16).mapNotNull { entry -> entry.message(2)?.let { (entry.long(1)) to it } }
        val thread = threads.firstOrNull { it.first == tid }?.second ?: threads.firstOrNull()?.second
        if (thread != null) {
            lines += "thread ${thread.string(2).orEmpty()} (tid ${thread.long(1) ?: tid ?: 0})"
            // BacktraceFrame: rel_pc 1, function_name 4, function_offset 5, file_name 6.
            thread.messages(4).forEachIndexed { i, frame ->
                val function = frame.string(4)?.takeIf { it.isNotEmpty() }?.let { " ($it+${frame.long(5) ?: 0})" }.orEmpty()
                lines += "#%02d pc %016x  %s%s".format(Locale.ROOT, i, frame.long(1) ?: 0L, frame.string(6).orEmpty(), function)
            }
        }
        if (lines.isEmpty()) throw Malformed()
        return cutTrace(lines)
    }
}
