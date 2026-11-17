package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ThreadPriorityTest {
    @Test
    fun anAllowedChangeReportsNothing() {
        val refused = mutableListOf<RuntimeException>()
        var raised = 0
        assertTrue(raisePriority({ raised++ }, { refused += it }))
        assertEquals(1, raised)
        assertEquals(emptyList<RuntimeException>(), refused)
    }

    @Test
    fun aRefusedChangeIsReportedOnceAndNotThrown() {
        for (error in listOf(SecurityException("no"), IllegalArgumentException("bad priority"))) {
            val refused = mutableListOf<RuntimeException>()
            assertFalse(raisePriority({ throw error }, { refused += it }))
            assertEquals(listOf(error), refused)
        }
    }
}
