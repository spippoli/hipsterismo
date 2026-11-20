#!/usr/bin/env bash
# Launch the debug app on a device or emulator and take one shutter Photo
# (#109): the check passes if the camera screen draws a live Mosaic, the
# shutter gives a Result with Tessels, and the app never crashes. It checks no
# hash and no timing, so it holds on any emulator.
#
#   android/scripts/launch-photo-check.sh [--launch-only] [APK]
#
# APK defaults to the debug APK that assembleDebug builds; it is installed
# with -r -g (so CAMERA is granted and no dialog shows). --launch-only stops
# after the live Mosaic (the API 30 leg of CI). ANDROID_SERIAL picks the
# device. The app's process, logcat and screen are left as they are.
#
# Exit code: 0 on success, 1 if a check fails, 2 on bad arguments or a
# missing adb or APK.
set -euo pipefail

launch_only=0
if [ "${1:-}" = --launch-only ]; then
  launch_only=1
  shift
fi
if [ $# -gt 1 ]; then
  echo "usage: $0 [--launch-only] [APK]" >&2
  exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
apk=${1:-$here/../app/build/outputs/apk/debug/app-debug.apk}
adb=${ANDROID_HOME:+$ANDROID_HOME/platform-tools/}adb
pkg=io.github.spippoli.hipsterismo.debug

if ! command -v "$adb" > /dev/null; then
  echo "no adb (set ANDROID_HOME)" >&2
  exit 2
fi
if [ ! -f "$apk" ]; then
  echo "no APK at $apk (run ./gradlew assembleDebug)" >&2
  exit 2
fi

fail() {
  echo "FAIL: $*" >&2
  echo "--- logcat (app tags, errors) ---" >&2
  "$adb" logcat -d -s hipsterismo:I hipsterismo-photo:I Mosaic:W AndroidRuntime:E \
    CameraX:W "*:F" 2>/dev/null | tail -n 60 >&2 || true
  exit 1
}

# Waits up to $2 seconds for a logcat line matching the ERE $1 and prints it.
wait_for_line() {
  local pattern=$1 deadline=$((SECONDS + $2)) line
  while [ $SECONDS -lt $deadline ]; do
    line=$("$adb" logcat -d -v brief -s hipsterismo:I hipsterismo-photo:I 2>/dev/null \
      | grep -E "$pattern" | sed -n 1p || true)
    if [ -n "$line" ]; then
      printf '%s\n' "$line"
      return 0
    fi
    crashed && fail "the app crashed"
    sleep 1
  done
  return 1
}

crashed() {
  "$adb" logcat -d -b crash 2>/dev/null | grep -q "$pkg" && return 0
  "$adb" logcat -d -s AndroidRuntime:E 2>/dev/null | grep -q "Process: $pkg" && return 0
  return 1
}

"$adb" wait-for-device
echo "device: $("$adb" shell getprop ro.product.model | tr -d '\r'), API $("$adb" shell getprop ro.build.version.sdk | tr -d '\r')"
echo "install $apk"
"$adb" install -r -g "$apk" > /dev/null

# Start clean: no old process, no old lines. English labels, so the shutter's
# content description is known (per-app locales exist from API 33; below,
# both labels are tried).
"$adb" shell am force-stop "$pkg"
"$adb" logcat -c
"$adb" logcat -b crash -c 2>/dev/null || true
"$adb" shell cmd locale set-app-locales "$pkg" --locales en > /dev/null 2>&1 || true
"$adb" shell input keyevent KEYCODE_WAKEUP
"$adb" shell wm dismiss-keyguard > /dev/null 2>&1 || true
"$adb" shell monkey -p "$pkg" -c android.intent.category.LAUNCHER 1 > /dev/null 2>&1

# A `preview` line comes once a second while Mosaics are drawn.
preview=$(wait_for_line '^I/hipsterismo *\( *[0-9]+\): preview [0-9]+x[0-9]+: ' 90) \
  || fail "no live Mosaic within 90 s"
echo "live: ${preview#*: }"
tessels=$(sed -E 's/.*, ([0-9]+) Tessels.*/\1/' <<< "$preview")
[ "$tessels" -gt 0 ] || fail "the live Mosaic has no Tessels"

if [ $launch_only = 1 ]; then
  "$adb" shell pidof "$pkg" > /dev/null || fail "the app is not running"
  echo "OK: launch"
  exit 0
fi

# Prints the bounds "x0 y0 x1 y1" of the first node of the UI dump file $1
# whose attributes match the ERE $2, or nothing.
node_bounds() {
  tr '>' '\n' < "$1" | grep -E "$2" \
    | sed -nE 's/.*bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]".*/\1 \2 \3 \4/p' \
    | sed -n 1p || true
}

tap_centre() {
  local x0 y0 x1 y1
  read -r x0 y0 x1 y1 <<< "$1"
  "$adb" shell input tap $(((x0 + x1) / 2)) $(((y0 + y1) / 2))
}

# The shutter: its bounds from a UI dump, by its content description. A slow
# emulator often shows "System UI isn't responding" over the app; its Wait
# button dismisses it.
dump=/sdcard/hipsterismo-launch-check.xml
local_dump=$(mktemp)
trap 'rm -f "$local_dump"' EXIT
bounds=
for attempt in $(seq 1 10); do
  if "$adb" shell uiautomator dump "$dump" > /dev/null 2>&1 \
      && "$adb" shell cat "$dump" > "$local_dump"; then
    if grep -q "isn't responding" "$local_dump"; then
      wait_button=$(node_bounds "$local_dump" 'text="(Wait|Attendi)"')
      if [ -n "$wait_button" ]; then
        echo "dismissing a \"not responding\" dialog"
        tap_centre "$wait_button"
        sleep 2
        continue
      fi
    fi
    bounds=$(node_bounds "$local_dump" 'content-desc="(Take photo|Scatta)"')
  fi
  [ -n "$bounds" ] && break
  sleep 2
done
"$adb" shell rm -f "$dump"
[ -n "$bounds" ] || fail "no shutter in the UI dump"
echo "shutter at $bounds"
tap_centre "$bounds"

# The Result's first draw logs the photo line, with its Tessels.
photo=$(wait_for_line '^I/hipsterismo-photo *\( *[0-9]+\): photo camera ' 120) \
  || fail "no Result within 120 s of the shutter"
echo "photo: ${photo#*: }"
tessels=$(sed -E 's/.*, ([0-9]+) Tessels.*/\1/' <<< "$photo")
[ "$tessels" -gt 0 ] || fail "the Photo's Mosaic has no Tessels"

sleep 2
crashed && fail "the app crashed after the Result"
"$adb" shell pidof "$pkg" > /dev/null || fail "the app is not running"
echo "OK: launch and Photo"
