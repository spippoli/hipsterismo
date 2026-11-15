# openFrameworks on Android: can hipsterismo build for a Pixel 8a?

Research for #49 (map #48). Checked on 2026-11-15 against the openFrameworks repo (`master` and tag `0.12.1`), the apothecary repo, the ofxCv repo, and the local install at `/home/dario/Applications/of` (0.12.1, Linux release). Nothing was installed or built. Every claim below comes from source files, PRs, CI runs or release assets. None of it was tested on a device.

## Verdict

**Viable but not cheap. The OF Android route needs current `master` and a git checkout of OF, plus our own Java/JNI code for everything the app needs beyond a preview.**

- **Build:** a command-line Linux build (`./gradlew assembleDebug`) is supported on `master`. The upstream CI builds the core and all 22 Android examples on `ubuntu-24.04` with NDK r30 on every push, and the job passes today. 0.12.1 as released has no Linux-host Android package, and the local Linux install has no Android core project. So a second OF tree (a git checkout of `master`) is needed. Upstream has never run a device test of the r30 pin.
- **Addons:** ofxOpenCv (OpenCV 4.14.0) and ofxGui are wired into the template and built in CI. ofxCv, ofxDelaunay and ofxMSAInterpolator are plain C++, but the template's Gradle script compiles only `addon/src/**`, so ofxCv's `libs/ofxCv/src` and ofxDelaunay's `libs/Delaunay/src` are not built. That needs a custom `CMakeLists.txt`. The Mosaic module itself only needs ofxCv/OpenCV plus vendored CDT.
- **Camera:** `ofVideoGrabber` works through `ofxAndroidVideoGrabber` with the deprecated Camera1 API (`android.hardware.Camera`), which delivers preview frames only. There is **no** photo capture, no gallery picker and no photo-picker support in ofxAndroid. Full-resolution shutter photos and gallery loading have to be written in Java/Kotlin, through intents or the photo picker, and passed to C++ over JNI.
- **Cost/risk:** medium-high. Expect a few days to get a hipsterismo-shaped APK building (custom CMake for the addons, pinning to an OF `master` commit), plus the Java glue for capture, picker, save and share. The OF Android port has one active maintainer (Dan Rosser). Its UI is GL-only, so the slider and screens would be ofxGui or hand-drawn. Its camera code has been unchanged since 2021. The other two routes on the map would need that same Java glue too. Only the OF route adds the OF toolchain and its single-maintainer risk on top.

## 1. Supported NDK, Gradle, CMake, SDK

| Item | 0.12.1 docs (local install) | `master` (2026-11-15) |
|---|---|---|
| NDK | 28.0.13004108 | **r30, `30.0.16248370`** (pinned in template, core project, `cmake.sh`, CI) |
| Android Gradle Plugin | 8.9.0 | 8.9.3 |
| Gradle wrapper | 8.11.1 | 8.14.3 |
| CMake (`externalNativeBuild`) | 3.22.1 (docs: install 3.31.6 or later) | 3.22.1 (docs: 3.31.6 or later) |
| Build tools | 35.0.0 | 37.0.0 |
| compileSdk / targetSdk | 34 / 34 | 34 / 34 |
| minSdk | 24 | 25 |
| C++ standard (template) | C++23, libc++ (`c++_shared`) | C++23, libc++ |
| JDK | "latest" | CI uses Temurin 17 |
| ABIs | arm64-v8a, armeabi-v7a, x86_64 | same |

Sources:
- Local `docs/android_studio.md` and `scripts/templates/android/{build.gradle,gradle/wrapper/gradle-wrapper.properties,ofApp/build.gradle,ofApp/src/CMakeLists.txt}` in `/home/dario/Applications/of`.
- `master` versions of the same files: https://github.com/openframeworks/openFrameworks/blob/master/docs/android_studio.md, https://github.com/openframeworks/openFrameworks/blob/master/scripts/templates/android/ofApp/build.gradle
- PR #8575 (merged 2026-10-10): NDK r29, min API 25, build-tools 37, and `cmake.sh` now uses the requested NDK instead of the newest one installed. https://github.com/openframeworks/openFrameworks/pull/8575
- PR #8629 (merged 2026-10-30): pins NDK r30 and builds every `examples/android` project in CI. It says "No device install." https://github.com/openframeworks/openFrameworks/pull/8629

