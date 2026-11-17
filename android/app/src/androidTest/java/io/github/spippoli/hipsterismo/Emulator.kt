package io.github.spippoli.hipsterismo

import android.os.Build

/**
 * Whether the tests run on an Android emulator, and what that means for them.
 * The x86_64 `google_apis` images run the arm64-only APK through their ARM
 * translator (#87): fine for the logic, but too slow for the phone's
 * performance floor, and on API 30 the translator changes the core's floats,
 * so its Mosaics don't hash to the goldens.
 */
object Emulator {
    /**
     * True on the emulator's `ranchu` (or older `goldfish`) virtual hardware,
     * or a Google emulator image (`sdk_gphone64_x86_64` and the like).
     */
    val isEmulator: Boolean =
        Build.HARDWARE == "ranchu" || Build.HARDWARE == "goldfish" || Build.PRODUCT.startsWith("sdk_gphone")

    /**
     * True where the core's floats are those of an arm64 CPU: every phone,
     * and the emulator images from API 34 on, whose translator gives the
     * goldens (API 36 checked in #87). API 30's does not; 31–33 are untried
     * and treated like 30.
     */
    val keepsGoldenHashes: Boolean = !isEmulator || Build.VERSION.SDK_INT >= 34
}
