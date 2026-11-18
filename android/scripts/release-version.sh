#!/usr/bin/env bash
# Turns a release tag into the app's version (#108):
#
#   android/scripts/release-version.sh v1.2.3
#   versionName=1.2.3
#   versionCode=10203
#
# The tag must be strict vMAJOR.MINOR.PATCH: decimal numbers without leading
# zeros, MINOR and PATCH under 100, so that versionCode = MAJOR·10000 +
# MINOR·100 + PATCH grows with every release. versionCode must be positive
# (v0.0.0 is refused) and at most 2 100 000 000, Android's cap (MAJOR at most
# 209 999). The two lines go to $GITHUB_OUTPUT as they are. Exit code 0 on a
# valid tag, 1 on an invalid one (with the reason on stderr), 2 on bad usage.
set -euo pipefail

if (( $# != 1 )); then
    echo "usage: $0 vMAJOR.MINOR.PATCH" >&2
    exit 2
fi
tag=$1

number='(0|[1-9][0-9]*)'
if [[ ! $tag =~ ^v${number}\.${number}\.${number}$ ]]; then
    echo "tag '$tag' is not vMAJOR.MINOR.PATCH (decimal, no leading zeros)" >&2
    exit 1
fi
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]}

if (( ${#minor} > 2 || ${#patch} > 2 )); then
    echo "tag '$tag': MINOR and PATCH must be under 100" >&2
    exit 1
fi
if (( ${#major} > 6 || major > 209999 )); then
    echo "tag '$tag': MAJOR must be at most 209999 (versionCode cap)" >&2
    exit 1
fi

code=$(( major * 10000 + minor * 100 + patch ))
if (( code < 1 )); then
    echo "tag '$tag': versionCode must be positive" >&2
    exit 1
fi

echo "versionName=$major.$minor.$patch"
echo "versionCode=$code"