The docs' own troubleshooting section says an NDK other than the pinned one "will not match this tree". The pin moved twice in October 2026, so pin our build to a specific OF commit.

The map's minSdk 26–29 is above the template's 25, so we can raise it. targetSdk 34 is fine for a sideloaded debug APK.

## 2. Building from this Linux machine on the command line

- The docs say: "Can build directly in folder with `./gradlew assembleDebug`". On a git checkout, run `scripts/android/download_libs.sh` first, then generate the project with the Project Generator. Source: `docs/android_studio.md` on `master`.
- CI job `build-android` in `.github/workflows/of.yml` runs on `ubuntu-24.04`. The steps are `./of update libs android`, then `gradle wrapper && ./gradlew assembleDebug` in `libs/openFrameworksCompiled/project/android`, then `./cmake.sh $ARCH`, then `scripts/ci/android/build_examples.sh`. https://github.com/openframeworks/openFrameworks/blob/master/.github/workflows/of.yml
- On `master` run 43696949062 (2026-11-15, push), all three `build-android` jobs (arm64-v8a, x86_64, armeabi-v7a) succeeded.
- **0.12.1 release assets** include `of_v0.12.1_android_macos_release.tar.gz` and `of_v0.12.1_android_windows_release.zip`, but **no Linux Android package**. The nightly has the same two (`of_v20261115_android_{macos,windows}_release`). https://github.com/openframeworks/openFrameworks/releases/tag/0.12.1
- **The local install is the Linux release.** It has `addons/ofxAndroid` (C++ and Java sources) and `scripts/templates/android`, but **no** `libs/openFrameworksCompiled/project/android` (the template's `checkAndBuildOF` task calls its `cmake.sh`), no `examples/android`, no `scripts/android`, and no Android OpenCV libs (`addons/ofxOpenCv/libs/opencv/lib/` only has `emscripten`). It cannot build Android as it is. We would need a git checkout of OF `master` alongside it, or in its place. The app's `Makefile` expects `OF_ROOT=../../..`, and the Android template's `ofRoot()` uses that same depth.
- Prebuilt libraries come from apothecary. The `latest` Android tarballs (arm64, armv7, x86, x86_64) were refreshed on 2026-11-15, and a `v12.2.0` release with Android tarballs appeared the same day. https://github.com/openframeworks/apothecary/releases

## 3. Addons on Android

How the template handles addons (`master` `scripts/templates/android/ofApp/build.gradle`):
- It reads `addons.make` and adds `addon/src` plus every `addon/libs/*/include` to the include path.
- It compiles `.cpp`/`.c` files **only under `addon/src`**, recursively.
- It links `.a`/`.so` files from `addon/libs/*/lib/android/<abi>`.
- It does **not** read `addon_config.mk`: no `ADDON_DEPENDENCIES`, no excludes and no per-platform sources. Upstream issue #5965, "Android ADDON_SOURCES_EXCLUDE not working", is still open from 2018.

| Addon | Status on Android | Notes |
|---|---|---|
| ofxAndroid | Required, core of the port | Window, lifecycle, grabber, permissions, share helper. |
| ofxOpenCv / OpenCV 4 | Supported | apothecary builds OpenCV **4.14.0** for `android` (formula `opencv.sh`, `VER=4.14.0`, Java bindings off). `addon_config.mk` lists `libs/opencv/lib/android/<abi>/*.a`. PR #8629 wires ofxOpenCv into the template, and `androidOpenCVExample` builds in CI. |
| ofxGui | Supported | Wired into the template by #8629, and `androidGuiExample` builds in CI. Fixed recently: #8574 (closed 2026-10-13), where Android `ofPath` fills were corrupted by a tess2 index-width mismatch. |
| ofxCv | No Android config, plain C++ over OpenCV 4 | The repo's last commit is from 2020-07. Its `addon_config.mk` has no android section. Its sources are in `libs/ofxCv/src`, which the template **does not compile**, and `libs/CLD/include/CLD` needs adding as an include path by hand. Old issue #252 "contour order is flipped on android" is closed. https://github.com/kylemcdonald/ofxCv |
| ofxDelaunay | Plain C++ | `libs/Delaunay/src/Delaunay.cpp` is not compiled by the template. Our patch still applies. Only needed for spline/stable, which stay desktop-only. |
| ofxMSAInterpolator / ofxMSACore | Plain C++ (header-only plus `MSACoreGL.cpp`) | Only needed for spline mode, which stays desktop-only. The `#pragma GCC diagnostic` guard in `Mosaic.h` also works under NDK clang. |
| ofxXmlSettings | Plain C++ | `libs/tinyxml*.cpp` is not compiled by the template. Probably not needed on the phone. |

Adaptive mode needs only `ofMain`, `ofxCv` (hence ofxOpenCv) and the vendored CDT in `src/mosaic/CDT`. So the hard addon dependency is ofxCv, and getting it built means listing its sources in our own `CMakeLists.txt`, or patching the generated `build.gradle`. That is a small, mechanical fix, but it means our Android project leaves the stock template. Restrict `abiFilters` to `arm64-v8a` for the Pixel 8a.

## 4. Camera, photos, gallery

**Preview through `ofVideoGrabber`:**
- `ofxAndroidVideoGrabber` (C++) and `OFAndroidVideoGrabber.java` use **`android.hardware.Camera` (Camera1)**, `Camera.PreviewCallback` and NV21 preview frames. Frames are converted to RGB, RGB565 or GRAY in C++ (`ConvertYUV2RGB`).
- The Java file was last changed on 2021-09-18. No Camera2 or CameraX path exists.
- Camera1 has been deprecated since API 21, but it still works on current Pixels through the HAL shim: https://developer.android.com/reference/android/hardware/Camera
- `androidCameraExample` shows the use. It calls `grabber.setup(1280, 720)`, recommends `OF_PIXELS_GRAY` because colour conversion is "heavy", and handles camera orientation and facing by hand (`getCameraOrientation()`, `getFacingOfCamera()`). https://github.com/openframeworks/openFrameworks/blob/master/examples/android/androidCameraExample/src/ofApp.cpp
- 320×240 is a standard Camera1 preview size, so the live-preview Frame size from #48 is reachable. Whether ≥ 15 fps adaptive Mosaics hold still has to be measured on the phone.

**Runtime permission:** `ofxAndroidRequestPermission()` and `ofxAndroidCheckPermission()` exist (`ofxAndroidUtils.h`, backed by `ActivityCompat.requestPermissions` in `OFAndroid.java`). The template manifest does not declare `CAMERA`; the docs say to add it yourself.

**Full-resolution photo (shutter):** not provided. ofxAndroid never calls `Camera.takePicture`, `ACTION_IMAGE_CAPTURE` or Camera2/CameraX capture. Options are:
- extend `OFAndroidVideoGrabber.java` with `takePicture()`;
- fire an `ACTION_IMAGE_CAPTURE` intent and handle the result in `OFActivity.onActivityResult`. `OFAndroidObject.onActivityResult(int, int, Intent)` is the existing hook;
- open the camera with CameraX in our own Java code, alongside OF.

**Gallery:** not provided. There is no `ACTION_PICK`, `ACTION_GET_CONTENT` or Photo Picker (`PickVisualMedia`) anywhere in ofxAndroid. We would write it in Java, read the `content://` URI into bytes, and hand them to C++ with JNI. `ofxAndroidUtils.h` has generic JNI call helpers. Then decode with `ofLoadImage(ofBuffer)` and apply the EXIF rotation ourselves.

**Save and share:** `OFAndroid.java` has a share helper. It uses `MediaStore.Images.Media.insertImage` (deprecated since API 29) plus an `ACTION_SEND` chooser, and `ACTION_MEDIA_SCANNER_SCAN_FILE`, which is also deprecated. It is usable as a starting point. A clean Android 14 implementation would use `MediaStore` `ContentValues`/`RELATIVE_PATH` in our own Java.

## 5. Known breakages and port vitality

**Commits:**
- Recent Android work on `libs/openFrameworksCompiled/project/android`:
  - 2026-10-30 (#8629, NDK r30 and example CI);
  - 2026-10-10 (#8575);
  - 2025-07-21 (x86_64 emulator fixes, tess2);
  - 2025-03-20 (the CMake "auto build system for core", Linux support).
- Recent work on `addons/ofxAndroid`:
  - 2026-10-10 (#8533, OpenGL ES 3.0/3.1);
  - 2026-08-15 (#8541, zip path-traversal fix for #8509);
  - 2025-07-21 (Android video player);
  - 2025-03 (the "android2025" merge, sound fixes).
- Nearly all of it is by one maintainer, Dan Rosser (danoli3).

**History:** Android CI was switched off in 2023 (#7427, "Android is so out of date right now…"). It was rebuilt in 2025–2026 and now passes on every `master` push. In a July 2025 comment on #7427, a user asks what state Android is in on 0.12.1, because the website guide is gone. The official https://openframeworks.cc/setup/android-studio/ page is still the old one, citing NDK r15c. The repo doc is the current one.

**Open Android issues:** at least 25 match "android" (the query was capped at 25). Most are from 2012–2021 and stale: FBO bugs #5183, #6023 and #6295, #2995 "Android File I/O is Wonky", #5965 addon excludes, and #6762 "Bring Android up to date with latest NDK", which the 2025–26 work has effectively superseded. Recently closed: #8574 (tess2/ofPath on Android, 2026-10-13), #8509 (zip traversal, 2026-08-15) and #8395 (ofCubeMap on Android, 2025-03-28).

**Known gaps that matter for us:**
- no Linux-host release package;
- the template does not compile addon `libs/*/src`, or read `addon_config.mk`;
- a Camera1-only grabber, with no capture or picker;
- the troubleshooting docs still mention editing `/addons/ofxAndroid/ofAndroidLib/build.gradle`, a path that no longer exists, so parts of the docs are stale;
- no upstream device test of the r30 toolchain (#8629: "No device install"). #8575 was tested on an Android 14 ARM64 emulator only.

**Forum:** `forum.openframeworks.cc` returns HTTP 403 to automated clients ("Crawler is not allowed!"), so no forum threads from 2024–2026 were checked. That is a gap in this research.

## 6. Cost and risk for this project

| Work item | Estimate | Risk |
|---|---|---|
| Git checkout of OF `master` next to the 0.12.1 install, pinned to a commit, plus `download_libs.sh`, SDK/NDK r30 install, JDK 17 | 0.5 day | Low to medium. The pin moves often. |
| Android project for hipsterismo: generate from template, raise minSdk, arm64 only, custom CMake adding ofxCv `libs/ofxCv/src` and CLD includes, share `src/mosaic/` | 1–2 days | Medium. The project leaves the stock template, and the desktop Makefile and Android CMake must share sources (#48 "Sharing the core"). |
| Live preview through `ofVideoGrabber` at 320×240, orientation, CAMERA permission | 0.5–1 day | Medium. Camera1 is deprecated, and orientation is handled by hand. |
| Shutter photo, gallery picker, EXIF, save, share, in Java plus JNI | 2–4 days | Medium to high. None of it exists in ofxAndroid. The same work is needed on the native-app route. |
| UI (slider, screens) in GL with ofxGui or hand-drawn, touch-sized | 1–2 days | Medium. There are no native Android widgets unless we mix Views over `OFGLSurfaceView`. |

**Compared with the other routes on #48:** the OF route reuses `src/mosaic/` as it is, with `ofPixels`, `ofMesh` and ofxCv. Its drawing is the same GL code as on the desktop. The price is a large toolchain, a single-maintainer port and our own Java for every Android-specific feature. A native app over an OF-free core also needs the core freed from `ofPixels`/`ofMesh`/ofxCv. In return it gets CameraX, the photo picker and native UI, all supported by Google.
