#!/usr/bin/env bash
# PROTOTYPE: builds the Android adaptive-icon layers from the PixelLab pictogram.
# Needs ImageMagick 6 (convert). Writes res/ next to this script.
set -euo pipefail
cd "$(dirname "$0")"
SRC=source-picto.png   # PixelLab create_image_pro, 512x512 RGBA (job d7428e8e-...)
FG_DP=52               # trimmed camera's side in dp; its farthest pixel lies ~32.2 dp from the centre, inside the 33 dp safe-zone radius
AMBER='#E49838'        # lens ring, lit side; both ring shades are cut out of the monochrome layer
AMBER_DARK='#99571F'   # lens ring, shaded side
rm -rf res && mkdir -p res
convert "$SRC" -trim +repage trimmed.png
convert trimmed.png -alpha extract -fill white -opaque white \( trimmed.png -alpha off -fuzz 12% -fill white -opaque "$AMBER" -opaque "$AMBER_DARK" -fill black +opaque white \) -compose minus_src -composite mono-mask.png
# densities: mdpi hdpi xhdpi xxhdpi xxxhdpi (1, 1.5, 2, 3, 4 px per dp)
for d in mdpi:1 hdpi:1.5 xhdpi:2 xxhdpi:3 xxxhdpi:4; do
  q=${d%%:*}; s=${d##*:}
  L=$(python3 -c "print(round(108*$s))"); F=$(python3 -c "print(round($FG_DP*$s))")
  mkdir -p res/mipmap-$q
  convert trimmed.png -filter Triangle -resize ${F}x${F} -background none -gravity center -extent ${L}x${L} res/mipmap-$q/ic_launcher_foreground.png
  convert mono-mask.png -filter Triangle -resize ${F}x${F} -background black -gravity center -extent ${L}x${L} \
    \( +clone -fill white -colorize 100 \) +swap -alpha off -compose copy_opacity -composite res/mipmap-$q/ic_launcher_monochrome.png
done
mkdir -p res/mipmap-anydpi-v26 res/values
for n in ic_launcher ic_launcher_round; do cat > res/mipmap-anydpi-v26/$n.xml <<XML
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background"/>
    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome"/>
</adaptive-icon>
XML
done
cat > res/values/ic_launcher_background.xml <<XML
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="ic_launcher_background">#E3ECF8</color>
</resources>
XML
rm trimmed.png mono-mask.png
