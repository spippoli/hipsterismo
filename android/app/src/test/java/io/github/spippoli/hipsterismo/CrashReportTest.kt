package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File

class CrashReportTest {
    @get:Rule
    val folder = TemporaryFolder()

    private fun record(trace: List<String> = listOf("java.lang.IllegalStateException: boom", "at a.B.c(B.kt:1)")) =
        CrashRecord(1_795_036_800_000L, "0.1.0", 100, "main", trace)

    // --- the trace cut ---

    @Test
    fun aShortTraceIsKeptWholeWithoutBlankLines() {
        assertEquals(listOf("one", "\tat two"), cutTrace(listOf("one", "", "\tat two   ", "  ")))
        assertEquals(emptyList<String>(), cutTrace(emptyList()))
    }

    @Test
    fun aLongTraceIsCutTo40LinesTheLastSayingHowManyWereCut() {
        val exactly = (1..TRACE_LINES).map { "line $it" }
        assertEquals(exactly, cutTrace(exactly))
        val long = (1..100).map { "line $it" }
        val cut = cutTrace(long)
        assertEquals(TRACE_LINES, cut.size)
        assertEquals("line 39", cut[38])
        assertEquals("... 61 more lines", cut.last())
        // Lines the caller didn't read count too.
        assertEquals(listOf("a", "... 1 more lines"), cutTrace(listOf("a"), notRead = 1))
    }

    @Test
    fun aLongLineIsCut() {
        val line = cutTrace(listOf("x".repeat(1000))).single()
        assertEquals(TRACE_LINE_CHARS, line.length)
        assertTrue(line.endsWith("..."))
    }

    @Test
    fun aTraceNamesNoFileOrUriButKeepsLibraries() {
        assertEquals(
            "java.io.FileNotFoundException: <path> (No such file or directory)",
            scrubLine("java.io.FileNotFoundException: /data/user/0/io.github.spippoli.hipsterismo/cache/photos/1.jpg (No such file or directory)"),
        )
        assertEquals("SecurityException: no access to <uri>", scrubLine("SecurityException: no access to content://media/external/images/media/42"))
        assertEquals(
            "native: #00 pc 000000000005c3a8  libc.so (syscall+24)",
            scrubLine("native: #00 pc 000000000005c3a8  /apex/com.android.runtime/lib64/bionic/libc.so (syscall+24)"),
        )
        assertEquals("#01 pc 0000000000001234  base.apk", scrubLine("#01 pc 0000000000001234  /data/app/~~x==/io.github-1==/base.apk"))
        // Class descriptors, fingerprints and Java frames stay as they are.
        for (same in listOf(
            "at io.github.spippoli.hipsterismo.MainActivity.onCreate(MainActivity.kt:52)",
            "Ljava/lang/String; google/akita/akita:17/AP4A/123:user/release-keys",
            "a / b and 1/2",
        )) {
            assertEquals(same, scrubLine(same))
        }
    }

    // --- the crash file ---

    @Test
    fun aCrashSurvivesARoundTrip() {
        val crash = record()
        assertEquals(crash, CrashFile.parse(CrashFile.serialize(crash)))
        val file = File(folder.root, "diagnostics/crash.txt")
        CrashFile.save(file, crash)
        assertEquals(crash, CrashFile.load(file))
        // Each crash replaces the last, and no temporary file is left.
        val next = record(listOf("java.lang.OutOfMemoryError")).copy(versionName = "0.2 beta", thread = "mosaic-preview")
        CrashFile.save(file, next)
        assertEquals(next, CrashFile.load(file))
        assertEquals(listOf("crash.txt"), file.parentFile!!.list()!!.toList())
    }

    @Test
    fun aMissingOrCorruptFileHoldsNoCrash() {
        assertNull(CrashFile.load(File(folder.root, "missing.txt")))
        assertNull(CrashFile.load(folder.root))
        assertNull(CrashFile.parse(""))
        val good = CrashFile.serialize(record()).lines()
        assertNull(CrashFile.parse(good.take(4).joinToString("\n")))
        assertNull(CrashFile.parse((listOf("time soon") + good.drop(1)).joinToString("\n")))
        assertNull(CrashFile.parse((listOf("time -5") + good.drop(1)).joinToString("\n")))
        assertNull(CrashFile.parse((good.take(2) + "version_code x" + good.drop(3)).joinToString("\n")))
        assertNull(CrashFile.parse((good.take(3) + "thraed main" + good.drop(4)).joinToString("\n")))
        assertNull(CrashFile.parse((good.take(4) + "" + "   ").joinToString("\n")))
        val corrupt = folder.newFile("crash.txt").apply { writeBytes(byteArrayOf(-1, 0, 10, 13, -128)) }
        assertNull(CrashFile.load(corrupt))
        // A corrupt file is replaced by the next crash.
        CrashFile.save(corrupt, record())
        assertEquals(record(), CrashFile.load(corrupt))
    }

