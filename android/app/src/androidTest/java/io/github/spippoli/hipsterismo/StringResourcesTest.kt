package io.github.spippoli.hipsterismo

import android.content.Context
import android.content.res.Configuration
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.Locale

/**
 * The UI in English and Italian (#93, #100): every string of values/ has its
 * Italian translation, a phone in another language gets English, and every
 * string with arguments formats in both languages.
 */
@RunWith(AndroidJUnit4::class)
class StringResourcesTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    private fun contextFor(tag: String): Context {
        val configuration = Configuration(context.resources.configuration)
        configuration.setLocale(Locale.forLanguageTag(tag))
        return context.createConfigurationContext(configuration)
    }

    /** Every string of the app, by name. */
    private val strings: Map<String, Int> =
        R.string::class.java.fields.associate { it.name to it.getInt(null) }

    /**
     * The strings that read the same in both languages: the app's own terms
     * (Mosaic, Tessel, Min patch area, LIVE, px²), and "Zoom %1$s".
     */
    private val sameInBoth = setOf("live_fps", "live_stats", "area_px2", "min_patch_area", "result_title", "a11y_zoom")

    @Test
    fun everyStringHasItsItalianTranslation() {
        val en = contextFor("en")
        val it = contextFor("it")
        assertTrue("found only ${strings.keys}", strings.keys.containsAll(sameInBoth + "a11y_shoot"))
        // A key missing from values-it/ falls back to English, so an Italian
        // string equal to the English one is a missing translation.
        for ((name, id) in strings) {
            if (name in sameInBoth) {
                assertEquals(name, en.getString(id), it.getString(id))
            } else {
                assertNotEquals("$name has no Italian translation", en.getString(id), it.getString(id))
            }
        }
        assertEquals("Take photo", en.getString(R.string.a11y_shoot))
        assertEquals("Scatta", it.getString(R.string.a11y_shoot))
    }

    @Test
    fun anotherLanguageFallsBackToEnglish() {
        val en = contextFor("en")
        for (tag in listOf("de", "fr", "it-CH")) {
            val other = contextFor(tag)
            val expected = if (tag.startsWith("it")) contextFor("it") else en
            for ((name, id) in strings) assertEquals("$name in $tag", expected.getString(id), other.getString(id))
        }
    }

    @Test
    fun everyStringWithArgumentsFormats() {
        var formatted = 0
        for (tag in listOf("en", "it")) {
            val localized = contextFor(tag)
            for ((name, id) in strings) {
                val raw = localized.getString(id)
                val conversions = conversionsOf(raw)
                assertEquals("$name in $tag", conversionsOf(contextFor("en").getString(id)), conversions)
                if (conversions.isEmpty()) continue
                val args = conversions.map { argumentFor(it) }.toTypedArray()
                val text = localized.getString(id, *args)
                assertTrue("$name in $tag left a placeholder: $text", '%' !in text)
                formatted++
            }
        }
        assertTrue("only $formatted strings with arguments", formatted >= 2 * 5)
    }

    /** The conversion of each argument of a format string, by position ("d", "f", "s"). */
    private fun conversionsOf(format: String): List<Char> {
        val byPosition = sortedMapOf<Int, Char>()
        var next = 1
        for (match in SPECIFIER.findAll(format)) {
            val conversion = match.groupValues[2].last()
            if (conversion == '%' || conversion == 'n') continue
            val position = match.groupValues[1].takeIf { it.isNotEmpty() }?.toInt() ?: next++
            byPosition[position] = conversion
        }
        return byPosition.values.toList()
    }

    private fun argumentFor(conversion: Char): Any = when (conversion) {
        'd' -> 42
        'f' -> 12.34
        's' -> "1.4×"
        else -> throw AssertionError("no test argument for %$conversion")
    }

    private companion object {
        val SPECIFIER = Regex("""%(?:(\d+)\$)?([-#+ 0,(]*\d*(?:\.\d+)?[a-zA-Z%])""")
    }
}
