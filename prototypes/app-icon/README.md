# PROTOTYPE: hipsterismo's app icon

Throwaway branch for [App icon with PixelLab](https://github.com/spippoli/hipsterismo/issues/60). Not for `master`. When the Android app is built, copy `res/` into `android/app/src/main/res/` and set `android:icon="@mipmap/ic_launcher"` and `android:roundIcon="@mipmap/ic_launcher_round"`.

- `source-picto.png`: the chosen launcher pictogram. It is a low-poly instant camera in blue and amber, seen in slight three-quarter view. PixelLab `create_image_pro` made it at 512×512 with a transparent background (round 2, `picto-2`; job `d7428e8e-6649-48f9-9f8b-b564294a19c1`, seed 808). The prompts and the rejected candidates are in the issue.
- `source-full.png`: the detailed version (`full-2`), kept for a splash or About screen. It is not used in the icon.
- `make-icons.sh`: regenerates `res/` from `source-picto.png` with ImageMagick 6.
- `res/`: the adaptive icon.
  - **Background:** solid `#E3ECF8`, a light tint of the palette's blue.
  - **Foreground:** the trimmed camera, 52 dp wide, centred in the 108 dp layer. Its farthest pixel is about 32 dp from the centre, inside the 66 dp safe zone.
  - **Monochrome:** for themed icons on Android 13+, the launcher tints this layer. It is the camera's silhouette with the amber lens ring cut out.
  - PNG layers at mdpi, hdpi, xhdpi, xxhdpi and xxxhdpi: 108, 162, 216, 324 and 432 px.
  - No legacy `ic_launcher.png` files, because minSdk 29 always uses the adaptive icon.
- `preview.png`, from left to right:
  - the colour icon in a circular mask, at xxxhdpi;
  - the themed (monochrome) icon;
  - both at 56 px.

The source is pixel art, but each layer is a downscale of it (the foreground goes from 384 px trimmed to at most 208 px), so the stair-steps are gone at icon size. No vector redraw is needed.
