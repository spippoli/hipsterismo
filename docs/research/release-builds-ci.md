# Research: signed release builds of the Android app in GitHub Actions

Ticket: #88 (part of map #85). Question: what does a signed release build of the Android app need, built by GitHub Actions? This covers the `release` build type in AGP 9.5.1, signing from secrets, R8 with the JNI surface, `NDEBUG`/`-O3` for the core, the toolchain on `ubuntu-latest`, tag-triggered releases with `gh`, version numbers from the tag, signature schemes, keystore backup and loss, and which tests can run against which build.

Researched 2026-11-16 against the repo at `0d3a804` (master) and the primary sources cited inline. Claims marked **measured** come from a local experiment: a scratch copy of `android/`, `src/` and `tests/fixtures/`, built with `./gradlew assembleRelease` (AGP 9.5.1, JDK 21, NDK r30, CMake 3.31.6) and signed with a throwaway 4096-bit RSA keystore. No keystore was committed. No phone was attached, so nothing was run on the device: **the release APK has not been launched yet**.

---

## TL;DR

1. **R8 breaks the JNI glue unless you add one keep rule.** `mosaic_jni.cpp` builds `io.github.spippoli.hipsterismo.Mosaic` with `FindClass` + `GetMethodID("<init>", "(II[F[I[FJF)V")`. Kotlin never calls that constructor, so R8 sees the class as never instantiated. In the measured release APK, `Mosaic` became a `PUBLIC ABSTRACT` class with **no fields and no methods**. Every Frame would then fail with `NoSuchMethodError`. The default rules keep `NativeMosaic`'s `native` methods and the classes in their signatures (`-keepclasseswithmembernames,includedescriptorclasses`), but only the class *name*, not its constructor. The fix, verified by dexdump:
   ```
   -keep class io.github.spippoli.hipsterismo.Mosaic {
       <init>(int, int, float[], int[], float[], long, float);
   }
   ```
   With this rule R8 keeps the constructor with that exact descriptor and still renames and prunes the fields (it drops `contours` and `hash`, which the app never reads). That is fine, because JNI only calls the constructor.
2. **Use AGP 9.3+'s `optimization { enable = true }` DSL** in `buildTypes.release`, with the rule in `app/src/main/keepRules/jni.keep`. This includes `proguard-android-optimize.txt` and resource shrinking [r8-enable] [keep-overview]. **Measured** on 9.5.1: it builds and gives the same `Mosaic` as the legacy `isMinifyEnabled` + `isShrinkResources` + `proguardFiles(...)`.
3. **`NDEBUG` comes for free, and `-O3` stays.** AGP builds the release variant's CMake as `RelWithDebInfo`. The NDK passes `-O2 -g -DNDEBUG` and the existing `target_compile_options(... -O3)` comes later and wins (**measured**, `compile_commands.json`). CDT uses `<cassert>`, so its asserts go. The goldens already hold with `NDEBUG`: `tests/core/CMakeLists.txt` sets `CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG"`, and all 33 checks pass (**measured**). AGP strips the `.so` in the APK (`file` says `stripped`).
4. **Signing from secrets:** add a `release` `signingConfig` that reads four environment variables (keystore path, store password, alias, key password). In CI, store the keystore as a base64 secret: 4.2 KB, 5.6 KB as base64, well under the 48 KB secret limit [gh-secrets]. Decode it into `$RUNNER_TEMP`, which is cleared at the start and end of each job [gh-vars]. AGP's default for minSdk 29 is **v2 only** (**measured**, `apksigner verify`: v1 false, v2 true, v3 false). Add `enableV3Signing = true` to keep key rotation possible later.
5. **The runner needs one install.** `ubuntu-latest` (Ubuntu 24.04, image 20261111) already has JDK 21 (`JAVA_HOME_21_X64`), `platforms;android-37.0`, build-tools 37.0.0 and **NDK `30.0.16248370`**. It does **not** have CMake 3.31.6, only 3.31.5 and 4.1.2 [runner-image]. Install it with `sdkmanager "cmake;3.31.6"`. Use `actions/setup-java` with `temurin` 21 and `gradle/actions/setup-gradle@v6` for the Gradle caches.
6. **Trigger on `push: tags: ['v*']`** and attach the APK with `gh release create "$GITHUB_REF_NAME" app-release.apk --verify-tag --generate-notes`, with `GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}` and `permissions: contents: write` [gh-cli] [gh-release]. Also attach `mapping.txt`, which you need to retrace crashes and which is overwritten on every build [r8-troubleshoot].
7. **Version from the tag:** `versionName` is the tag without the `v`. `versionCode` must grow strictly (Android refuses downgrades) and stay ≤ 2 100 000 000 [versioning]. Derive it as `major·10000 + minor·100 + patch` from a strict `vMAJOR.MINOR.PATCH` tag, and fail the build on any other tag.
8. **Keystore loss is permanent.** Outside Play App Signing, "if you lose your app's signing key, you lose the ability to update your app" [signing]. Android only allows an update signed with the same certificate, so users would have to uninstall. Keep two offline copies of the keystore and its passwords, separate from the GitHub secret. Give the key ≥ 25 years of validity [signing].
9. **Tests:** the **instrumented tests cannot run against the R8 release APK.** With `testBuildType = "release"` the test APK builds (**measured**), but it calls `Mosaic.getHash()`, `getContours()`, `getVertices()`, `getCoreMs()` and more, which R8 removed from the app. They would fail with `NoSuchMethodError`. Keep `connectedDebugAndroidTest` on the debug build, as today. The core tests and `core-on-device.sh` already test the core as the release build compiles it (`-O3 -DNDEBUG`, the same `adaptive_core` target and flags). What is left is a smoke test of the release APK itself: install it and run the camera, photo, save and share paths of `android/TESTING.md`.

