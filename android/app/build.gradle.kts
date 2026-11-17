// The Android app: Kotlin (AGP's built-in Kotlin), Compose, and the shared
// adaptive core through JNI (src/main/cpp/). Debug builds only, installed by
// hand with adb; arm64-v8a only, for the Pixel 8a. The debug build is its own
// package, io.github.spippoli.hipsterismo.debug ("Hipsterismo debug"), so it
// can sit next to a release install (#98).
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
        versionCode = 1
        versionName = "0.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    // The launcher label goes through the manifest's ${appLabel} placeholder.
    buildTypes {
        getByName("debug") {
            applicationIdSuffix = ".debug"
            manifestPlaceholders["appLabel"] = "Hipsterismo debug"
        }
        getByName("release") {
            manifestPlaceholders["appLabel"] = "Hipsterismo"
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
