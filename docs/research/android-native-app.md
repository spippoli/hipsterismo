# Research: native Android app over an OF-free C++ core

Ticket: #50 (part of map #48). Question: what does a native Android app (Kotlin, current AndroidX) need to feed camera and gallery Frames to a C++ core through JNI, and to save and share the resulting Mosaic?

Target constraints from #48: Pixel 8a only, minSdk 26–29, live preview of 320×240 Frames at ≥ 15 fps (30 target) with the adaptive core run on every Frame, full-resolution shots and gallery photos capped at about 2048 px on the long side, command-line Gradle build on this Linux machine, debug APK installed with `adb install`.

Researched 2026-11-15. Versions were read from Google's Maven repository, the SDK repository manifest and the release pages linked below. Claims are cited inline. Things that could not be confirmed from a primary source are marked **unverified** or **measure on device**.

---

## TL;DR: recommendations

1. **minSdk 29.** Pixel 8a only, so nothing is lost, and API 29 is the level where several pieces become simple: `Canvas.drawVertices()` is hardware-accelerated from API 29 [hwaccel], `ImageDecoder` exists from API 28 [ImageDecoder], and on Android 10+ the app needs no storage permission to write its own MediaStore images [media]. compileSdk/targetSdk 37, the highest level AGP 9.5 supports [AGP].
2. **Camera:** CameraX `ProcessCameraProvider` with `Preview` + `ImageAnalysis` + `ImageCapture` bound together. `ImageAnalysis` uses `OUTPUT_IMAGE_FORMAT_RGBA_8888`, `STRATEGY_KEEP_ONLY_LATEST` and a `ResolutionSelector` asking for 320×240 (4:3). Rotation stays **off** in CameraX. C++ applies `rotationDegrees` while it copies RGBA to RGB, so one pass handles rotation, alpha stripping and stride.
3. **Shutter and gallery share one path.** `ImageCapture` saves a JPEG to the app cache, and the photo picker returns a content URI. Both are decoded with `ImageDecoder`, using `setTargetSize` (cap ~2048) and `ALLOCATOR_SOFTWARE`. ImageDecoder applies EXIF orientation itself [hwui-src]. The resulting `Bitmap` goes to C++ via `AndroidBitmap_lockPixels`.
4. **JNI:** use one `external` entry per Frame source: a direct `ByteBuffer` for camera planes (zero copy) [Image.Plane], and a locked `Bitmap` for photos [bitmap.h]. Register the methods with `RegisterNatives` in `JNI_OnLoad` [jni-tips]. Return the Mosaic as flat arrays (`FloatArray` of triangle vertices + `IntArray` of colours).
5. **Drawing:** a custom `View` calls `Canvas.drawVertices(TRIANGLES, …)` with a fit-centre `Matrix`. That covers the live preview (crisp at any scale, hardware-accelerated on API 29+) and the saved file, which draws the same arrays onto a software `Canvas(Bitmap)` at the capped Frame size and then `compress(PNG)`s it. GLES is not worth the extra code at these triangle counts (**measure on device**).
6. **Output:** insert into `MediaStore.Images` with `RELATIVE_PATH = Pictures/Hipsterismo` and `IS_PENDING` [media]. Share that MediaStore content URI with `ACTION_SEND` + `EXTRA_STREAM` + `FLAG_GRANT_READ_URI_PERMISSION` [share]. Use FileProvider only if you want to share without saving. **The only permission is `CAMERA`** [camerax-arch].
7. **OpenCV:** the adaptive core uses only `convertTo`, `cvtColor(RGB2Lab)` on float and `GaussianBlur` (`src/mosaic/Adaptive.cpp:53-58`). The prebuilt Android SDK is a monolithic ~24.7 MB/ABI `.so` (4.14.0) that also pulls in `libc++_shared.so`. **Recommend not shipping it.** Either port those three operations to portable C++ in the shared core, or build OpenCV `core,imgproc` statically from source. Bit-identical output across x86-64 and arm64 cannot be guaranteed in either case (see §7). That is a decision for the spec.
8. **Toolchain (late 2026):** AGP 9.5.1, Gradle 9.9.1 (≥ 9.6.0 required), JDK 17+ (system JDK 25 is within Gradle's 17–27 range), NDK r30 LTS `30.0.16248370` (AGP 9.5's default is `28.2.13676358`), CMake 4.1.2 or 3.31.6 from the SDK, build-tools 37.0.0, CameraX 1.6.2, activity 1.13.0, exifinterface 1.4.2 (not needed if ImageDecoder is used). Built-in Kotlin, so no `kotlin-android` plugin.

