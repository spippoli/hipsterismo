# Pixel-only assumptions in the app, and CameraX on other phones

Research for #86 (part of #85). The question: which assumptions in the Android app hold only on the Pixel 8a, and what CameraX 1.6 does on other phones from API 29.

Sources are of two kinds. **Code** means files under `android/app/src/main/` at `origin/master` (0d3a804). **Primary** means Android or AndroidX documentation, AOSP source, or the AndroidX source on `androidx-main`, quoted below. The CameraX quirk files come from `androidx-main` (November 2026), so some may be newer than the 1.6.2 the app ships. Nothing here was run on a phone other than the Pixel 8a. Where a claim is an estimate, it says so.

## Summary

| # | Assumption | Holds elsewhere? | Breaks where | Severity |
|---|---|---|---|---|
| 1 | arm64-v8a only (`abiFilters`) | No | 32-bit-only phones (many Android Go devices) can't install it; x86 emulators can't run it | Medium: install fails, no crash |
| 2 | ADPF keeps the thread on a fast core (20 ms target) | Partly | API 29–30 have no ADPF; on other SoCs the session may be null or may not migrate the thread | Low: handled, but slower |
| 3 | Core speed (22 ms per Frame, about 0.5 s per photo) | No | Phones with only Cortex-A55-class cores: estimated 3–4× slower | Medium on low-end phones: UX only |
| 4 | `THREAD_PRIORITY_DISPLAY` | Yes | AOSP gives every process RLIMIT_NICE 40; not guarded, though | None in practice |
| 5 | ImageAnalysis RGBA_8888 at 320×240 4:3, packed rows | Mostly | 320×240 isn't guaranteed: fallback goes higher (often 640×480); row padding is possible | Low: handled; bigger Frame costs about 4× the core time |
| 6 | Preview + ImageCapture + ImageAnalysis bound together | Yes | Guaranteed on LEGACY (PRIV PREVIEW + YUV PREVIEW + JPEG MAXIMUM); `bindToLifecycle` not guarded | Low, but an unguarded crash if it ever throws |
| 7 | ImageCapture `MINIMIZE_LATENCY`, largest 4:3 | Yes | On 50–200 MP phones, "largest" can be a big JPEG: slower decode (still capped at 2048) | Low |
| 8 | `isReversedHorizontal` mirrors front-camera photos through EXIF, applied by `ImageDecoder` | Expected | Verified only on the Pixel (Android 17); API 29 behaviour not tested | Low |
| 9 | Ultrawide zoom chip (`minZoom < 1`) | No | No `CONTROL_ZOOM_RATIO_RANGE` before API 30; many phones expose the ultrawide as a separate camera | None: degrades to 1×/2× or no chips |
| 10 | A back and a front camera | Mostly | Front-only handled; **external-only (USB) cameras crash**; `hasCamera()` exceptions not caught | Low (rare devices) |
| 11 | Display P3 photos, sRGB decode, F16/1010102 copy | Yes | `setTargetColorSpace(SRGB)` works everywhere from API 28; Samsung HEIF needs a device HEVC decoder | Low |
| 12 | Portrait lock (`screenOrientation="portrait"`) | No | targetSdk 37: ignored on displays ≥ sw600dp (tablets, unfolded foldables) with **no opt-out** | Medium on large screens: layout only |
| 13 | Photo picker | Yes | API 29 (and devices without Play system updates) fall back to `ACTION_OPEN_DOCUMENT` | None |
| 14 | `drawVertices` on the hardware renderer | Yes | Hardware-accelerated `drawVertices()` arrived in API 29, the app's minSdk | None |
| 15 | Device quirks (Samsung, Xiaomi, others) | n/a | CameraX works around them automatically; none needs app code | None found that needs app code |

The only crash paths found are #6 and #10: `bindToLifecycle()` and `hasCamera()` are called with no try/catch. Everything else degrades: no install (#1), a slower preview (#2, #3, #5), fewer zoom chips (#9), or a landscape layout on large screens (#12).

## 1. ABI: arm64-v8a only

