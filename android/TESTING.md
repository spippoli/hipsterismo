# Manual checks for the Android app

There are no automated UI tests (#59). Run this list by hand on the Pixel 8a before merging a change to the UI, the camera or the threading, and put the results in the PR. The full run with the performance pass takes about 15 minutes. #67 is the acceptance run of the finished app.

## Before you start

```sh
export JAVA_HOME=~/Android/jdk-21 ANDROID_HOME=~/Android/Sdk
alias adb=$ANDROID_HOME/platform-tools/adb    # the SDK's adb, not /usr/bin/adb
cd android && ./gradlew installDebug          # or: adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell content query --uri content://media/external/images/media --projection _id | wc -l   # the gallery's count, for the end
```

- `./gradlew connectedDebugAndroidTest` uninstalls the app, so a fresh install asks for the camera again. To skip the denial checks, grant it with `adb shell pm grant io.github.spippoli.hipsterismo android.permission.CAMERA`.
- Keep the phone unlocked for the whole run. A locked screen drops the preview to about 2 fps, and that is not a failure.
- Point the camera at an ordinary indoor scene, such as a desk, and keep the default `min patch area` of 32 px² for the performance figures.
- To come back to the app from adb, use the launcher's intent, which brings the running task to the front: `adb shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n io.github.spippoli.hipsterismo/.MainActivity`. A bare `am start -n …` stacks a new MainActivity on the old one, so it always shows the camera.

## Camera screen (#64)

- [ ] **Permission denied:** on a fresh install, deny the camera. The explanation appears ("Serve la fotocamera"), with no crash. "Consenti la fotocamera" asks again; after a second denial the button reads "Apri le impostazioni" and opens the app's settings. Granting the camera there and coming back shows the preview.
- [ ] **Permission granted:** the screen looks like the stock camera app: a top bar with the LIVE chip, the live Mosaic whole in a 3:4 viewfinder under it, then on black the `min patch area` row and the gallery button, the shutter and the camera switch. The Mosaic follows the camera upright. There is no Contour overlay button.
- [ ] **Stats:** a tap on the LIVE chip adds the core's ms per Frame and the Tessel count; another tap hides them. The fps is measured below (Performance).
- [ ] **`min patch area` row:** 8 steps, 8 to 1024 px², starting at 32, the selected one centred in a grey pill ("32 px²"). A tap on a step or a swipe along the row picks it (swipe left for larger) and the row slides to centre it. It changes the Patches: fewer and larger Tessels towards 1024.
- [ ] **Camera switch:** the button right of the shutter switches to the front camera (its arrows turn half a turn): the Mosaic is mirrored like a mirror, and back again. A front photo comes out mirrored as the preview showed it, upright.
- [ ] **Zoom:** the chips at the bottom of the viewfinder (0,5 / 1× / 2× on the back camera) jump to those ratios and the Mosaic follows; a pinch on the preview zooms smoothly between the camera's limits, and the lit chip reads the ratio (e.g. "1,4×"). A photo taken zoomed shows the zoomed framing. After a camera switch the zoom is back at 1×.
- [ ] **Landscape:** rotate the phone both ways (90° and 270°), with auto-rotate on and off. The layout stays in portrait, as in the stock camera app; the gallery and switch glyphs and the zoom labels turn the short way to read upright, and back when the phone is upright again. The Mosaic keeps showing the world upright. The row, the camera and the zoom stay as they were.
- [ ] **Background and resume:** Home, then back to the app (its icon, recents or the launcher intent above): the preview resumes, with the same slider.

## Photo to Mosaic (#65)

Watch the photo times with `adb logcat -s hipsterismo-photo:I`. Each photo logs one `photo camera|gallery WxH: N ms to the result (capture N, decode N, core N = N%, other N), …` line, timed from the shutter tap (or the picker's answer) to the result screen's first draw.

- [ ] **Shutter (processing state):** tap the shutter. The last preview Mosaic freezes and dims, the chip reads "in pausa" with an amber dot, a white ring spins around the gallery button, and the shutter, the `min patch area` row, the gallery button, the camera switch and the zoom chips are dimmed and ignore taps, as does a pinch (a second shutter tap too). The system back gesture does nothing. The result screen follows.
- [ ] **Upright:** take one photo holding the phone in portrait and one in each landscape turn. Each shows upright on the result screen, in the same orientation as the preview.
- [ ] **Upright with auto-rotate off:** with "Rotazione automatica" off, the screen stays in portrait. A photo taken holding the phone in either landscape turn is still a landscape photo (`2048x1536` in its `photo camera` line) and shows upright, turned on the portrait screen; a portrait photo is `1536x2048`.
- [ ] **Gallery:** the thumbnail opens the system photo picker, and no permission is asked. Cancelling it goes back to the live preview. Picking an image shows the same processing state, then its Mosaic, and logs a `photo gallery` line.
- [ ] **Gallery, EXIF rotation:** pick a portrait photo taken with the camera app (stored sideways with an EXIF Orientation). Its Mosaic shows upright.
- [ ] **Gallery, small image:** pick an image smaller than 2048 px on the long side, such as a screenshot. Its `photo gallery` line keeps its size.
- [ ] **Result screen:** like the Google Photos viewer: the Mosaic fits whole on black, not cropped. The top bar has back and the `min patch area` the photo was taken at (e.g. `32 px²`); no overlay toggle. At the bottom, Condividi, Salva and Scarta are glyphs over labels.
- [ ] **Never recomputed:** take a photo at 32 px², go back with Scarta, move the row to 512 and take another. The second result's top bar reads `512 px²` and its Tessels are larger. The first one did not change while the slider moved, since the result screen has no row.
- [ ] **Scarta:** goes back to the camera at once, without asking, and the preview is live again (the LIVE chip and its fps come back). The system back gesture on the result screen does the same.
- [ ] **Landscape:** the screen stays in portrait. Holding the phone in landscape on the result screen turns the Mosaic (it reads upright and takes the long side) and the glyphs and labels; turning back restores them. It is the same Mosaic throughout.
- [ ] **Background:** Home during processing, then back: the result screen (or the camera, if it failed) is shown, with no crash. Home on the result screen, then back: the same result.

## Save and share (#66)

Watch the export times with `adb logcat -s hipsterismo-photo:I` (one `export WxH: N ms` line per Salva or Condividi). List the saved Mosaics with:

```sh
adb shell content query --uri content://media/external/images/media \
  --projection _id:_display_name:relative_path:datetaken:width:height --where "relative_path='Pictures/Hipsterismo/'"
```

Delete every test image afterwards: `adb shell content delete --uri content://media/external/images/media/<_id>`.

- [ ] **Salva:** on a camera photo's result screen, tap Salva. The snackbar "Salvato in Immagini/Hipsterismo" with Apri appears just above the actions and goes after about 4 s. One row appears, `hipsterismo_YYYYMMDD_HHMMSS.png` (the local time of the save) in `Pictures/Hipsterismo/`, at the working size (e.g. 1536×2048 in portrait).
- [ ] **Salvato:** after Salva, the button is a dimmed "Salvato" with a tick. Tapping it adds no row.
- [ ] **What you see:** the saved PNG is the pure Mosaic, with no Contours. Pull it (`adb shell content read --uri content://media/external/images/media/<_id> > m.png`): `exiftool m.png` shows only PNG chunks (no EXIF, no GPS).
- [ ] **Apri:** the snackbar's Apri opens the PNG in Google Photos (or the chosen viewer).
- [ ] **Album:** Google Photos lists the Mosaic in the Hipsterismo album (Raccolta → Hipsterismo), dated at the save.
- [ ] **Condividi:** opens the system share sheet with a preview of the Mosaic. No new row appears in MediaStore, and `adb shell run-as io.github.spippoli.hipsterismo ls cache/shared` holds exactly one PNG.
- [ ] **Share to a real app:** send it to a recipient app (a chat to yourself, Gmail to yourself, or Drive). The image that arrives is the Mosaic, at the working size.
- [ ] **Scarta after Salva:** Scarta goes back to the camera without asking, and the next photo's result starts with Salva again.
- [ ] **Gallery photo Salva:** pick a photo from the gallery and tap Salva. The PNG is saved like a camera one, at the gallery Mosaic's size. Its `datetaken` reads NULL and Google Photos dates it at the save, not next to its original (decided in #66).

## Cache cleared at startup

- [ ] **Photos:** after a few photos, `adb shell run-as io.github.spippoli.hipsterismo ls cache/photos` is empty. Each JPEG is deleted once decoded, and the folder is emptied at every start.
- [ ] **Shared:** after a Condividi, `adb shell am force-stop io.github.spippoli.hipsterismo`, then start the app again (the launcher intent above). `adb shell run-as io.github.spippoli.hipsterismo ls cache/` no longer lists `shared` (or lists it empty).

## Performance (#59)

Pixel 8a, the default `min patch area` of 32 px², an ordinary indoor scene, the phone unlocked and not hot. A warm phone, for example one charging at 100%, caps the X3 and slows both the preview and the photos. Check before you start, and let the phone cool if either check fails:

```sh
adb shell dumpsys thermalservice | grep 'Thermal Status'                # 0
adb shell cat /sys/devices/system/cpu/cpu8/cpufreq/scaling_max_freq    # the X3's cap: about 2.9 GHz, not 1.9
```

| Measure | Threshold | Source |
|---|---|---|
| Preview fps | ≥ 15 sustained for 60 s, required; 30 is the target | the `preview` lines, one per second |
| Photo, shutter to result | ≤ 1.5 s at 2048 on the long side | the `photo camera` lines |
| Memory | no OOM or crash; TOTAL PSS under about 600 MB | `dumpsys meminfo`, with `VmHWM` recorded too |

- [ ] **Preview fps:** with the stats shown, leave the camera on the scene for at least 60 s, then summarise the `preview WxH: N fps, core N ms, …` lines (the decimal comma follows the phone's locale):

  ```sh
  adb logcat -c; sleep 65
  adb logcat -d -s hipsterismo:I | grep -oE '[0-9]+[.,][0-9] fps, core [0-9]+[.,][0-9] ms' | tr , . |
    sort -n | awk '{f[NR]=$1; c[NR]=$4} END {print NR " lines, min " f[1] " fps, median " f[int((NR+1)/2)] " fps, core " c[int((NR+1)/2)] " ms (median line)"}'
  ```

  Every line from the 60 s is at least 15 fps.
- [ ] **Photo time:** take five shutter photos, each followed by Scarta, with `adb logcat -c` first. Every `photo camera` line from `adb logcat -d -s hipsterismo-photo:I` says at most 1500 ms to the result, at about 2048×1536. Note the Tessel count too: very rich scenes (over about 1 500 Tessels) can reach 1.8 s (#73).
- [ ] **Shutter and Scarta save nothing:** the gallery's count (`adb shell content query --uri content://media/external/images/media --projection _id | wc -l`) is the same after the five photos as before them.
- [ ] **Peak memory:** after the photos, with the app still running:

  ```sh
  adb shell dumpsys meminfo io.github.spippoli.hipsterismo | grep -E 'TOTAL PSS'
  adb shell cat /proc/$(adb shell pidof io.github.spippoli.hipsterismo)/status | grep VmHWM
  ```

  TOTAL PSS, the measure #59 names, stays under about 600 MB. Record `VmHWM` too: it is the peak resident memory, and it reads higher than PSS because it counts shared and graphics pages in full.
- [ ] **No OOM or crash** during the whole run:

  ```sh
  adb logcat -d -b crash
  adb logcat -d | grep -iE 'OutOfMemoryError|FATAL EXCEPTION|lowmemorykiller.*hipsterismo|Fatal signal'
  ```

  Both print nothing for the app.

## At the end

- Delete the test images (Save and share above). The gallery's count is back where it started.
- `adb shell ls /data/local/tmp` holds nothing you left there.