---

## 1. The `release` build type (AGP 9.5.1)

The current `android/app/build.gradle.kts` has no `buildTypes` and no `signingConfigs`. The debug APK is signed with `~/.android/debug.keystore` [signing].

### 1.1 Shape that was built and verified

```kotlin
android {
    defaultConfig {
        // ...
        versionCode = providers.gradleProperty("versionCode").map(String::toInt).getOrElse(1)
        versionName = providers.gradleProperty("versionName").getOrElse("0.1")
    }

    signingConfigs {
        create("release") {
            storeFile = providers.environmentVariable("HIPSTERISMO_KEYSTORE").orNull?.let(::file)
            storePassword = providers.environmentVariable("HIPSTERISMO_KEYSTORE_PASSWORD").orNull
            keyAlias = providers.environmentVariable("HIPSTERISMO_KEY_ALIAS").orNull
            keyPassword = providers.environmentVariable("HIPSTERISMO_KEY_PASSWORD").orNull
            enableV3Signing = true
        }
    }

    buildTypes {
        release {
            optimization { enable = true }   // AGP ≥ 9.3: R8 + resource shrinking + default rules
            signingConfig = signingConfigs.getByName("release")
        }
    }
}
```

Plus `app/src/main/keepRules/jni.keep` with the rule from the TL;DR.

Notes:

- The experiment used `System.getenv()`. `providers.environmentVariable()` is the configuration-cache-friendly form (the repo has `org.gradle.configuration-cache=true`). Either way the secrets are inputs to configuration, so they can end up in the configuration cache entry. In CI, **pass `--no-configuration-cache` to the release build**. setup-gradle also stores configuration-cache entries only if you give it a `cache-encryption-key` [setup-gradle]. The local experiment ran with `--no-configuration-cache`. Whether the release config works with the configuration cache on was **not tested**.
- When the variables are missing (local `assembleRelease` without secrets), AGP fails at `validateSigningRelease`. That is the right behaviour for a release. `assembleDebug` is unaffected.
- `optimization {}` and the `src/<variant>/keepRules/*.keep` source set are new in AGP 9.3. The legacy `isMinifyEnabled`/`isShrinkResources`/`proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), …)` still works [r8-enable] [keep-overview]. Since AGP 9.0, optimized resource shrinking is on whenever resource shrinking is, and `proguard-android.txt` (with `-dontoptimize`) is no longer supported. Since 9.1, R8 repackages classes by default [r8-enable].
- The app's own classes other than `Mosaic` need no rules. `FrameTransform` is plain Kotlin passed through Kotlin; the JNI receives only its ints. `NativeMosaic`'s `external fun`s are kept by the default `-keepclasseswithmembernames,includedescriptorclasses class * { native <methods>; }` (seen in the merged `configuration.txt`). That rule also keeps the descriptor classes' *names* (`Mosaic`, `Bitmap`, `ByteBuffer`), which is why `Mosaic` kept its name but lost its members. The JNI symbol names `Java_io_github_spippoli_hipsterismo_NativeMosaic_fromBitmap` and `_fromPixels` match the kept names, so no `RegisterNatives` is needed. Google's docs name JNI callbacks from native code into Kotlin as a case that needs keep rules [keep-overview].
- A narrower alternative to the hand-written rule is the Jetpack Keep annotations, which generate the rule from an annotation on the constructor [keep-overview]. That adds a Gradle plugin, so for one constructor the `.keep` file is simpler.
- **Guard against drift.** If `mosaicOf()`'s descriptor or `Mosaic`'s constructor changes, the keep rule silently stops matching. The cheapest check is a CI step after `assembleRelease` that runs `dexdump` on the APK and greps `Mosaic`'s `<init>` with `(II[F[I[FJF)V` (that is how it was verified here). A comment next to `GetMethodID` and next to the constructor pointing at `jni.keep` helps too.