---

## 1. Camera: CameraX

### 1.1 Live Frames with `ImageAnalysis`

- **Formats.** `ImageAnalysis` delivers `YUV_420_888` by default. `setOutputImageFormat()` (since 1.1.0) also accepts `RGBA_8888`, `NV21` and `PRIVATE`. "Requesting RGBA_8888 or NV21 will have extra overhead because format conversion takes time" [IA.Builder]. With RGBA, CameraX converts internally and packs the pixels into **plane 0 only**. The guide documents the byte order as A, R, G, B for bytes 0..3 [analyze].
  > **Unverified/odd:** the guide's listing says `buffer[0]: alpha, buffer[1]: red …`, while the constant's name and `PixelFormat.RGBA_8888` suggest R,G,B,A in memory. **Check on the device** with a known colour before trusting either one.
- **Cost.** At 320×240 a YUV→RGBA conversion touches 76,800 pixels, which is small next to the adaptive pipeline. The options:
  - (a) CameraX `RGBA_8888`: simplest. The conversion happens before `analyze()` is called.
  - (b) Request YUV and convert in C++. You then choose the colour matrix yourself. Camera YUV is normally BT.601 full-range (JFIF), **unverified for the Pixel 8a**. The chroma planes have a `pixelStride` of 1 or 2 and a `rowStride` that can be padded [Image.Plane].
  - Recommendation: (a). Switch to (b) only if profiling shows the conversion matters.
- **Backpressure.** The default is `STRATEGY_KEEP_ONLY_LATEST`, a queue of depth 1 where new images overwrite the old one. A slow analyser therefore drops Frames without stalling `Preview` [IA.Builder] [analyze]. That is exactly what the "≥ 15 fps, 30 target" requirement needs. `STRATEGY_BLOCK_PRODUCER` blocks every bound use case, preview included [analyze], so avoid it.
- **Resolution.** `setTargetResolution`/`setTargetAspectRatio` are deprecated since 1.3.0. Use `setResolutionSelector(ResolutionSelector)` with a `ResolutionStrategy` and an `AspectRatioStrategy`. By default ImageAnalysis bounds at 640×480 with `FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER` [IA.Builder]. What you actually get "depends on the camera device's hardware level, capabilities and the bound use cases combination" [IA.Builder]. Read `imageProxy.width/height` on every Frame. **Measure on device** whether 320×240 is offered. If it isn't, take 640×480 and box-downscale 2× in C++. That keeps the 320×240 reference size of `adaptive::sizesFor()` exact.
- **Rotation.** `imageInfo.rotationDegrees` is the clockwise rotation that makes the image match the use case's target rotation [rotation]. `setOutputImageRotationEnabled(true)` (API 23+) makes CameraX rotate the image itself, but it costs "about 10-15ms for 640x480 image on a mid-range device" [IA.Builder], which is a large slice of a 33–66 ms budget. **Recommendation:** leave it off and rotate in the C++ RGBA→RGB copy. With a portrait-locked activity and an `OrientationEventListener` updating `targetRotation` [rotation], the core gets 240×320 in portrait. `sizesFor()` already treats 240×320 like 320×240.
- **Close every `ImageProxy`** (`imageProxy.close()`, never `Image.close()`) [analyze]. With KEEP_ONLY_LATEST, a proxy you forget to close stops the stream.
- **Concurrency.** One `Preview` + one `ImageAnalysis` + one `ImageCapture` can be bound at once [camerax-arch]. "Some cameras may support the combination at lower resolution modes" [camerax-arch], so check the capture resolution you get while all three are bound (**measure on device**).

Code shape (signatures only):

