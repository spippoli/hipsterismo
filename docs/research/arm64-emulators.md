# Emulators for an arm64-only APK on this x86_64 machine

Research for [#87](https://github.com/spippoli/hipsterismo/issues/87), part of map #85. Date: 2026-11-16. Host: Linux x86_64, 8 threads, 14 GB RAM, `/dev/kvm` readable and writable by the user (ACL). SDK under `~/Android/Sdk`, emulator 37.2.13.

## Answer

- **arm64 system images do not run on this host.** The emulator refuses them (string in the `emulator` binary 37.2.13): *"Avd's CPU Architecture '%s' is not supported by the QEMU2 emulator on x86_64 host. System image must match the host architecture."* The release notes mention arm64 images only for arm64 hosts (30.0.26) [2].
- **x86_64 Google APIs and Play images with ARM translation do run it.** They ship `libndk_translation.so` as the native bridge and list `arm64-v8a` among their ABIs. Our APK installs on them, `libhipsterismo.so` loads, NEON included, and the app and the instrumented tests run. Translation is per process; ART and the GL/Vulkan libraries stay x86 [1].
- **Use API 34–37 as the matrix.** These are the images whose repository manifest declares `translatedAbis arm64-v8a`. API 36 was tried here: all 18 instrumented tests pass, golden hashes included, and `core-on-device.sh` matches the desktop on every fixture.
- **API 30 runs the app, but its translator gives other floats.** `fingers.png` and the synthetic 2048 px still get different hashes, so 5 of 18 tests fail. It is fine for a launch smoke test, not for the golden tests.
- **API 29 is out.** CameraX can't bind Preview, ImageCapture and ImageAnalysis together on an emulator at Android 10 or lower [5], and that is exactly what `PreviewPipeline` binds. Android 10 x86_64 images also have no ARM translation (see below).
- **The camera works headless.** `hw.camera.back=virtualscene` feeds CameraX a rendered 3D room: RGBA analysis at 320×240 (portrait 240×320 after rotation), Preview and ImageCapture at 1280×960. The live Mosaic and a shutter photo both worked on API 30 and 36. `emulated` (a synthetic pattern) and `webcam0` are the other options [4][6]. This host has a webcam that the emulator lists as `webcam0` (YUYV). It was not tried.

## Matrix

| API | x86_64 image with translation | arm64 `.so` | Golden hashes | CameraX (P + IC + IA) | Verdict |
|---|---|---|---|---|---|
| 29 | none: Android 10 x86_64 images have no ARM translation [2]; not in the manifest | no | – | no, at API ≤ 29 on an emulator [5] | **not possible** |
| 30 | `google_apis` / `google_apis_playstore` (release notes [2]; manifest silent) | **tried: yes** | **tried: NO** (2 of 4 fixtures differ) | **tried: yes** | smoke test only |
| 31–33 | `google_apis` / `_playstore`: manifest silent, release notes silent | not tried | not tried | yes [5] | unknown, likely as API 30 |
| 34 | `google_apis` / `_playstore` (manifest: `translatedAbis arm64-v8a`) | not tried | not tried | yes | likely good |
| 35 | as 34, also `_ps16k` (16 KB pages) | not tried | not tried | yes | likely good |
| 36 | as 35 | **tried: yes** | **tried: yes** (4 of 4) | **tried: yes** | **good** |
| 36.1, 37.0 | as 35 | not tried | not tried | yes | likely good |
| 37.1, 37.2 | only `_ps16k` variants | not tried | not tried | yes | likely good (16 KB pages) |

The AOSP `default`, `aosp_atd` and `google_atd` images declare no translated ABI in the manifest, so they can't run an arm64-only APK. The ARM translation is limited to Google APIs and Play Store images, for development and debugging only, and not for commercial hosted services [1]. That excludes ATDs, which Gradle Managed Devices otherwise suggest for tests [3].

Manifest data: `https://dl.google.com/android/repository/sys-img/{google_apis,google_apis_playstore,android,aosp_atd,google_atd}/sys-img2-4.xml`, field `<translatedAbis>`. Only `google_apis` and `google_apis_playstore` have it: API 34, 35, 36, 36.1 and 37.x, plus their extension and `_ps16k` variants. The API 30 image doesn't declare it, yet it has `ro.product.cpu.abilist=x86_64,x86,arm64-v8a,armeabi-v7a,armeabi` and `ro.dalvik.vm.native.bridge=libndk_translation.so`, so the manifest is not exhaustive for older images.

## What was tried

APK from `./gradlew assembleDebug` at `origin/master` 0d3a804 (arm64-v8a only: `libhipsterismo.so`, CameraX's `libimage_processing_util_jni.so` and `libsurface_util_jni.so`, `libandroidx.graphics.path.so`). The emulator was headless (`-no-window -no-audio -no-snapshot -gpu swiftshader_indirect -port 5554`). Every adb and Gradle call named `emulator-5554` (`ANDROID_SERIAL`), and the phone was not touched.

### API 36 (`system-images;android-36;google_apis;x86_64` r7, AVD `hip36`, `pixel_8a`, 4 GB)

- Cold boot 49 s, 21 s later. ABIs `x86_64,arm64-v8a`, native bridge `libndk_translation.so`.
- **App:** `am start` cold 5.5 s. The live Mosaic of the virtual scene runs at 1–3 fps, `core 300–900 ms` per 240×320 Frame (22–24 ms on the Pixel 8a). The shutter gave `photo camera 960x1280: 7838 ms to the result (capture 1276, decode 222, core 6021 = 77%, other 319), 2051 Tessels`. The result screen shows the Mosaic.
- **`connectedDebugAndroidTest`:** 18/18 pass in 48 s. `CorePerformanceTest`: median 42 ms (plain), 40 ms (ADPF hint), so even the 15 fps floor passes. A 4624×3472 JPEG decodes in about 52 ms and runs the core in 1.8–2.7 s. In the test, the core with no camera running is about 10× faster than in the live preview, where the virtual scene's software rendering competes for the 4 vCPUs (not investigated further).
- **`core-on-device.sh`** (ANDROID_SERIAL=emulator-5554): all four hashes match the desktop, 33/33 checks.

### API 30 (`system-images;android-30;google_apis;x86_64` r16, AVD `hip30`, `pixel_4`, 4 GB)

- Cold boot 30 s. ABIs `x86_64,x86,arm64-v8a,armeabi-v7a,armeabi`, native bridge `libndk_translation.so`.
- **App:** launches (cold 3.8 s) and the live Mosaic runs, `core 480–660 ms`.
- **`connectedDebugAndroidTest`:** 13/18 pass. Failures:
  - `NativeMosaicTest` ×3: `fingers.png` hashes `0x830e0c0bcf1425fb`, not `0xe0d145733a499498`.
  - `CorePerformanceTest`: median 89 / 75 ms, under the 15 fps floor.
  - `MosaicExportTest.condividiWritesToTheCacheAndNotToTheGallery`: `NoSuchMethodError` on `Intent.getParcelableExtra(String, Class)`, an API 33 method called at `MosaicExportTest.kt:144` and `:147`. This is a test bug for any device below API 33 (minSdk is 29). `IntentCompat.getParcelableExtra` would fix it.
- **`core-on-device.sh`:** `fingers.png` → `0x830e0c0bcf1425fb` (1373 Tessels, 277 Contours, 110 Patches, against 1392/276/110), `fingers-2048x1536-synthetic` → `0x0a367f30bbcde1d9` (2719 Tessels against 2436). `fingers-003.png` and `fingers-060.png` match. 31/33 checks.
  - So Android 11's translator doesn't reproduce some arm64 float result bit for bit. The likely suspects are the fused multiply-adds in `gaussianBlur()` (NEON `vfmaq`), which the core relies on to be correctly rounded. Not narrowed down here. The API 36 translator does reproduce them.
  - The release notes record later fixes to `libndk_translation` (for example 35.6.11, a crash in `Arm64AesEncode` [2]), and the translator is part of the system image, so the emulator version doesn't change it.

## Camera details

- **AVD keys:** `hw.camera.back` / `hw.camera.front` in `~/.android/avd/<name>.avd/config.ini`. The values are `emulated` and `virtualscene` (software-generated) or `webcamN` (the host's webcam) [4]. `avdmanager` defaults to back `emulated` and front `none`; we set back `virtualscene` and front `emulated`, so the switch button has two cameras.
- **Since Android 11** the emulated camera also offers level 3, RAW, YUV reprocessing, logical cameras and sensor orientation from the sensor manager [6]. Since 36.6.11 there is an "environment" camera mode with backgrounds from `environment.ini` [2].
- **What CameraX did here:** it bound all three use cases on camera id 10 (the virtual scene) with no errors, at Preview 1280×960, ImageAnalysis 320×240 and ImageCapture 1280×960. The camera's largest 4:3 size is 1280×960, so the photo path never reaches the 2048 px cap from the camera. Gallery photos (the fixtures, or files pushed to MediaStore) can.
- **Webcam:** `emulator -webcam-list` lists `webcam0` (`Integrated_Webcam_HD`, YUYV). The release notes require 720p capture for API ≥ 27 images [2]. Not tried.
- **Smoke-test images:** the virtual scene is deterministic only up to its default pose, so a smoke test should assert "a Mosaic arrives" (fps > 0, Tessels > 0) and not a hash. The golden hashes belong to `NativeMosaicTest` on the fixtures.

## What got installed where

All of it is still installed. Remove it with `sdkmanager --uninstall …` and `avdmanager delete avd -n …`.

| What | Where | Download | On disk |
|---|---|---|---|
| Emulator 37.2.13 | `~/Android/Sdk/emulator/` | (one zip) | 863 MB |
| `system-images;android-36;google_apis;x86_64` r7 | `~/Android/Sdk/system-images/android-36/google_apis/x86_64/` | 1.90 GB | 4.3 GB |
| `system-images;android-30;google_apis;x86_64` r16 | `~/Android/Sdk/system-images/android-30/google_apis/x86_64/` | 1.44 GB | 3.3 GB |
| AVD `hip36` (pixel_8a, 4 GB RAM, back virtualscene, front emulated) | `~/.android/avd/hip36.avd` | – | 1.8 GB after runs |
| AVD `hip30` (pixel_4, same cameras) | `~/.android/avd/hip30.avd` | – | 0.8 GB after runs |

- The other images' download sizes, from the manifest: API 34 `google_apis` 1.56 GB, 35 1.74 GB, 37.0 2.14 GB, 37.2 `_ps16k` 2.43 GB; Play Store variants about the same.
- Each image unpacks to about 2.3× its download.
- A four-level matrix (34, 35, 36, 37) costs about 8 GB of downloads and about 18 GB on disk, plus about 1–2 GB per AVD.
- `avdmanager create avd` prints `Could not load devices from …/devices.xml` with these images but creates the AVD anyway.

### Commands

```sh
export JAVA_HOME=~/Android/jdk-21 ANDROID_HOME=~/Android/Sdk
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --install emulator "system-images;android-36;google_apis;x86_64"
echo no | $ANDROID_HOME/cmdline-tools/latest/bin/avdmanager create avd -n hip36 -k "system-images;android-36;google_apis;x86_64" -d pixel_8a
sed -i -e 's/^hw.camera.back=.*/hw.camera.back=virtualscene/' -e 's/^hw.camera.front=.*/hw.camera.front=emulated/' -e 's/^hw.ramSize=.*/hw.ramSize=4G/' ~/.android/avd/hip36.avd/config.ini
$ANDROID_HOME/emulator/emulator -avd hip36 -no-window -no-audio -no-snapshot -no-boot-anim -gpu swiftshader_indirect -port 5554 &
export ANDROID_SERIAL=emulator-5554   # never the phone
until [ "$($ANDROID_HOME/platform-tools/adb shell getprop sys.boot_completed | tr -d '\r')" = 1 ]; do sleep 3; done
$ANDROID_HOME/platform-tools/adb install -r -g android/app/build/outputs/apk/debug/app-debug.apk
(cd android && ./gradlew connectedDebugAndroidTest)
android/scripts/core-on-device.sh
$ANDROID_HOME/platform-tools/adb emu kill
```

## Open points for the follow-up (#91)

- **The API 30 hash mismatch:** decide whether the golden tests are skipped below API 34 on emulators or the matrix starts at 34. Covering minSdk 29 still needs the phone, or a physical or cloud arm64 device.
- **`MosaicExportTest` below API 33:** it uses `getParcelableExtra(String, Class)`, which doesn't exist there; switch to `IntentCompat`.
- **`CorePerformanceTest`'s 15 fps floor** is about the phone and fails under translation on API 30. Skip it on emulators (`Build.FINGERPRINT` contains `generic`/`emu`, or `ro.kernel.qemu`) or make it log-only there.
- **API 31–33, 34 and 37** were not tried. Gradle Managed Devices [3] could drive the matrix with `systemImageSource = "google"`, but not the ATD images, which have no translation.

## Sources

1. Android Developers Blog, "Run ARM apps on the Android Emulator" (2020-03): https://android-developers.googleblog.com/2020/03/run-arm-apps-on-android-emulator.html. It covers per-process translation, keeping ART and GLES/Vulkan native, Google APIs and Play images only, and the licence limits.
2. Emulator release notes: https://developer.android.com/studio/releases/emulator.
   - 30.0.0: the x86_64 image includes the ARM64 ABI, and Android 9 and 11 images support ARM by default.
   - 30.0.26: arm64 images are for arm64 hosts.
   - 35.6.11: the `libndk_translation` `Arm64AesEncode` fix.
   - 27.1.10: webcams need 720p.
   - 36.6.11: the environment camera mode.
3. Gradle Managed Devices / ATD: https://developer.android.com/studio/test/gradle-managed-devices.
4. Create and manage virtual devices, AVD camera property: https://developer.android.com/studio/run/managing-avds.
5. CameraX codelab: https://developer.android.com/codelabs/camerax-getting-started. *"Implementing preview, image capture, and image analysis concurrently will not work for Android Studio's device emulator if you are running Android 10 or lower."*
6. Emulator camera: https://developer.android.com/studio/run/emulator-use-camera, for the Android 11+ camera capabilities and the virtual scene.
7. SDK repository manifests, `sys-img2-4.xml` per tag (see Matrix).
8. Local: the `emulator` binary's strings, the images' `build.prop` and `getprop`, and the test and script output above.