    @Test
    fun aFileWithALongTraceIsCutWhenRead() {
        val text = CrashFile.serialize(record((1..500).map { "at f$it" }))
        assertEquals(TRACE_LINES, CrashFile.parse(text)!!.trace.size)
    }

    // --- the handler ---

    @Test
    fun theHandlerWritesTheCrashThenChains() {
        val file = File(folder.root, "diagnostics/crash.txt")
        val handed = mutableListOf<Throwable>()
        val handler = CrashHandler(file, "0.1.0", 100, { _, e -> handed += e }, clock = { 1_795_036_800_000L })
        // Short stacks, so the cause falls inside the cut.
        val cause = RuntimeException("the cause").apply { stackTrace = arrayOf(StackTraceElement("a.B", "g", "B.kt", 2)) }
        val error = IllegalStateException("boom", cause).apply { stackTrace = arrayOf(StackTraceElement("a.B", "f", "B.kt", 1)) }
        handler.uncaughtException(Thread("mosaic-preview"), error)
        assertSame(error, handed.single())
        val crash = CrashFile.load(file)!!
        assertEquals(1_795_036_800_000L, crash.timeMillis)
        assertEquals("0.1.0", crash.versionName)
        assertEquals(100L, crash.versionCode)
        assertEquals("mosaic-preview", crash.thread)
        assertEquals("java.lang.IllegalStateException: boom", crash.trace.first())
        assertEquals(
            listOf("java.lang.IllegalStateException: boom", "\tat a.B.f(B.kt:1)", "Caused by: java.lang.RuntimeException: the cause", "\tat a.B.g(B.kt:2)"),
            crash.trace,
        )
    }

    @Test
    fun theHandlerChainsEvenIfItCannotWrite() {
        // A file under a regular file can't be created.
        val blocked = File(folder.newFile("not-a-folder"), "crash.txt")
        var handed = false
        CrashHandler(blocked, "0.1.0", 100, { _, _ -> handed = true }).uncaughtException(Thread.currentThread(), Error("x"))
        assertTrue(handed)
        CrashHandler(blocked, "0.1.0", 100, null).uncaughtException(Thread.currentThread(), Error("x"))
    }

    // --- the report ---

    private fun report(crash: CrashRecord?, exit: ExitRecord?, exitsKnown: Boolean = true) = formatReport(
        ReportData(
            0L, "0.1.0", 100, "en", null, "Google", "Pixel 8a", "f", "soc", 37, "p", 0L, emptyList(), 0,
            emptyList(), null, null, emptyList(), null, null, crash = crash, exit = exit, exitsKnown = exitsKnown,
        ),
    )

    private fun valueOf(text: String, key: String) = text.lines().single { it.startsWith("$key: ") }.removePrefix("$key: ")

    @Test
    fun theReportShowsTheCrashAndTheExit() {
        val exit = ExitRecord(6, 1_795_036_860_000L, 0, 100, 123_456, 234_567, "user request after error: ANR", listOf("\"main\" prio=5", "at a.b(c.kt:1)"))
        val text = report(record(), exit)
        assertEquals("2026-11-18T21:20:00Z, app 0.1.0 (100), thread main", valueOf(text, "crash.last"))
        assertEquals("java.lang.IllegalStateException: boom", valueOf(text, "crash.trace.1"))
        assertEquals("at a.B.c(B.kt:1)", valueOf(text, "crash.trace.2"))
        assertEquals(
            "anr at 2026-11-18T21:21:00Z, status 0, importance 100, pss 123456 kB, rss 234567 kB, description user request after error: ANR",
            valueOf(text, "exit.last"),
        )
        assertEquals("\"main\" prio=5", valueOf(text, "exit.trace.1"))
        assertEquals("at a.b(c.kt:1)", valueOf(text, "exit.trace.2"))
        for (key in REPORT_KEYS) assertEquals(key, 1, text.lines().count { it.startsWith("$key: ") })
        // Every line after the header is still one key: value.
        for (line in text.lines().drop(1).filter { it.isNotEmpty() }) assertTrue(line, Regex("^[a-z0-9_.]+: .+$").matches(line))
    }

    @Test
    fun withoutACrashOrExitTheReportSaysNoneOrNa() {
        val none = report(null, null)
        assertEquals("none", valueOf(none, "crash.last"))
        assertEquals("none", valueOf(none, "exit.last"))
        assertFalse(none, "crash.trace" in none || "exit.trace" in none)
        // Below API 30 Android records no exits.
        assertEquals("n/a", valueOf(report(null, null, exitsKnown = false), "exit.last"))
        val noDescription = report(null, ExitRecord(13, 0L, 0, 400, 0, 0, null, emptyList()))
        assertEquals(
            "other at 1970-01-01T00:00:00Z, status 0, importance 400, pss 0 kB, rss 0 kB, description none",
            valueOf(noDescription, "exit.last"),
        )
    }