```kotlin
val analysis = ImageAnalysis.Builder()
    .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
    .setResolutionSelector(
        ResolutionSelector.Builder()
            .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
            .setResolutionStrategy(ResolutionStrategy(Size(320, 240),
                ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER))
            .build())
    .build()
analysis.setAnalyzer(coreExecutor) { proxy: ImageProxy ->
    val p = proxy.planes[0]                       // direct ByteBuffer
    val mosaic = MosaicCore.makeFromRgba(p.buffer, proxy.width, proxy.height,
        p.rowStride, proxy.imageInfo.rotationDegrees, minPatchArea)
    proxy.close()
    mosaicView.post { mosaicView.show(mosaic) }
}
cameraProvider.bindToLifecycle(owner, CameraSelector.DEFAULT_BACK_CAMERA, preview, analysis, capture)
```

The raw `Preview` does not have to be shown. The UI can show only the Mosaic view. Binding a `Preview` is optional [camerax-arch], but keeping it gives AE/AF a normal stream, and a "show source" toggle (desktop key `h`) is then free.

### 1.2 Full-resolution shot with `ImageCapture`

- Use `takePicture(OutputFileOptions, Executor, OnImageSavedCallback)` to save to a file, a stream or MediaStore, or `takePicture(Executor, OnImageCapturedCallback)` to get the image in memory [take-photo]. Capture modes are `CAPTURE_MODE_MINIMIZE_LATENCY` (the default), `CAPTURE_MODE_MAXIMIZE_QUALITY` and an experimental ZSL mode. The output is JPEG [capture-options].
- Rotation: a captured image, in whatever form (`ImageProxy`, `File`, `OutputStream`, MediaStore), carries the rotation needed to match the target rotation. For files it can be read back from the EXIF orientation [rotation].
- `ImageProxy.toBitmap()` (1.3.0+) handles YUV_420_888, JPEG and RGBA_8888 [ImageProxy]. Its documentation says nothing about rotation, so with the in-memory path you would apply `rotationDegrees` yourself.
- **Recommendation:** capture to a file in `cacheDir`, then decode it with exactly the same `ImageDecoder` path as gallery photos (§2). One decode path then covers EXIF orientation and the ~2048 cap for both sources, at the cost of one JPEG write and read, which is cheap next to the Mosaic. Set a `ResolutionSelector` on `ImageCapture` near the cap (for example 4:3 around 2048×1536, fallback higher). The sensor then does not have to deliver a full-size image that is downscaled straight away. Note the trade-off documented for RECORD vs MAXIMUM sizes when the three use cases are bound together [IA.Builder].
- Pixel 8a hardware: Tensor G3, 8 GB RAM, 64 MP Quad-PD wide camera, 1080×2400 display [pixel-specs]. The default JPEG size CameraX picks is **measure on device**.

## 2. Gallery: the photo picker, decoding, EXIF

- **Picker.** `ActivityResultContracts.PickVisualMedia` with `PickVisualMediaRequest(PickVisualMedia.ImageOnly)` needs `androidx.activity` ≥ 1.7.0 [picker] (current stable is 1.13.0). The picker is native on Android 11+ devices that get Google System Updates. On older devices the library falls back to `ACTION_OPEN_DOCUMENT`, and on Android 4.4–10 a backport can be installed through Google Play services with a `ModuleDependencies` manifest `<service>` entry [picker]. The Pixel 8a runs Android 14 or later, so no backport entry is needed. **No permission is required.** Access lasts until reboot or until the app stops, unless you call `takePersistableUriPermission()` [picker]. The app processes the photo straight away, so no persistence is needed. The Media guide recommends the picker over `READ_MEDIA_IMAGES` [media].

  ```kotlin
  val pick = registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? -> … }
  pick.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
  ```

- **Decoding with a size cap.** Use `ImageDecoder` (API 28) [ImageDecoder]:
  - `ImageDecoder.createSource(contentResolver, uri)` or `createSource(file)`, then `decodeBitmap(source) { decoder, info, _ -> … }`. Every setter must be called inside `OnHeaderDecodedListener.onHeaderDecoded`.
  - `decoder.setTargetSize(w, h)` takes an arbitrary size, computed from `info.size` so that the long side becomes ≤ 2048. `setTargetSampleSize(n)` is the power-of-two variant.
  - **`decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE`.** By default the bitmap "will typically have Config HARDWARE", which is not CPU-accessible. `ALLOCATOR_SOFTWARE` is "useful … for accessing the pixels" [ImageDecoder]. Without it, `AndroidBitmap_lockPixels` cannot work, and a HARDWARE bitmap reports `ANDROID_BITMAP_FLAGS_IS_HARDWARE` [bitmap.h].
  - **EXIF orientation:** the Java reference page doesn't say. The hwui implementation behind it applies the encoded origin with `SkEncodedOriginToMatrix` and swaps width and height for 90°/270° origins (`libs/hwui/hwui/ImageDecoder.cpp`, main) [hwui-src]. So ImageDecoder output is upright and `info.size` is already in display orientation. **Verify with a rotated test JPEG on the device.**
