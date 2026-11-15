#!/bin/bash
# Throwaway (#53): build the benchmark for the desktop and the Pixel 8a,
# prepare raw Frames, run on both. Paths are this machine's.
#   OUT     scratch directory (builds, OpenCV, Frames, results); never in the repo
#   PHOTOS  folder of JPEGs; they and their resized copies are never committed
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$HERE/../..
OUT=${OUT:?set OUT to a scratch directory}
PHOTOS=${PHOTOS:-$HOME/Downloads/rd}
export ANDROID_HOME=${ANDROID_HOME:-$HOME/Android/Sdk}
NDK=$ANDROID_HOME/ndk/29.0.14206865
export PATH=$ANDROID_HOME/cmake/3.31.6/bin:$PATH
ADB=$ANDROID_HOME/platform-tools/adb
GLM=${GLM:-$HOME/Applications/of/libs/glm/include}
T=/data/local/tmp/hb53

# OpenCV 4.10.0 (the desktop's version), core+imgproc, static, arm64-v8a.
[ -d $OUT/src/opencv ] || git clone --depth 1 --branch 4.10.0 https://github.com/opencv/opencv $OUT/src/opencv
cmake -S $OUT/src/opencv -B $OUT/build/ocv-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_LIST=core,imgproc -DBUILD_SHARED_LIBS=OFF -DBUILD_JAVA=OFF \
  -DBUILD_ANDROID_PROJECTS=OFF -DBUILD_ANDROID_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF \
  -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF -DWITH_KLEIDICV=OFF -DWITH_OPENCL=OFF -DWITH_IPP=OFF -DWITH_ITT=OFF \
  -DWITH_TIFF=OFF -DWITH_JPEG=OFF -DWITH_PNG=OFF -DWITH_WEBP=OFF -DWITH_OPENJPEG=OFF -DWITH_OPENEXR=OFF \
  -DWITH_JASPER=OFF -DWITH_PROTOBUF=OFF -DBUILD_PROTOBUF=OFF -DWITH_ADE=OFF -DWITH_FLATBUFFERS=OFF \
  -DCMAKE_INSTALL_PREFIX=$OUT/install/ocv-android
cmake --build $OUT/build/ocv-android
cmake --install $OUT/build/ocv-android

# The harness: desktop (GCC, system OpenCV 4.10.0) and phone (NDK clang 21).
cmake -S $HERE -B $OUT/build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLM_INCLUDE_DIR=$GLM
cmake --build $OUT/build/desktop
ANDROID_ARGS=(-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a
  -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR=$OUT/install/ocv-android/sdk/native/jni
  -DGLM_INCLUDE_DIR=$GLM)
cmake -S $HERE -B $OUT/build/android -G Ninja "${ANDROID_ARGS[@]}"
cmake --build $OUT/build/android
# Variant: triangulate()'s two vertex() calls sequenced explicitly, in GCC's
# order (right to left), so clang builds the same CDT input as GCC.
mkdir -p $OUT/variant
python3 "$HERE/eval-order-fix.py" "$REPO/src/mosaic/Adaptive.cpp" "$OUT/variant/Adaptive.cpp"
cmake -S $HERE -B $OUT/build/android-fix -G Ninja "${ANDROID_ARGS[@]}" -DADAPTIVE_CPP=$OUT/variant/Adaptive.cpp
cmake --build $OUT/build/android-fix

# Raw Frames (binary PPM).
mkdir -p $OUT/fix $OUT/mov $OUT/photos/1024 $OUT/photos/2048 $OUT/photos/full
for f in fingers fingers-003 fingers-060; do
  magick $REPO/tests/fixtures/$f.png -depth 8 -type TrueColor ppm:$OUT/fix/$f.ppm
done
ffmpeg -v error -y -i $REPO/bin/data/fingers.mov -vf "select=not(mod(n\,5))" -vsync vfr -pix_fmt rgb24 $OUT/mov/f%03d.ppm
i=0
for f in $PHOTOS/*.jpg; do
  i=$((i + 1)); n=p$(printf %02d $i)
  magick "$f" -auto-orient -resize 1024x1024 -depth 8 ppm:$OUT/photos/1024/$n.ppm
  if [ $((i % 2)) = 1 ]; then magick "$f" -auto-orient -resize 2048x2048 -depth 8 ppm:$OUT/photos/2048/$n.ppm; fi
  case $i in 1 | 4 | 7 | 9) magick "$f" -auto-orient -depth 8 ppm:$OUT/photos/full/$n.ppm ;; esac
done

# Desktop.
mkdir -p $OUT/results
B=$OUT/build/desktop/bench
$B --reps 50 $OUT/fix/*.ppm > $OUT/results/desk-fix.txt
$B --reps 10 --warmup 2 $OUT/mov/*.ppm > $OUT/results/desk-mov.txt
$B --reps 5 --warmup 1 --budget 30 $OUT/photos/1024/*.ppm $OUT/photos/2048/*.ppm > $OUT/results/desk-photos.txt
for f in $OUT/photos/full/*.ppm; do $B --reps 3 --warmup 1 $f; done >> $OUT/results/desk-photos.txt
$B --reps 1 --warmup 0 $OUT/fix/*.ppm $OUT/mov/*.ppm $OUT/photos/*/*.ppm | grep RESULT > $OUT/results/desk-hashes.txt

# Phone, detached so that a dropped adb shell does not stop it.
$ADB shell mkdir -p $T
$ADB push $OUT/build/android/bench $T/bench
$ADB push $OUT/build/android-fix/bench $T/bench-fix
$ADB push $HERE/phone-run.sh $T/
for d in fix mov photos; do $ADB push $OUT/$d $T/; done
$ADB shell "cd $T && nohup sh phone-run.sh > out.txt 2>&1 < /dev/null &"
until $ADB shell "grep -q '== end' $T/out.txt"; do sleep 20; done
$ADB pull $T/out.txt $OUT/results/phone.txt
$ADB shell rm -rf $T

awk -f $HERE/summarise.awk $OUT/results/desk-mov.txt $OUT/results/desk-photos.txt
awk -f $HERE/summarise.awk $OUT/results/phone.txt
