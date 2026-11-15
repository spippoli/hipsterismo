# Manual checks for the Android app

There are no automated UI tests (#59). Run this list by hand on the Pixel 8a before merging a change to the UI, the camera or the threading, and put the results in the PR. Install with `adb install -r app/build/outputs/apk/debug/app-debug.apk`; `./gradlew connectedDebugAndroidTest` uninstalls the app, so a fresh install asks for the camera again.

## Camera screen (#64)

- [ ] **Permission denied:** on a fresh install, deny the camera. The explanation appears ("Serve la fotocamera"), with no crash. "Consenti la fotocamera" asks again; after a second denial the button reads "Apri le impostazioni" and opens the app's settings. Granting the camera there and coming back shows the preview.
- [ ] **Permission granted:** the live Mosaic fills the screen, cropped, and follows the camera upright.
- [ ] **Stats:** a tap on the LIVE chip adds the core's ms per Frame and the Tessel count; another tap hides them. The fps stays ≥ 15 (30 is the target) for 60 s on an ordinary indoor scene. `adb logcat -s hipsterismo:I` prints one `preview …: N fps, core N ms` line per second.
- [ ] **Slider:** 8 steps, 8 to 1024 px², starting at 32. Moving it changes the Patches: fewer and larger Tessels towards 1024.
- [ ] **Overlay:** the polyline button draws the Contours in white over the Mosaic and hides them again.
- [ ] **Landscape:** rotate the phone both ways (90° and 270°). The Mosaic stays upright, the slider is vertical on the left (grossa at the top), and the overlay, shutter and gallery buttons are in a column on the right. The slider and overlay settings survive the rotation.
- [ ] **Background and resume:** Home, then back to the app: the preview resumes.

## Photo to Mosaic (#65)

Watch the photo times with `adb logcat -s hipsterismo-photo:I`: one `photo camera|gallery WxH: N ms to the result (capture N, decode N, core N = N%, other N), …` line per photo, from the shutter tap (or the picker's answer) to the result screen's first draw.

- [ ] **Shutter:** at the default 32 px², point at an ordinary indoor scene and tap the shutter. The last preview Mosaic freezes and dims, the chip reads "in pausa" with an amber dot, a blue ring spins on the shutter, and the slider, gallery and overlay buttons are dimmed and ignore taps (a second shutter tap too). The result screen follows.
- [ ] **≤ 1.5 s:** take five shutter photos, each followed by Scarta. Every `photo camera` line says at most 1500 ms to the result, at about 2048×1536 (2048 on the long side).
- [ ] **Upright:** a photo taken holding the phone in portrait, and one in landscape (both turns), shows upright on the result screen, in the same orientation as the preview was.
- [ ] **Gallery:** the thumbnail opens the system photo picker (no permission asked). Cancelling it goes back to the live preview. Picking an image shows the same processing state, then its Mosaic; a `photo gallery` line is logged. An image with an EXIF rotation (a portrait photo from the camera app) shows upright, and one smaller than 2048 keeps its size in the log line.
- [ ] **Result screen:** the Mosaic fits the screen whole, not cropped. The top bar has back, "Mosaic", the `min patch area` the photo was taken at (e.g. `32 px²`) and the overlay toggle, which draws the Contours over the photo's Mosaic and is the same toggle as on the camera screen. Condividi and Salva do nothing until #66.
- [ ] **Never recomputed:** take a photo at 32 px², go back with Scarta, move the slider to 512 and take another: the second result's top bar reads `512 px²` and its Tessels are larger; the first one did not change while the slider moved (it has no slider).
- [ ] **Scarta:** goes back to the camera at once, without asking, and the preview is live again (the LIVE chip and its fps come back). The system back gesture on the result screen does the same; during processing it does nothing.
- [ ] **Landscape:** in landscape the processing state looks as in portrait, with the ring on the shutter in the right-hand column. The result screen keeps the top bar, the Mosaic fills the left, and Scarta, Condividi and Salva are a column on the right. Turning the phone on the result screen keeps the same Mosaic.
- [ ] **Background:** Home during processing, then back: the result screen (or the camera, if it failed) is shown, with no crash. Home on the result screen, then back: the same result.
- [ ] **Cache:** after a few photos, `adb shell run-as io.github.spippoli.hipsterismo ls cache/photos` is empty (each JPEG is deleted once decoded, and the folder at every start).
- [ ] **Memory:** after a few photos, `adb shell cat /proc/$(adb shell pidof io.github.spippoli.hipsterismo)/status | grep VmHWM` (the peak resident memory) stays under about 600 MB (#59).