### 1.2 Native code in release

**Measured**, from `app/.cxx/RelWithDebInfo/…/compile_commands.json`:

```
AdaptiveCore.cpp: … -O2 -g -DNDEBUG -std=c++20 -fPIC -Wall -Wextra -ffp-contract=off -O3 -pthread
mosaic_jni.cpp:   … -O2 -g -DNDEBUG -std=c++20 -fPIC -Wall -Wextra -O3 -pthread
```

- `CMAKE_BUILD_TYPE=RelWithDebInfo` for the release variant. `-DNDEBUG` turns off CDT's `assert`s (`src/mosaic/CDT/CDT.h` includes `<cassert>`). `-ffp-contract=off` stays, since it comes from the core's own target. The last `-O3` wins over `-O2`.
- `NDEBUG` cannot change the hashes: an `assert` evaluates a condition and has no other effect. CLAUDE.md's "the core tests keep CDT's asserts on" is out of date, by the way: `tests/core/CMakeLists.txt` sets `CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG"`, and the build type defaults to Release. So the goldens, and `core-on-device.sh`'s phone hashes, already come from `NDEBUG` builds. Rebuilt here, 33/33 checks pass, including the 2048×1536 synthetic golden `0x17305ab000cbc3a4`.
- CLAUDE.md says `NDEBUG` saves about 3% of core time. Release gets that with no change to `CMakeLists.txt`.
- `libhipsterismo.so` in the APK is stripped (1.2 MB). AGP keeps the unstripped copy under `app/build/intermediates/`. Attaching the unstripped `.so` (or `native-debug-symbols.zip`) to the release would let you symbolize native crashes. That is optional for a hand-installed APK.

### 1.3 Signature schemes

- **Measured** with `apksigner verify -v`: v1 false, v2 true, v3/v3.1/v4 false. AGP's `enableV1Signing`…`enableV4Signing` are nullable, and "if null, a default value is used" [apk-signing-config]. For minSdk 29 that default is v2 only.
- v2 is enough to install on API 29+. v3 adds a proof-of-rotation lineage, so a later update can move to a new key, from Android 9 on. Older platforms ignore v3 and verify v2 [apksig-v3]. Rotation itself needs `apksigner rotate` and `--lineage` outside AGP's DSL, and Google does not recommend relying on it for API ≤ 31 [apksig-v3]. Recommendation: **`enableV3Signing = true`**, so a v3 block is there from the first release. It costs nothing and keeps rotation open. v1 is pointless at minSdk 29. v4 is only for incremental adb installs.

## 2. Keystore: creating, storing, losing

- Create it once, locally, never in CI: `keytool -genkeypair -keystore hipsterismo-release.jks -alias hipsterismo -keyalg RSA -keysize 4096 -validity 10000`. 10 000 days is about 27 years, and the docs ask for ≥ 25 years [signing]. Google Play would require validity past 22 October 2033 [signing]. Play is not a target, but this already satisfies it.
- **Loss means no updates.** "If you lose access to your app signing key … you will not be able to release new versions of your app to users as updates to the original app" and "you cannot regenerate a previously generated key" [signing]. The system installs an update only if the certificates match. Otherwise the app needs a different package name and installs as a separate app [signing]. For this app, that means users uninstall and reinstall, losing nothing important: the Mosaics are in `Pictures/Hipsterismo` and survive an uninstall as the user's media.
- **Backup:** the GitHub secret is write-only and cannot be read back, so it is not a backup. Keep the `.jks` and the passwords in two places outside GitHub (a password manager, plus an offline copy).
- **Compromise:** whoever has the key and passwords can ship updates that replace the app [signing]. Keep the secrets in a GitHub **environment** (for example `release`) restricted to tag refs, so a workflow on another branch can't read them. Secrets are never passed to workflows triggered from forks [gh-secrets].
- **The installed debug build.** The phone today runs the debug-signed APK. Installing the release APK over it fails with a signature mismatch, so uninstall it first. That also drops the camera grant, as `connectedDebugAndroidTest` already does. To keep both on the phone, give debug `applicationIdSuffix = ".debug"`. The FileProvider authority already uses `${applicationId}`, but CLAUDE.md's `am start -n io.github.spippoli.hipsterismo/.MainActivity` and `TESTING.md`'s commands would need the suffix for debug.

## 3. The workflow on `ubuntu-latest`

### 3.1 What the runner already has

Per the Ubuntu 24.04 image readme (image `20261111.370.1`) [runner-image]:

| Needed | On the image | Action |
|---|---|---|
| JDK 21 | 17 default, 21 at `JAVA_HOME_21_X64` | `actions/setup-java` `temurin` 21, or `JAVA_HOME=$JAVA_HOME_21_X64` |
| `platforms;android-37.0` | yes (`android-37.0`) | none |
| build-tools 37.0.0 | yes | none |
| NDK `30.0.16248370` | yes (`ANDROID_NDK_LATEST_HOME`; the default `ANDROID_NDK` is 27.3) | none: `ndkVersion` in Gradle picks it |
| CMake 3.31.6 | **no** (3.31.5, 4.1.2) | `sdkmanager "cmake;3.31.6"` |
| `ANDROID_HOME` | `/usr/local/lib/android/sdk` | none |

`android-actions/setup-android@v4` (inputs `packages`, `cmdline-tools-version`, `accept-android-sdk-licenses`) is only needed when the image's SDK is missing something it can't install [setup-android]. The image's own `sdkmanager` suffices here. Image contents change over time: the job should fail loudly, not silently pick another NDK. `ndkVersion` and `cmake.version` are already pinned in `build.gradle.kts`, so a missing version is a build error.

### 3.2 Caching

- `gradle/actions/setup-gradle@v6` caches the Gradle User Home (dependencies, transforms, wrapper) and validates the wrapper jar by default [setup-gradle]. `actions/setup-java`'s `cache: gradle` is the simpler alternative, keyed on the Gradle files and `gradle/*.versions.toml` [setup-java].
- **The catch for tag-only workflows:** setup-gradle writes the cache only on the default branch, and other refs only restore it [setup-gradle]. A workflow that runs only on tags never saves a cache. Either accept cold builds (about 2 minutes locally, cold), or run the same build also on `push` to `master` so it warms the cache, which also catches R8 breakage before a tag.
- CMake 3.31.6 from `sdkmanager` is a small download. It can go in `actions/cache` keyed on the version, but that isn't worth the YAML.

### 3.3 Sketch (not added to the repo)

```yaml
name: android-release
on:
  push:
    tags: ['v*']
permissions:
  contents: write            # gh release create
jobs:
  release:
    runs-on: ubuntu-latest
    environment: release     # holds the four secrets, limited to tags
    defaults: { run: { working-directory: android } }
    steps:
      - uses: actions/checkout@v5
      - uses: actions/setup-java@v5
        with: { distribution: temurin, java-version: '21' }
      - uses: gradle/actions/setup-gradle@v6
      - run: yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" "cmake;3.31.6"
      - name: Version from tag
        run: |
          [[ "$GITHUB_REF_NAME" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || { echo "tag must be vX.Y.Z"; exit 1; }
          echo "VERSION_NAME=${GITHUB_REF_NAME#v}" >> "$GITHUB_ENV"
          echo "VERSION_CODE=$(( BASH_REMATCH[1]*10000 + BASH_REMATCH[2]*100 + BASH_REMATCH[3] ))" >> "$GITHUB_ENV"
      - name: Keystore
        env: { KEYSTORE_B64: '${{ secrets.HIPSTERISMO_KEYSTORE_B64 }}' }
        run: base64 -d <<< "$KEYSTORE_B64" > "$RUNNER_TEMP/release.jks"
      - name: Build
        env:
          HIPSTERISMO_KEYSTORE: ${{ runner.temp }}/release.jks
          HIPSTERISMO_KEYSTORE_PASSWORD: ${{ secrets.HIPSTERISMO_KEYSTORE_PASSWORD }}
          HIPSTERISMO_KEY_ALIAS: ${{ secrets.HIPSTERISMO_KEY_ALIAS }}
          HIPSTERISMO_KEY_PASSWORD: ${{ secrets.HIPSTERISMO_KEY_PASSWORD }}
        run: ./gradlew --no-configuration-cache test assembleRelease -PversionCode="$VERSION_CODE" -PversionName="$VERSION_NAME"
      - name: Check the APK
        run: |
          BT="$ANDROID_HOME/build-tools/37.0.0"
          APK=app/build/outputs/apk/release/app-release.apk
          "$BT/apksigner" verify --print-certs "$APK"
          for d in $(unzip -Z1 "$APK" 'classes*.dex'); do unzip -p "$APK" "$d" > "$RUNNER_TEMP/$d"; done
          "$BT/dexdump" "$RUNNER_TEMP"/classes*.dex | grep -A40 "hipsterismo/Mosaic;'" | grep -q "(II\[F\[I\[FJF)V"
      - name: Release
        env: { GH_TOKEN: '${{ secrets.GITHUB_TOKEN }}' }
        run: |
          cp app/build/outputs/apk/release/app-release.apk "hipsterismo-$VERSION_NAME.apk"
          cp app/build/outputs/mapping/release/mapping.txt "mapping-$VERSION_NAME.txt"
          gh release create "$GITHUB_REF_NAME" "hipsterismo-$VERSION_NAME.apk" "mapping-$VERSION_NAME.txt" --verify-tag --generate-notes
```

Sources for the pieces: tag filters for `push` [gh-events], `GITHUB_REF_NAME` ("the short ref name of the branch or tag that triggered the workflow run") and `RUNNER_TEMP` [gh-vars], `gh` preinstalled and needing `GH_TOKEN` [gh-cli], `gh release create <tag> <files>… --verify-tag --generate-notes` [gh-release], base64 for small binary secrets [gh-secrets]. `setup-gradle@v6` is the major in its docs [setup-gradle]. The `checkout` and `setup-java` majors are **unverified**: check each README's current major when writing the workflow, and pin to commit SHAs if you want to harden it. The `dexdump` grep is a sketch: check it against a real APK before relying on it.

### 3.4 Version numbers

- `versionCode` is a positive integer. The system refuses an APK with a lower `versionCode` than the installed one, and Play caps it at 2 100 000 000 [versioning]. `versionName` is a free string shown to users [versioning].
- From `vMAJOR.MINOR.PATCH`: `versionCode = MAJOR·10000 + MINOR·100 + PATCH`. That is monotonic as long as MINOR and PATCH stay < 100, readable, and reproducible from the tag alone. `GITHUB_RUN_NUMBER` is monotonic too [gh-vars], but it says nothing about the version and resets if the workflow file is renamed.
- Pass them as `-P` Gradle properties, read with `providers.gradleProperty(…)`, with today's 1 / "0.1" as the local default. Bake the debug build's values in the same way, so a release never has a lower code than a debug build from before it. The current `versionCode = 1` means the first tag must give ≥ 1 (v0.1.0 → 100).

## 4. Which tests run against which build

| Check | Runs on | Covers the release build? |
|---|---|---|
| `tests/core` (`core_tests`) | desktop, CI-able | **Yes for the core**: same `adaptive_core` target and flags, `-O3 -DNDEBUG` |
| `core-on-device.sh` | phone | Yes for the core on arm64 (NDK clang, `NDEBUG`), not through the app |
| `./gradlew test` (JVM) | CI-able | Unaffected by R8 (it runs unminified classes). Run it in the release job |
| `connectedDebugAndroidTest` | phone | **Debug only.** See below |
| R8 + JNI keep check | CI-able | Yes: `dexdump` on the release APK (§1.1) |
| `android/TESTING.md` by hand | phone | Yes, if done with the release APK: the only end-to-end proof |

- **Instrumented tests against release don't work as-is** (**measured**). With `testBuildType = "release"`, `assembleReleaseAndroidTest` builds. But the test APK calls `Mosaic.getColors/getContours/getCoreMs/getHash/getHeight/getTesselCount/getVertices/getWidth`, and the minified app's `Mosaic` has only the constructor and five renamed fields. They would fail at run time. Making them pass would need keep rules for everything the tests touch (`Mosaic`'s getters, `PhotoDecoder`, `MosaicExport`, `FrameTransform`, …), which changes the code that ships. Google's own advice is to enable optimization only in the release build, not for tests, and to check the release build's critical user journeys with UI Automator or benchmarks [r8-enable] [r8-test].
- So: keep `connectedDebugAndroidTest` on debug as the functional suite. Before each tag, install the release APK from the Release and run the camera, shutter, gallery, Salva and Condividi sections of `android/TESTING.md`. Those are the paths that cross JNI (`fromFrame`, `fromBitmap`) and the export. If that hand pass gets old, a small UI Automator or macrobenchmark module against a minified non-debuggable build type is the next step [r8-test]. That is a separate ticket.
- GitHub-hosted runners have no Pixel 8a, and the instrumented tests need a real camera and `MediaStore`. CI can run `tests/core`, `./gradlew test` and the release build with its checks. The phone-side checks stay manual.

## 5. Open points for the spec

- Launch the release APK on the Pixel 8a once with the keep rule: no device was attached during this research.
- Whether to add `applicationIdSuffix = ".debug"` (changes CLAUDE.md/TESTING.md commands) or just uninstall the debug build before installing a release.
- Whether the build also runs on `push` to `master` (warms the Gradle cache, catches R8 breakage early) or only on tags.
- Fix CLAUDE.md's claim that the core tests keep CDT's asserts on (they build with `-DNDEBUG`).

---

## Sources

- [signing] Android Developers, "Sign your app": https://developer.android.com/studio/publish/app-signing
- [apk-signing-config] AGP DSL `ApkSigningConfig` (9.0): https://developer.android.com/reference/tools/gradle-api/9.0/com/android/build/api/dsl/ApkSigningConfig
- [apksig-v3] AOSP, "APK Signature Scheme v3": https://source.android.com/docs/security/features/apksigning/v3
- [r8-enable] Android Developers, "Enable app optimization" (AGP/R8 version table, `optimization {}` DSL): https://developer.android.com/topic/performance/app-optimization/enable-app-optimization
- [keep-overview] Android Developers, "Keep rules overview" (JNI, `src/<variant>/keepRules`): https://developer.android.com/topic/performance/app-optimization/keep-rules-overview
- [r8-troubleshoot] Android Developers, "Test and troubleshoot the optimization" (`NoSuchMethodError`, `mapping.txt`, retrace): https://developer.android.com/topic/performance/app-optimization/test-and-troubleshoot-the-optimization
- [r8-test] Android Developers, "Test the optimization": https://developer.android.com/topic/performance/app-optimization/test-the-optimization
- [versioning] Android Developers, "Version your app": https://developer.android.com/studio/publish/versioning
- [runner-image] actions/runner-images, Ubuntu 24.04 readme: https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md
- [setup-java] actions/setup-java README: https://github.com/actions/setup-java
- [setup-gradle] gradle/actions, setup-gradle docs: https://github.com/gradle/actions/blob/main/docs/setup-gradle.md
- [setup-android] android-actions/setup-android README: https://github.com/android-actions/setup-android
- [gh-secrets] GitHub Docs, "Using secrets in GitHub Actions": https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-secrets
- [gh-vars] GitHub Docs, "Variables reference": https://docs.github.com/en/actions/reference/workflows-and-actions/variables
- [gh-events] GitHub Docs, "Events that trigger workflows": https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows
- [gh-cli] GitHub Docs, "Using GitHub CLI in workflows": https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-github-cli
- [gh-release] GitHub CLI manual, `gh release create`: https://cli.github.com/manual/gh_release_create
- Repo: `android/app/src/main/cpp/mosaic_jni.cpp` (`FindClass`/`GetMethodID` at lines 95–97), `android/app/src/main/java/io/github/spippoli/hipsterismo/{NativeMosaic,Mosaic}.kt`, `android/app/src/main/cpp/CMakeLists.txt`, `src/mosaic/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`.
