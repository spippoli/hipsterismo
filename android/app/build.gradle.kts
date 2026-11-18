// The Android app: Kotlin (AGP's built-in Kotlin), Compose, and the shared
// adaptive core through JNI (src/main/cpp/). arm64-v8a only. The debug build
// is its own package, io.github.spippoli.hipsterismo.debug ("Hipsterismo
// debug"), so it can sit next to a release install (#98). The release build is
// minified by R8 and signed from four environment variables (#105).
plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "io.github.spippoli.hipsterismo"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "io.github.spippoli.hipsterismo"
        minSdk = 29
        targetSdk = 37
        // -PversionCode=… -PversionName=… set them for a release (#105).
        versionCode = providers.gradleProperty("versionCode").orNull?.toInt() ?: 1
        versionName = providers.gradleProperty("versionName").orNull ?: "0.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    // The release key comes from four environment variables. If any is
    // missing, there is no release signing config and assembleRelease builds
    // an unsigned APK. v3 next to the default v2 (minSdk 29 gets only v2)
    // keeps key rotation possible.
    val releaseSigning = listOf(
        "RELEASE_KEYSTORE_PATH",
        "RELEASE_STORE_PASSWORD",
        "RELEASE_KEY_ALIAS",
        "RELEASE_KEY_PASSWORD",
    ).map { providers.environmentVariable(it).orNull?.takeIf(String::isNotEmpty) }
    if (releaseSigning.all { it != null }) {
        signingConfigs.create("release") {
            storeFile = file(releaseSigning[0]!!)
            storePassword = releaseSigning[1]
            keyAlias = releaseSigning[2]
            keyPassword = releaseSigning[3]
            enableV2Signing = true
            enableV3Signing = true
        }
    }

    // The instrumented tests stay on debug: R8 removes the Mosaic getters
    // that the test APK calls (#88).
    testBuildType = "debug"

    // The launcher label goes through the manifest's ${appLabel} placeholder.
    buildTypes {
        getByName("debug") {
            applicationIdSuffix = ".debug"
            manifestPlaceholders["appLabel"] = "Hipsterismo debug"
        }
        // CMake builds the release native code as RelWithDebInfo (-DNDEBUG,
        // so CDT's asserts are off); the -O3 of src/main/cpp/ comes later and
        // wins.
        getByName("release") {
            manifestPlaceholders["appLabel"] = "Hipsterismo"
            // R8, with the keep rules of src/main/keepRules/ (the Mosaic
            // constructor that the JNI calls).
            optimization {
                enable = true
            }
            signingConfigs.findByName("release")?.let { signingConfig = it }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    // The fixtures of the core tests are the app's assets: fingers.png is the
    // Frame it shows, and the instrumented tests hash all three.
    sourceSets {
        getByName("main") {
            assets.srcDir("../../tests/fixtures")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    buildFeatures {
        compose = true
    }

    // English in values/, Italian in values-it/ (#93). The generated locale
    // config (res/resources.properties says values/ is English) lists both in
    // Android 13's per-app language settings. The libraries' other
    // translations are left out, so on a phone in German even Compose's own
    // TalkBack words are English, like the rest of the UI.
    androidResources {
        generateLocaleConfig = true
        localeFilters += listOf("en", "it")
    }

    // A string without its Italian translation fails lint, and so the release
    // build, whose lint-vital runs the fatal checks.
    lint {
        fatal += "MissingTranslation"
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.core)
    implementation(libs.androidx.compose.foundation)
    implementation(libs.androidx.compose.ui)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.androidx.camera.core)
    implementation(libs.androidx.camera.camera2)
    implementation(libs.androidx.camera.lifecycle)
    implementation(libs.androidx.camera.view)

    testImplementation(libs.junit)

    androidTestImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.test.runner)
}