**Code.** `android/app/build.gradle.kts`: `ndk { abiFilters += "arm64-v8a" }`, commented "arm64-v8a only, for the Pixel 8a". The core's `convolve()` has AVX2, NEON and scalar versions (CLAUDE.md), so the code itself isn't tied to arm64.

**Where it breaks.** A phone with a 32-bit-only ABI list (`ro.product.cpu.abilist` without `arm64-v8a`) can't install the APK. That covers many Android Go devices, which often run a 32-bit userland on 64-bit cores. x86_64 emulators can't run it either. This is a packaging choice, not a bug, and it costs an `abiFilters += "armeabi-v7a"`, plus a check that the NEON path builds for 32-bit ARM, if those phones matter.

## 2. ADPF hint session and the X3

**Code.** `PreviewPipeline.reportWork()`: on API 31+ it calls `createHintSession(intArrayOf(Process.myTid()), 20 ms)`, then `reportActualWorkDuration()` per Frame. The 20 ms target is derived from the X3's 17.8 ms (#53).

**Primary.** `PerformanceHintManager` was added in API level 31. `createHintSession` returns "the new session if it is supported on this device, null if hint session is not supported on this device or the tid doesn't belong to the application" ([reference](https://developer.android.com/reference/android/os/PerformanceHintManager)).

**Where it breaks.**
- API 29–30: no ADPF at all. The code skips it (`SDK_INT < S`).
- API 31+ without an ADPF HAL: the session is null. The code logs "no ADPF" and goes on.
- On other SoCs with ADPF, the scheduler's response to the hint is vendor policy. The 20 ms target is tuned for one core: on a phone whose big core takes, say, 30 ms, the session reports a miss on every Frame, which is harmless.

So nothing breaks, but the fast-core placement the app counts on for 30 fps exists only where the vendor honours the hint.

## 3. Core speed and the thread count

**Code and CLAUDE.md.**
- The X3 runs 22–24 ms per camera Frame and an A715 about 26 ms.
- A 2048 px photo takes about 0.5 s of core on the X3.
- `AdaptiveParallel.h` caps the threads at `maxThreads = 5` ("the caller and four A715 cores").

**Where it breaks.** These figures come from the G3's cores (one X3 and four A715s). The estimate below is not measured. A phone with only Cortex-A55-class cores (common on Android Go and low-end devices) runs this integer- and branch-heavy code about 3–4× slower than an A715. That means roughly 80–100 ms per Frame, about 10 fps, and 2–3 s per photo, above `android/TESTING.md`'s 1.5 s target. Nothing fails, and the Mosaic is the same because the output doesn't depend on the CPU or the thread count (CLAUDE.md). Mid-range phones with A76/A78 big cores should land near the A715 numbers.

## 4. `THREAD_PRIORITY_DISPLAY`

**Code.** The `mosaic-preview` thread calls `Process.setThreadPriority(THREAD_PRIORITY_DISPLAY)` (nice −4) in its thread factory, with no try/catch.

**Primary.** AOSP's `system/core/rootdir/init.rc` runs `setrlimit nice 40 40` ("set RLIMIT_NICE to allow priorities from 19 to -20"), and every process inherits it ([init.rc](https://android.googlesource.com/platform/system/core/+/refs/heads/main/rootdir/init.rc)). So any app may set −4 on an AOSP-derived build.

**Where it breaks.** It doesn't, unless an OEM changes that rlimit. If it ever threw `SecurityException`, the executor's thread factory would fail on every task. That is a theoretical risk only.

## 5. ImageAnalysis: RGBA_8888 at 320×240, 4:3

**Code.**
- The analysis use case: `setOutputImageFormat(OUTPUT_IMAGE_FORMAT_RGBA_8888)`, `ResolutionStrategy(320×240, FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER)`, `RATIO_4_3_FALLBACK_AUTO_STRATEGY`, `STRATEGY_KEEP_ONLY_LATEST`.
- `FrameTransform.of()` takes any `rowStride ≥ width·pixelStride` and `pixelStride ≥ 4`.
- The JNI checks that the four corners lie inside the buffer.
- The core scales its sizes to any Frame size (`sizesFor`).

**Primary.**
- Byte order: "a single-plane with the order of R, G, B, A in increasing byte index in the ByteBuffer" ([ImageAnalysis reference](https://developer.android.com/reference/androidx/camera/core/ImageAnalysis#OUTPUT_IMAGE_FORMAT_RGBA_8888())). The guide page ([Analyze images](https://developer.android.com/media/camera/camerax/analyze)) still lists `buffer[0]: alpha, buffer[1]: red, …`, which contradicts the reference. The app's goldens on the Pixel follow the reference (R first). The conversion is CameraX's own (YUV from the HAL, converted to RGBA by CameraX in software, "CameraX internally converts images from YUV to RGBA"), so the byte order doesn't depend on the device.
- Fallback: `FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER` means "When the specified bound size is unavailable, CameraX falls back to select the closest higher resolution size. If CameraX still cannot find any available resolution, it will fallback to select other lower resolutions." Only `FALLBACK_RULE_NONE` makes the bind throw `IllegalArgumentException` ([ResolutionStrategy](https://developer.android.com/reference/androidx/camera/core/resolutionselector/ResolutionStrategy)).
- Camera2 guarantees no particular small YUV size. The stream tables give only maximum sizes (PREVIEW, RECORD, MAXIMUM; [CameraDevice](https://developer.android.com/reference/android/hardware/camera2/CameraDevice#createCaptureSession(android.hardware.camera2.params.SessionConfiguration))).
- CameraX handles the one-pixel-shift YUV bug of the Moto G3, Samsung SM-G532F/J700F/J415F/A920F and Xiaomi Mi A1 itself, for RGBA output too (`YuvImageOnePixelShiftQuirk`, applied in `ImageAnalysis.java` through `setOnePixelShiftEnabled`).

**Where it breaks.**
- A camera without 320×240 in its 4:3 YUV list gets the closest larger 4:3 size. The estimate (not measured) is usually 640×480. The Mosaic looks the same (sizes are scaled), but the core does about 4× the work: on the X3 about 50 ms, so about 20 fps; on an A55 well under 10 fps.
- Row padding: the RGBA buffer comes from an `ImageReader`, so its row stride may exceed `width·4` on some GPUs or gralloc implementations. `FrameTransform` already handles that, and the instrumented tests cover padded rows.

## 6. Binding Preview + ImageCapture + ImageAnalysis, LEGACY devices

**Code.** `bind()` calls `cameras.bindToLifecycle(owner, selector, preview, analysis, capture)` with no try/catch, from a `ListenableFuture` listener, and from `switchCamera()`.

**Primary.**
- On LEGACY, Camera2 guarantees `PRIV PREVIEW + YUV PREVIEW + JPEG MAXIMUM` ("Still capture plus in-app processing"). LIMITED and above inherit the LEGACY table ([CameraDevice](https://developer.android.com/reference/android/hardware/camera2/CameraDevice#createCaptureSession(android.hardware.camera2.params.SessionConfiguration))). Preview is PRIV, ImageAnalysis is YUV (converted to RGBA after the camera) and ImageCapture is JPEG, so this exact set is guaranteed on every hardware level.
- CameraX: "Depending on camera capability, some cameras may support the combination at lower resolution modes"; "If an incompatible combination of use cases is created, a runtime error is thrown the first time createCaptureSession() is called". Stream sharing applies only when VideoCapture is bound ([architecture](https://developer.android.com/media/camera/camerax/architecture)).
- The `ResolutionStrategy` reference warns that "Some devices may have issues using sizes of the preferred aspect ratios … an IllegalArgumentException may be thrown when calling bindToLifecycle" unless a `THEN_LOWER`/`THEN_HIGHER` fallback is used. The analysis use case uses one; Preview and ImageCapture use the default strategy, with `RATIO_4_3_FALLBACK_AUTO`.

**LEGACY quirks CameraX handles itself.**
- `AeFpsRangeLegacyQuirk`: all LEGACY devices, sets a wider AE FPS range against underexposure.
- `PreviewOrientationIncorrectQuirk`: LEGACY devices, a reused surface across sessions.
- `ExtraCroppingQuirk`: old Samsung Galaxy J/A/Tab models, a 25% crop and stretch. CameraX forces verified sizes (PRIV 1920×1080, YUV 1280×720, JPEG 3264×1836), so the analysis Frame there would be 1280×720, 16:9, about 12× the core work of 320×240.

**Where it breaks.** Nothing documented for this combination. But an exception from `bindToLifecycle` (an unsupported combination, a camera in use by another app, a disconnected camera) is uncaught and crashes the app on start or on the switch. CameraX's camera-state errors after a successful bind come through `CameraInfo.getCameraState()`, which the app doesn't observe, so a camera that fails later just shows a frozen preview.

## 7. ImageCapture: `CAPTURE_MODE_MINIMIZE_LATENCY`, the largest 4:3 size

**Code.** `setCaptureMode(CAPTURE_MODE_MINIMIZE_LATENCY)`, `RATIO_4_3_FALLBACK_AUTO`, no resolution strategy (so the highest available). Then `PhotoDecoder` caps the decode at 2048 px.

**Primary.**
- The capture mode is a latency or quality hint. On the Pixel 2 and 3, CameraX turns HDR+ off for `MINIMIZE_LATENCY` (`ImageCapturePixelHDRPlusQuirk`). This quirk is Pixel-specific but only applies to old Pixels.
- `ZslDisablerQuirk` disables ZSL on several Samsung (Fold4, Flip4/5, S22/S24/A05), Xiaomi/Redmi Note 11–13, Motorola Razr+ 2024 and Honor 90 phones. That changes latency, not correctness.
- `JpegHalCorruptImageQuirk` (Galaxy S7 SM-G930T/V, an Alps reference board) makes CameraX encode the JPEG from YUV itself.
- `JpegCaptureDownsizingQuirk` (Redmi Note 8 Pro front camera) handles a JPEG smaller than configured.
- `ExcludedSupportedSizesQuirk` removes bad sizes on OnePlus 6/6T, Huawei P20, Samsung J7/A05s/Z Fold 4, Redmi Note 9 Pro and Nokia 7 plus.

**Where it breaks.**
- On phones whose largest 4:3 JPEG is 50 MP or more (8160×6120 and up), the JPEG is 3–4× the Pixel's 12.5 MP. ImageDecoder then reads more data before it scales down, so the decode takes longer, and the capture itself may take longer. That is an estimate. CameraX by default doesn't enable the sensors' maximum-resolution (remosaic) mode, so most such phones still give the binned 12 MP.
- Nothing fails. If the 1.5 s target matters on other phones, a `ResolutionStrategy` bound near 4032×3024 would cap it.

## 8. `isReversedHorizontal` on front cameras

**Code.** `takePhoto()` sets `ImageCapture.Metadata().isReversedHorizontal = mirror` (true for the front camera), saves to a file, and `PhotoDecoder` relies on `ImageDecoder` to apply the resulting mirrored EXIF orientation. `PhotoDecoderTest` checks orientations 2, 4, 5 and 7.

**Primary.** The reference only says "Sets left-right mirroring of the capture" ([ImageCapture.Metadata](https://developer.android.com/reference/androidx/camera/core/ImageCapture.Metadata)). It doesn't document whether this is done through EXIF or by changing the pixels.

**Where it breaks.** Not documented anywhere as device-dependent: CameraX writes the EXIF itself after the HAL's JPEG, or after its own encode on `JpegHalCorruptImageQuirk` devices. The two links not verified off the Pixel are an API 29 `ImageDecoder` applying the flipped orientations (the test runs only on the Pixel's Android 17) and front cameras whose sensor orientation is 90 rather than 270. `FrameTransform` handles any `rotationDegrees`, so the preview is fine either way. Run `PhotoDecoderTest` on an API 29 device or emulator to check.

## 9. Zoom: the ultrawide chip and the ranges

**Code.** `zoomPresets()`: the minimum ratio if it is under 1, then 1× and 2× if reachable, else just 1×. The chips hide when `maxZoom == minZoom`, and `pinchedZoom()` clamps to the range.

**Primary.**
- `CONTROL_ZOOM_RATIO_RANGE` was "Added in API level 30": "If the camera device supports zoom-out from 1x zoom, minZoom will be less than 1.0". It is "Optional - The value for this key may be null on some devices" ([CameraCharacteristics](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#CONTROL_ZOOM_RATIO_RANGE)).
- On API 29, zoom is crop-region only, so it starts at 1.
- CameraX catches the `AssertionError` that the JioPhone Next, Galaxy A02s and Vivo V2039 throw on that key (`ControlZoomRatioRangeAssertionErrorQuirk`).
- On recent Samsung S/Z, Xiaomi 14/15, OPPO Find and Sony Xperia phones, CameraX doesn't use or expose the physical cameras (`ExcludePhysicalCameraIdQuirk`). The logical camera's zoom range still applies.

**Where it breaks.** It degrades cleanly. Phones on API 29, phones whose ultrawide is a separate camera ID rather than part of the logical back camera, and phones that report no range get no 0.x× chip. `DEFAULT_BACK_CAMERA` picks the first back camera CameraX lists. Phones with `maxZoom < 2` get only 1×, and a camera without zoom shows no chips.

## 10. A front and a back camera

**Code.**
- `start()`: `canSwitch = back && front`, then `bind(if (back && front) front else !back)`.
- The manifest requires `android.hardware.camera.any`.
- `hasCamera()` is declared to throw `CameraInfoUnavailableException` and isn't caught.

**Where it breaks.**
- Back only: the back camera is bound and the switch shows an empty box, which is correct.
- Front only (some tablets): `!back` is true, so the front camera is bound, which is correct.
- **External-only** (USB camera on a TV box or a Chromebook, `LENS_FACING_EXTERNAL`): neither selector matches, so `bind(true)` asks for the front camera and `bindToLifecycle` throws `IllegalArgumentException`, which crashes the app. `camera.any` lets these devices install the app.
- If `hasCamera()` throws, the listener crashes the same way.

This is rare on phones, and a try/catch around the bind (shown as an error screen) covers both.

## 11. Display P3 photos, sRGB decode, F16/1010102

**Code.** `PhotoDecoder.decode()`: `setTargetColorSpace(SRGB)`, `ALLOCATOR_SOFTWARE`, `setTargetSize(cappedSize(…))`, then a copy to ARGB_8888 if the config isn't ARGB_8888. The JNI throws on any non-RGBA_8888 Bitmap.

**Primary.** `setTargetColorSpace` (API 28): "If non-null, the decoder will try to decode into colorSpace. If it is null … or the request cannot be met, the decoder will pick either the color space embedded in the image or the ColorSpace best suited for the requested image configuration (for instance SRGB for ARGB_8888 and EXTENDED_SRGB for RGBA_F16)". Only RGB colour spaces are allowed, and sRGB has the parametric transfer function required before API 34 ([ImageDecoder](https://developer.android.com/reference/android/graphics/ImageDecoder#setTargetColorSpace(android.graphics.ColorSpace))).

**Where it breaks.**
- P3 isn't Pixel-only (recent Samsung, Xiaomi and Apple-sourced images are P3 too), and the sRGB conversion works on every API level the app supports.
- HEIF/HEIC (Samsung's "high efficiency pictures") decode through `ImageDecoder` only where the device has an HEVC decoder. That holds for practically every phone from API 29, but a failure surfaces as the "photo failed" toast, not a crash.
- An Ultra HDR JPEG (API 34+) decodes to its SDR base with a gainmap attached, which the core ignores.
- One subtle point, not verified: when the decoder falls back to F16 (10-bit HEIF/AVIF), the colour space is EXTENDED_SRGB, and `Bitmap.copy(ARGB_8888)` should clamp that to sRGB. The core doesn't check the Bitmap's colour space, only its config.

## 12. Portrait lock on large screens

**Code.** The manifest has `android:screenOrientation="portrait"` and `targetSdk = 37`. The camera screen's layout assumes a portrait window: the 3:4 viewfinder is as wide as the screen and the controls sit below it.

**Primary.** Android 16 behaviour changes: for apps targeting API 36, "orientation, resizability, and aspect ratio restrictions no longer apply on displays with smallest width >= 600dp", and `screenOrientation` `portrait` is ignored. The opt-out (`PROPERTY_COMPAT_ALLOW_RESTRICTED_RESIZABILITY`) "won't apply when targeting API level 37" ([behaviour changes](https://developer.android.com/about/versions/16/behavior-changes-16)). Games and screens under sw600dp are exempt.

**Where it breaks.** On tablets and unfolded foldables (Pixel Fold included, Galaxy Z Fold, Pixel Tablet), the activity fills the window in landscape. The viewfinder and the controls then follow the portrait layout in a landscape window: a short viewfinder, or controls off-screen, depending on how `CameraScreen` measures. The camera keeps running, because the activity handles `configChanges`. No crash, but a broken layout. This is the one assumption that also fails on other Pixels.

## 13. Photo picker on API 29

**Code.** `ActivityResultContracts.PickVisualMedia` with `ImageOnly`, then `originalDateTaken()` queries `DATE_TAKEN` inside a try/catch.

**Primary.** The picker runs natively on Android 11+ with Google System Updates. On older devices, or without it, "the library automatically invokes the ACTION_OPEN_DOCUMENT intent action instead" ([photo picker](https://developer.android.com/training/data-storage/shared/photopicker)). A backport through Play services needs a manifest `ModuleDependencies` entry, which the app doesn't have.

**Where it breaks.** API 29 and non-GMS phones get the system document picker. That works, but it looks different, and the `DATE_TAKEN` query on a documents Uri returns nothing (caught, so it falls back to the picker's answer time). Cosmetic only.

## 14. Drawing with `drawVertices` on the hardware renderer

**Code.** `MosaicCanvas` and `MosaicExport.render()` draw with `Canvas.drawVertices` on a hardware Canvas, and `Bitmap.createBitmap(Picture)` (API 28).

**Primary.** The hardware acceleration support table lists `drawVertices()` as first supported at API 29 ([hardware acceleration](https://developer.android.com/develop/ui/views/graphics/hardware-accel)).

**Where it breaks.** Nowhere in range: minSdk 29 is exactly the first level where this works. Lowering minSdk would break the drawing.

## 15. Samsung, Xiaomi and other quirks

CameraX's `DeviceQuirksLoader` (camera-camera2, `androidx-main`) lists about 30 quirks, and CameraX applies them automatically by build model. Those relevant to this app's use cases are cited above:

- one-pixel shift: §5
- extra cropping, excluded sizes, ZSL, corrupt JPEG, JPEG downsizing: §6 and §7
- zoom range assertion, physical camera IDs: §9

Most of the rest concern flash (the app sets no flash mode, so the default applies), video, or extensions, which the app doesn't use. **No quirk was found that the app must work around in its own code.** The issue tracker's open CameraX bugs could not be searched in a structured way from here: `issuetracker.google.com` needs a sign-in, so the quirk files, which cite their bug IDs (`b/…`), stand in for it.

## What to change, if other phones matter

In order of value for effort:

1. Wrap `bindToLifecycle()` and `hasCamera()` in a try/catch and show an error state (§6, §10). This is the only crash path.
2. Decide on large screens (§12): either make the camera screen adaptive, or accept the landscape layout on sw600dp devices. There is no opt-out at targetSdk 37.
3. Cap ImageCapture near 12 MP with a `ResolutionStrategy` (§7), so the photo time doesn't depend on the sensor.
4. Add `armeabi-v7a` if Android Go phones are a target (§1), and measure the core on an A55 (§3).
5. Run `PhotoDecoderTest` and `NativeMosaicTest` on an API 29 emulator (§8, §5) to cover the oldest supported decoder and the first-run analysis size.