- **Fallback (only if minSdk < 28):** `BitmapFactory` with `inJustDecodeBounds` and then a power-of-two `inSampleSize` [load-bitmap]. BitmapFactory does not apply EXIF orientation, so read it with `androidx.exifinterface` 1.4.2 [exif-rel] and rotate. With the recommended minSdk 29, this path and the exifinterface dependency are not needed.

## 3. Native build: NDK + CMake in Gradle

### 3.1 Versions (checked 2026-11-15)

| Component | Version | Source |
|---|---|---|
| Android Gradle Plugin | **9.5.1** stable (9.6.0-alpha03 in preview). Max API 37; Gradle ≥ 9.6.0; JDK 17; build-tools 36.0.0 minimum; default NDK `28.2.13676358` | [AGP], Google Maven |
| Gradle | **9.9.1** current; runs on JVM 17–27; tested with AGP 9.0–9.5 | [gradle-compat] |
| Kotlin | Built into AGP 9.x: do **not** apply `org.jetbrains.kotlin.android` [builtin-kotlin]. AGP 9.5.1's POM depends on KGP 2.2.10. Standalone Kotlin stable is 2.4.21 | AGP POM, Maven Central |
| NDK | **r30 LTS `30.0.16248370`**; r29 `29.0.14206865`; AGP default `28.2.13676358`. r28+ builds 16 KB-aligned by default | [ndk-dl], SDK repo manifest, [16kb] |
| CMake (SDK packages) | `cmake;4.1.2`, `cmake;3.31.6`, `3.22.1`… The host has CMake 4.2.3 | SDK repo manifest |
| Build-tools / platforms | `build-tools;37.0.0`, `platforms;android-37.0` (37.1/37.2 also listed) | SDK repo manifest |
| CameraX | **1.6.2** stable (2026-08-26); 1.7.0-rc02 | [camerax-rel], Google Maven |
| androidx.activity | 1.13.0 stable | Google Maven |
| androidx.core | 1.19.1 (FileProvider lives here) | Google Maven, [FileProvider] |
| exifinterface | 1.4.2 stable (1.5.0-beta01) | [exif-rel] |
| OpenCV Android (Maven `org.opencv:opencv`) | 5.1.0 latest, 4.14.0 last 4.x. Desktop uses 4.10.0 (`pkg-config`) | Maven Central |

Command-line setup without Android Studio: download `commandlinetools-linux-<rev>_latest.zip` and run `sdkmanager --licenses`. AGP 4.2+ can then download the NDK and CMake on the first build by itself [install-ndk]. Alternatively, preinstall `sdkmanager "platform-tools" "platforms;android-37.0" "build-tools;37.0.0" "ndk;30.0.16248370" "cmake;4.1.2"`. Then run `./gradlew assembleDebug` and `adb install -r app/build/outputs/apk/debug/app-debug.apk`.

### 3.2 Gradle and CMake shape

```kotlin
// android/app/build.gradle.kts
plugins { id("com.android.application") }        // AGP 9: Kotlin built in
android {
    namespace = "…"; compileSdk = 37
    ndkVersion = "30.0.16248370"
    defaultConfig { minSdk = 29; targetSdk = 37; ndk { abiFilters += "arm64-v8a" } }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "4.1.2" } }
}
```