    @Test
    fun theExitReasonsAreNamed() {
        assertEquals("crash", exitReasonName(4))
        assertEquals("crash_native", exitReasonName(5))
        assertEquals("anr", exitReasonName(6))
        assertEquals("low_memory", exitReasonName(3))
        assertEquals("reason 99", exitReasonName(99))
    }

    // --- ANR traces ---

    @Test
    fun anAnrExcerptStartsAtTheMainThreadAndEndsWithItsStack() {
        val trace = listOf("----- pid 123 at 2026-11-19 -----", "Cmd line: io.github.spippoli.hipsterismo", "") +
            "\"main\" prio=5 tid=1 Sleeping" + (1..3).map { "  at a.B.f$it(B.kt:$it)" } + "" +
            "\"mosaic-preview\" prio=10 tid=20 Native" + "  at c.D.g(D.kt:1)"
        assertEquals(
            listOf("\"main\" prio=5 tid=1 Sleeping", "  at a.B.f1(B.kt:1)", "  at a.B.f2(B.kt:2)", "  at a.B.f3(B.kt:3)"),
            anrExcerpt(trace.asSequence()),
        )
        val deep = listOf("\"main\" prio=5") + (1..100).map { "  at f$it" }
        val cut = anrExcerpt(deep.asSequence())
        assertEquals(TRACE_LINES, cut.size)
        assertEquals("... 62 more lines", cut.last())
        // Without a main thread, the top.
        val top = anrExcerpt((1..100).map { "line $it" }.asSequence())
        assertEquals("line 1", top.first())
        assertEquals("... 61 more lines", top.last())
    }

    // --- native tombstones ---

    private class Proto {
        val out = ByteArrayOutputStream()
        fun varint(v: Long): Proto {
            var x = v
            while (true) {
                if (x and 0x7fL.inv() == 0L) { out.write(x.toInt()); return this }
                out.write(((x and 0x7f) or 0x80).toInt())
                x = x ushr 7
            }
        }
        fun num(field: Int, v: Long) = varint((field shl 3).toLong()).varint(v)
        fun bytes(field: Int, b: ByteArray): Proto {
            varint(((field shl 3) or 2).toLong()).varint(b.size.toLong())
            out.write(b)
            return this
        }
        fun str(field: Int, s: String) = bytes(field, s.toByteArray())
        fun msg(field: Int, m: Proto) = bytes(field, m.out.toByteArray())
        fun build(): ByteArray = out.toByteArray()
    }

    private fun frame(relPc: Long, file: String, function: String?) = Proto().num(1, relPc).num(2, relPc + 0x7000000000).apply {
        if (function != null) str(4, function).num(5, 24)
    }.str(6, file)

    private fun tombstone(): ByteArray {
        val signal = Proto().num(1, 6).str(2, "SIGABRT").num(3, -6).str(4, "SI_TKILL").num(8, 0)
        val crashing = Proto().num(1, 4321).str(2, "mosaic-preview")
            .msg(4, frame(0x5c3a8, "/apex/com.android.runtime/lib64/bionic/libc.so", "abort"))
            .msg(4, frame(0x1234, "/data/app/~~x==/io.github-1==/lib/arm64/libhipsterismo.so", null))
        val other = Proto().num(1, 1234).str(2, "main").msg(4, frame(0x99, "/system/lib64/libutils.so", "f"))
        return Proto()
            .num(1, 3).str(2, "google/akita/akita:17/AP4A/123:user/release-keys").num(5, 1234).num(6, 4321)
            .msg(10, signal).str(14, "core: bad Frame")
            .msg(16, Proto().num(1, 1234).msg(2, other))
            .msg(16, Proto().num(1, 4321).msg(2, crashing))
            .msg(17, Proto().str(1, "/data/app/x/base.apk"))
            .build()
    }

    private val expected = listOf(
        "signal 6 (SIGABRT), code -6 (SI_TKILL)",
        "abort message: core: bad Frame",
        "thread mosaic-preview (tid 4321)",
        "#00 pc 000000000005c3a8  libc.so (abort+24)",
        "#01 pc 0000000000001234  libhipsterismo.so",
    )

    @Test
    fun aTombstoneGivesTheSignalAndTheCrashingThreadsFrames() {
        assertEquals(expected, tombstoneExcerpt(tombstone()))
    }

    @Test
    fun aCutTombstoneKeepsItsFirstFieldsAndGarbageGivesNull() {
        val whole = tombstone()
        // Cut inside the memory mappings, after the threads.
        assertEquals(expected, tombstoneExcerpt(whole.copyOf(whole.size - 5)))
        assertNull(tombstoneExcerpt(ByteArray(0)))
        assertNull(tombstoneExcerpt(byteArrayOf(-1, -1, -1)))
        assertNull(tombstoneExcerpt("not a tombstone at all".toByteArray()))
    }
}
