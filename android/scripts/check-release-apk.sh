#!/usr/bin/env bash
# Checks that R8 kept the Mosaic constructor that mosaic_jni.cpp calls
# (GetMethodID("<init>", "(II[F[I[FJF)V")). Without the keep rule in
# app/src/main/keepRules/jni.keep, R8 empties the class and every Frame would
# throw NoSuchMethodError (#88, #105).
#
#   android/scripts/check-release-apk.sh [APK]
#
# APK defaults to the release APK (signed or unsigned) under app/build/outputs.
# Needs ANDROID_HOME (dexdump from the newest build-tools). Exit code 0 if the
# constructor is there, 1 if not, 2 on a missing APK or tool.
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
apk=${1:-}
if [[ -z $apk ]]; then
    for candidate in "$here"/app/build/outputs/apk/release/app-release.apk \
                     "$here"/app/build/outputs/apk/release/app-release-unsigned.apk; do
        if [[ -f $candidate ]]; then apk=$candidate; break; fi
    done
fi
if [[ -z $apk || ! -f $apk ]]; then
    echo "no release APK (run ./gradlew assembleRelease, or pass its path)" >&2
    exit 2
fi

: "${ANDROID_HOME:?set ANDROID_HOME}"
dexdump=$(ls -d "$ANDROID_HOME"/build-tools/*/dexdump 2>/dev/null | sort -V | tail -n 1)
if [[ -z $dexdump ]]; then
    echo "no dexdump under $ANDROID_HOME/build-tools" >&2
    exit 2
fi

class='Lio/github/spippoli/hipsterismo/Mosaic;'
signature='(II[F[I[FJF)V'

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
unzip -q -o "$apk" 'classes*.dex' -d "$work"

# dexdump prints each method as "name : '<init>'" then "type : '(…)V'".
found=0
for dex in "$work"/classes*.dex; do
    if "$dexdump" -d "$dex" 2>/dev/null | awk -v cls="$class" -v sig="$signature" '
        /Class descriptor/ { inside = index($0, "\x27" cls "\x27") > 0 }
        inside && /name *: \x27<init>\x27/ { init = 1; next }
        inside && init && /type *:/ { if (index($0, "\x27" sig "\x27") > 0) ok = 1; init = 0 }
        END { exit ok ? 0 : 1 }'; then
        found=1
        break
    fi
done

if (( found )); then
    echo "ok: $class <init>$signature is in $(basename "$apk")"
else
    echo "missing: $class <init>$signature is not in $(basename "$apk")" >&2
    exit 1
fi