- `externalNativeBuild.cmake.path` links one top-level `CMakeLists.txt` per module. `ndk.abiFilters` limits the ABIs built and packaged [ext-native]. The Pixel 8a needs only `arm64-v8a`. Add `x86_64` only for an emulator.
- Gradle passes `ANDROID_ABI`, `ANDROID_PLATFORM` (= minSdk) and the NDK toolchain file automatically. Don't set them yourself [ndk-cmake]. The default STL is `c++_static` [ndk-cmake], which is right for a single `.so`. Shipping the OpenCV AAR forces `c++_shared`, because the AAR bundles `libc++_shared.so`.
- Set flags in CMake (`target_compile_features(... cxx_std_20)`, `target_compile_options`), not through Gradle `cppFlags`: the NDK guide says "You should not do this" [ndk-cmake].
- **Sharing sources with the desktop build:** `android/app/src/main/cpp/CMakeLists.txt` adds `../../../../../src/mosaic/*.cpp` (the OF-free core), plus the JNI glue under `android/`. The JNI glue must stay **outside `src/`**, because the OF Makefile compiles everything under `src/`. CDT is header-only inside `src/mosaic/CDT`, so it needs no CMake project of its own.
- Link `jnigraphics` (for `AndroidBitmap_*`) and `log`: `target_link_libraries(mosaic PRIVATE jnigraphics log)` [bitmap.h].
- 16 KB pages: Android 15+ supports 16 KB-page devices, and the Pixel 8a has a "Boot with 16KB page size" developer option. NDK r28+ and AGP ≥ 8.5.1 are compliant by default [16kb]. The OpenCV 4.14.0 AAR's arm64 `.so` has 0x4000-aligned LOAD segments (checked with `readelf`), so it would be fine too.

### 3.3 The core must become OF-free

The adaptive core currently takes `ofPixels` and returns `Tessel{std::array<glm::vec3,3>}` and `std::vector<ofPolyline>` (`src/mosaic/Mosaic.h:24,31,89`; `Adaptive.h`), and it reaches OpenCV through `ofxCv::toCv`. For Android it needs plain types, roughly:

```cpp
namespace adaptive {
struct RgbView { const uint8_t* data; int width, height, stride; };   // tightly packed RGB rows
struct Point  { float x, y; };
struct Tessel { Point a, b, c; uint8_t r, g, b; };
struct Result { std::vector<Tessel> tessels; std::vector<std::vector<Point>> contours; };
Result make(RgbView rgb, int minPatchArea);
}
```

