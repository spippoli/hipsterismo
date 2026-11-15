#!/usr/bin/env bash
# Runs the core tests (tests/core/) on the phone and compares every hash with
# the desktop's. Run it by hand before merging a change to src/mosaic/core/ or
# to the native code under android/, and paste its output in the PR's Evidence.
#
#   android/scripts/core-on-device.sh
#
# It builds tests/core/ for the desktop (GCC) and cross-compiles it for
# arm64-v8a with the NDK (into tests/core/build-android-<NDK version>/),
# pushes the binary and tests/fixtures/ to /data/local/tmp/hipsterismo-core/
# (removed again on exit), runs both and prints one line per fixture. The fixtures are the three PNGs plus the
# synthetic 2048 px still the tests build from fingers.png; never user photos.
# Exit code: 0 if every hash matches, 1 otherwise.
#
# Environment: ANDROID_HOME (default ~/Android/Sdk), NDK_VERSION (default
# 30.0.16248370), ANDROID_SERIAL to pick a device when several are connected.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
sdk=${ANDROID_HOME:-$HOME/Android/Sdk}
ndk=$sdk/ndk/${NDK_VERSION:-30.0.16248370}
cmake=$sdk/cmake/3.31.6/bin/cmake
adb=$sdk/platform-tools/adb
remote=/data/local/tmp/hipsterismo-core

for tool in "$cmake" "$adb" "$ndk/build/cmake/android.toolchain.cmake"; do
    [[ -e $tool ]] || { echo "missing: $tool" >&2; exit 1; }
done

echo "== desktop build"
"$cmake" -S "$repo/tests/core" -B "$repo/tests/core/build" > /dev/null
"$cmake" --build "$repo/tests/core/build" -j > /dev/null

echo "== arm64-v8a build (NDK $(basename "$ndk"))"
"$cmake" -S "$repo/tests/core" -B "$repo/tests/core/build-android-$(basename "$ndk")" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static > /dev/null
"$cmake" --build "$repo/tests/core/build-android-$(basename "$ndk")" -j > /dev/null

echo "== device $("$adb" shell getprop ro.product.model | tr -d '\r') (Android $("$adb" shell getprop ro.build.version.release | tr -d '\r'))"
trap '"$adb" shell rm -rf "$remote" || true' EXIT
"$adb" shell mkdir -p "$remote/fixtures"
"$adb" push -q "$repo/tests/core/build-android-$(basename "$ndk")/core_tests" "$remote/"
"$adb" push -q "$repo"/tests/fixtures/*.png "$remote/fixtures/"

desktop=$("$repo/tests/core/build/core_tests" "$repo/tests/fixtures" || true)
phone=$("$adb" shell "cd $remote && ./core_tests fixtures" | tr -d '\r' || true)

hashes() { grep '^hash ' | awk '{print $2, $3}'; }
echo
printf '%-32s %-20s %-20s %s\n' fixture desktop phone same
status=0
while read -r name hash; do
    other=$(hashes <<< "$phone" | awk -v n="$name" '$1 == n {print $2}')
    if [[ $hash == "$other" ]]; then same=yes; else same=NO; status=1; fi
    printf '%-32s %-20s %-20s %s\n' "$name" "$hash" "${other:-missing}" "$same"
done < <(hashes <<< "$desktop")
echo
echo "desktop: $(tail -n 1 <<< "$desktop")"
echo "phone:   $(tail -n 1 <<< "$phone")"
if ! diff <(echo "$desktop") <(echo "$phone") > /dev/null; then
    echo "the full outputs differ:"
    diff <(echo "$desktop") <(echo "$phone") || true
    status=1
fi
exit $status
