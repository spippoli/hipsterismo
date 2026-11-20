#!/usr/bin/env bash
# One leg of the emulator matrix (#109), on the booted emulator that adb sees
# (ANDROID_SERIAL picks one), run from the repository root or anywhere.
#
#   android/scripts/emulator-leg.sh full     # API 34+: tests, core, launch + Photo
#   android/scripts/emulator-leg.sh launch   # API 30-32: build, launch only
#
# full runs connectedDebugAndroidTest (CorePerformanceTest skips itself on an
# emulator, and below API 34 the golden-hash tests too, #99), core-on-device.sh
# and launch-photo-check.sh. Each step's output goes to the log, and the leg
# stops at the first failure. CI runs it inside android-emulator-runner, whose
# `script:` runs each line on its own under sh, hence one script.
#
# Exit code: 0 if every step passed, the failed step's code otherwise, 2 on a
# bad argument.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
cd "$here/.."

step() {
  echo "::group::$*"
  local code=0
  "$@" || code=$?
  echo "::endgroup::"
  if [ $code -ne 0 ]; then
    echo "::error::failed ($code): $*"
    exit $code
  fi
}

# Prints the instrumented tests' totals from the JUnit XML, which the Gradle
# log doesn't show.
test_totals() {
  local xml=app/build/outputs/androidTest-results/connected/debug
  cat "$xml"/*.xml 2>/dev/null | grep -o '<testsuite [^>]*' \
    | awk '{ for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2) {
               gsub(/"/, "", kv[2]); n[kv[1]] += kv[2] } }
           END { printf "instrumented tests: %d, %d failed, %d skipped\n",
                 n["tests"], n["failures"] + n["errors"], n["skipped"] }'
}

case "${1:-}" in
  full)
    step ./gradlew connectedDebugAndroidTest
    test_totals
    step scripts/core-on-device.sh
    # connectedDebugAndroidTest uninstalled the app; the check installs it
    # again from the APK that the tests built.
    step scripts/launch-photo-check.sh
    ;;
  launch)
    step ./gradlew assembleDebug
    step scripts/launch-photo-check.sh --launch-only
    ;;
  *)
    echo "usage: $0 full|launch" >&2
    exit 2
    ;;
esac
echo "leg $1: OK"