The desktop `MosaicMaker` then adapts `ofPixels` into `RgbView` and the `Result` back into `ofPolyline` and `glm::vec3`. The golden hashes guard that the output stays identical (#48).

## 4. JNI: passing pixels and returning the Mosaic

- **Camera planes:** `Image.Plane.getBuffer()` "will always" return a direct ByteBuffer, "so the underlying data could be mapped as a pointer in JNI without doing any copies with `GetDirectBufferAddress`" [Image.Plane]. The buffer is only valid until the `ImageProxy` is closed [Image.Plane], so the C++ call must copy or convert synchronously.
- **Bitmaps:** `AndroidBitmap_getInfo` gives width, height, stride and format. `AndroidBitmap_lockPixels`/`unlockPixels` pin the pixels and must be balanced. The format is `ANDROID_BITMAP_FORMAT_RGBA_8888` (R,G,B,A bytes). Decoded JPEGs are opaque, so premultiplication does not matter [bitmap.h].
- **General advice** [jni-tips]: keep marshalling minimal and synchronous, register natives in `JNI_OnLoad` with `RegisterNatives` (errors show up at load time, and only `JNI_OnLoad` needs to be exported), and use `-fvisibility=hidden` or a version script. If there is no clear winner between `byte[]` and a direct buffer, "use a direct byte buffer". Prefer `Set<Type>ArrayRegion` over `Get…Elements` when copying.
- **Returning the Mosaic:** at a few thousand Tessels per Frame (**measure**), allocating two arrays per Frame with `NewFloatArray` + `SetFloatArrayRegion` is simple and cheap. The alternative is filling caller-owned arrays and returning a count. Duplicate each colour 3×, because `drawVertices` takes one colour per vertex.

```kotlin
class MosaicArrays(val vertices: FloatArray /* 6 per Tessel */, val colors: IntArray /* 3 per Tessel, ARGB */,
                   val width: Int, val height: Int)
object MosaicCore {
    init { System.loadLibrary("mosaic") }
    external fun makeFromRgba(plane: ByteBuffer, width: Int, height: Int, rowStride: Int,
                              rotationDegrees: Int, minPatchArea: Int): MosaicArrays
    external fun makeFromBitmap(bitmap: Bitmap, minPatchArea: Int): MosaicArrays
}
```

```cpp
static jobject makeFromRgba(JNIEnv*, jclass, jobject plane, jint w, jint h, jint rowStride,
                            jint rotation, jint minPatchArea);   // GetDirectBufferAddress → rotate+RGBA→RGB → adaptive::make
static jobject makeFromBitmap(JNIEnv*, jclass, jobject bitmap, jint minPatchArea); // AndroidBitmap_lockPixels
JNIEXPORT jint JNI_OnLoad(JavaVM*, void*);                        // RegisterNatives
```

Run the core on the analyser executor (one thread), never on the UI thread. That follows the "callback between two threads in the Java programming language" pattern in [jni-tips].

## 5. Drawing the Mosaic

| Option | Pros | Cons |
|---|---|---|
| **`Canvas.drawVertices(VertexMode.TRIANGLES, …)`** in a custom `View` | A few dozen lines. Vector, so crisp when scaled to the 1080×2400 screen with a fit-centre `Matrix` (desktop uses `OF_SCALEMODE_FIT`). Hardware-accelerated from API 29 [hwaccel]. The same call on a software `Canvas(Bitmap)` renders the file to save. | Per-vertex colours, so vertices are duplicated per Tessel. The `short[]` indices limit one call to 65,535 indexed vertices; pass no indices, or split into batches. |
| GLES 3 via `GLSurfaceView` | Fastest. Matches how OF draws on desktop. | EGL/GL boilerplate, a separate path for saving (FBO readback), more to test. |
| Rasterise in C++ into a `Bitmap` | Deterministic. Testable on the desktop. | Blocky when a 320×240 raster is scaled up, unless you rasterise at screen size. Reimplements a rasteriser. |

The `drawVertices` signature is `drawVertices(VertexMode mode, int vertexCount, float[] verts, int vertOffset, float[] texs, int texOffset, int[] colors, int colorOffset, short[] indices, int indexOffset, int indexCount, Paint paint)` [Canvas]. **Recommendation:** use `drawVertices` for both preview and save, and move to GLES only if on-device measurement shows dropped Frames from drawing rather than from the core. Hardware-accelerated drawing tips: don't allocate a `Paint` or `Path` in `onDraw` [hwaccel].

## 6. Output: MediaStore save and share

- **Permissions.** "On devices that run Android 10 or higher, you don't need storage-related permissions to access and modify media files that your app owns" [media]. CameraX itself needs only `CAMERA`, plus `WRITE_EXTERNAL_STORAGE` only before Android 10 [camerax-arch]. The picker needs nothing [picker]. **Manifest: `<uses-permission android:name="android.permission.CAMERA"/>`**, requested at runtime (`ActivityResultContracts.RequestPermission`). Optionally add `<uses-feature android:name="android.hardware.camera.any"/>`.
- **Save.** Insert into `MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)` with `DISPLAY_NAME`, `MIME_TYPE` and `RELATIVE_PATH` (for example `Pictures/Hipsterismo`), and `IS_PENDING = 1`. Write through `contentResolver.openOutputStream(uri)`, then set `IS_PENDING = 0` [media]. While a file is pending, only the app can see it [media]. Use PNG: flat-colour Tessels compress well and stay lossless.

  ```kotlin
  fun saveMosaic(resolver: ContentResolver, bitmap: Bitmap, name: String): Uri
  ```

- **Share.** `Intent(ACTION_SEND).setType("image/png").putExtra(EXTRA_STREAM, uri).addFlags(FLAG_GRANT_READ_URI_PERMISSION)`, then `Intent.createChooser(…)` [share]. The receiver needs access to the URI. [share] names a FileProvider or the MediaStore as the two recommended ways. Sharing the MediaStore URI of the saved file is simplest. To share without saving, write to `cacheDir` and use `FileProvider.getUriForFile(context, authority, file)` with a `<provider android:exported="false" android:grantUriPermissions="true">` and a `<cache-path>` in `res/xml/file_paths.xml` [FileProvider]. Android 14+ adds optional sharesheet custom actions (`EXTRA_CHOOSER_CUSTOM_ACTIONS`); the app doesn't need them [share].

## 7. OpenCV: needed or not?

What the adaptive core uses (`src/mosaic/Adaptive.cpp:53-58`): `Mat::convertTo(CV_32F, 1/255)`, `cv::cvtColor(…, COLOR_RGB2Lab)` on float, `cv::GaussianBlur(…, Size(0,0), sigma)`, and `cv::Mat`/`cv::Vec3f` as containers. The spline and stable modes use `ofxCv` (Canny, ContourFinder), but they stay desktop-only (#48).

**Prebuilt OpenCV Android SDK** (Maven Central `org.opencv:opencv`, checked by downloading the AARs):

| AAR | Size | arm64-v8a `.so` | Notes |
|---|---|---|---|
| 4.14.0 | 123 MB | `libopencv_java4.so` **24.7 MB** (~9.8 MB gzipped) + `libc++_shared.so` 1.3 MB | monolithic (all modules), prefab module `opencv_java4`, minSdk 21, 16 KB-aligned |
| 5.1.0 | 148 MB | `libopencv_java5.so` **32.7 MB** + `libc++_shared.so` | monolithic, prefab `opencv_java5` |

The AAR ships no static libraries, so you can't link only core+imgproc from it. AGP stores `.so` files uncompressed in the APK by default, so the debug APK grows by roughly the full `.so` size.

**Building only what you need:** OpenCV's CMake accepts `-DBUILD_LIST=core,imgproc`, which builds just those modules and their dependencies [ocv-config]. Building it with the NDK toolchain as static libraries (`BUILD_SHARED_LIBS=OFF`, Java and apps off) and linking them into `libmosaic.so` keeps only the code you use (**size: measure**). The price is a cross-build of OpenCV in the setup, and pinning the same version as the desktop (4.10.0) to stay closest to its behaviour.

**Avoiding OpenCV:** replace the three calls with about 100 lines of portable C++ (sRGB→linear→XYZ(D65)→Lab, and a separable Gaussian with `BORDER_REFLECT_101`). Then the shared core has no OpenCV dependency on either platform. **Caveat on identical output:**
- OpenCV's float RGB→Lab does not use the textbook formula. With the default sRGB and D65 it takes an interpolated, table-driven, SIMD path (`RGB2Lab_f`, `useInterpolation`, `LAB_CBRT_TAB_SIZE` in `modules/imgproc/src/color_lab.cpp`) [ocv-lab].
- `GaussianBlur` derives the kernel size from sigma as `cvRound(sigma*4*2+1)|1` for non-8U data [ocv-smooth].
- A rewrite will therefore **not** be bit-identical to the current desktop output, and would change the adaptive golden hash. The map only allows refactors that keep the output identical.

**Cross-architecture caveat:** float code is not guaranteed to give identical results on x86-64 and arm64, because SIMD paths and FMA contraction differ. This holds even with the same OpenCV version and the same core sources. For example, clang on aarch64 may fuse multiply-adds unless built with `-ffp-contract=off` (**unverified for this NDK, measure**). So "Android output == desktop golden hash" should not be a test assumption in any case. Test Android against its own golden values, or by Tessel-area coverage and invariants, as the desktop tests already do by summing Tessel areas.

**Recommendation:** for the spec, decide between:
- **(A)** Port the three operations to portable C++ in the shared core and deliberately re-record the adaptive golden hash once. This gives one dependency-free core everywhere and the smallest APK.
- **(B)** Keep OpenCV on desktop and build OpenCV `core,imgproc` statically for Android. The desktop output stays untouched, at the cost of a heavier Android setup.

(A) is the cleaner fit for "one Mosaic module" and "command-line build on this machine". Prototype it on a throwaway branch and measure the visual difference first. The prebuilt AAR is not recommended: it is monolithic, ~25–33 MB per ABI, and forces `c++_shared`.

## 8. Open items to measure on the Pixel 8a

- Whether `ImageAnalysis` delivers 320×240 when bound with `Preview` + `ImageCapture`. Otherwise 640×480 with a 2× downscale.
- The byte order of the RGBA plane (the guide says A,R,G,B).
- The adaptive core's time per 320×240 Frame on Tensor G3, which decides the 15/30 fps target, and the Tessel count per Frame.
- The `drawVertices` frame time at that Tessel count.
- The default `ImageCapture` JPEG size, and the time to decode it to ≤ 2048 px and run the core on it. This fixes the resolution cap.
- ImageDecoder EXIF handling, with a rotated test JPEG.

---

## Sources

- [analyze] CameraX image analysis guide: https://developer.android.com/media/camera/camerax/analyze
- [IA.Builder] `ImageAnalysis.Builder` reference: https://developer.android.com/reference/androidx/camera/core/ImageAnalysis.Builder
- [rotation] CameraX orientation and rotation: https://developer.android.com/media/camera/camerax/orientation-rotation
- [take-photo] CameraX image capture: https://developer.android.com/media/camera/camerax/take-photo
- [capture-options] ImageCapture options: https://developer.android.com/media/camera/camerax/take-photo/options
- [ImageProxy] `ImageProxy` reference (`toBitmap`): https://developer.android.com/reference/androidx/camera/core/ImageProxy
- [camerax-arch] CameraX architecture (use-case combinations, permissions, dependencies): https://developer.android.com/media/camera/camerax/architecture
- [camerax-rel] CameraX releases: https://developer.android.com/jetpack/androidx/releases/camera
- [picker] Photo picker: https://developer.android.com/training/data-storage/shared/photopicker
- [ImageDecoder] `android.graphics.ImageDecoder`: https://developer.android.com/reference/android/graphics/ImageDecoder
- [hwui-src] AOSP `libs/hwui/hwui/ImageDecoder.cpp` (main), origin handling via `SkEncodedOriginToMatrix`: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/libs/hwui/hwui/ImageDecoder.cpp ; commit "Handle EXIF orientation in hwui/ImageDecoder": https://android.googlesource.com/platform/frameworks/base/+/139145be8082f0606045b89feebc9126a9a376d8
- [load-bitmap] Loading large bitmaps (`inSampleSize`): https://developer.android.com/topic/performance/graphics/load-bitmap
- [exif-rel] ExifInterface releases: https://developer.android.com/jetpack/androidx/releases/exifinterface
- [AGP] AGP release notes (9.5 compatibility table): https://developer.android.com/build/releases/gradle-plugin
- [gradle-compat] Gradle compatibility matrix: https://docs.gradle.org/current/userguide/compatibility.html
- [builtin-kotlin] Migrate to built-in Kotlin: https://developer.android.com/build/migrate-to-built-in-kotlin
- [ndk-dl] NDK downloads: https://developer.android.com/ndk/downloads
- [install-ndk] Install and configure the NDK and CMake: https://developer.android.com/studio/projects/install-ndk
- [ext-native] Link Gradle to your native library: https://developer.android.com/studio/projects/gradle-external-native-builds
- [ndk-cmake] NDK CMake guide: https://developer.android.com/ndk/guides/cmake
- [16kb] Support 16 KB page sizes: https://developer.android.com/guide/practices/page-sizes
- SDK repository manifest (package versions): https://dl.google.com/android/repository/repository2-3.xml
- [bitmap.h] NDK Bitmap API: https://developer.android.com/ndk/reference/group/bitmap
- [jni-tips] JNI tips: https://developer.android.com/training/articles/perf-jni
- [Image.Plane] `android.media.Image.Plane`: https://developer.android.com/reference/android/media/Image.Plane
- [hwaccel] Hardware acceleration (drawing-operation support table): https://developer.android.com/topic/performance/hardware-accel
- [Canvas] `android.graphics.Canvas` reference: https://developer.android.com/reference/android/graphics/Canvas
- [media] Access media files from shared storage: https://developer.android.com/training/data-storage/shared/media
- [share] Send simple data to other apps: https://developer.android.com/training/sharing/send
- [FileProvider] `androidx.core.content.FileProvider`: https://developer.android.com/reference/androidx/core/content/FileProvider
- [pixel-specs] Pixel phone hardware tech specs: https://support.google.com/pixelphone/answer/7158570
- [ocv-config] OpenCV configuration reference (`BUILD_LIST`): https://github.com/opencv/opencv/blob/4.x/doc/tutorials/introduction/config_reference/config_reference.markdown
- [ocv-lab] OpenCV `modules/imgproc/src/color_lab.cpp`: https://github.com/opencv/opencv/blob/4.x/modules/imgproc/src/color_lab.cpp
- [ocv-smooth] OpenCV `modules/imgproc/src/smooth.dispatch.cpp`: https://github.com/opencv/opencv/blob/4.x/modules/imgproc/src/smooth.dispatch.cpp
- OpenCV Android AARs: https://repo1.maven.org/maven2/org/opencv/opencv/ (4.14.0 and 5.1.0 downloaded and inspected)
- Google Maven metadata (AndroidX/AGP versions): https://dl.google.com/dl/android/maven2/
