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
- [ ] The gallery thumbnail and the shutter are placeholders until #65: tapping them does nothing.
