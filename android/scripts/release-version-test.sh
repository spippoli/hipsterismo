#!/usr/bin/env bash
# Tests of release-version.sh (#108). Run it from anywhere:
#
#   android/scripts/release-version-test.sh
#
# Exit code: the number of failed checks.
set -uo pipefail

script="$(cd "$(dirname "$0")" && pwd)/release-version.sh"
failures=0

# accepts TAG NAME CODE
accepts() {
    local out
    if ! out=$("$script" "$1" 2>&1); then
        echo "FAIL $1: refused ($out)"; failures=$((failures + 1)); return
    fi
    local want=$'versionName='"$2"$'\nversionCode='"$3"
    if [[ $out != "$want" ]]; then
        echo "FAIL $1: got '$out', want '$want'"; failures=$((failures + 1)); return
    fi
    echo "ok   $1 -> $2 / $3"
}

# refuses TAG
refuses() {
    local out code
    out=$("$script" "$1" 2>&1); code=$?
    if (( code != 1 )); then
        echo "FAIL $1: exit $code, want 1 ($out)"; failures=$((failures + 1)); return
    fi
    echo "ok   $1 refused: $out"
}

accepts v0.0.1 0.0.1 1
accepts v0.1.0 0.1.0 100
accepts v0.1.2 0.1.2 102
accepts v1.2.3 1.2.3 10203
accepts v1.99.99 1.99.99 19999
accepts v2.0.0 2.0.0 20000
accepts v12.34.56 12.34.56 123456
accepts v209999.99.99 209999.99.99 2099999999

refuses v1.2
refuses v1
refuses v1.200.0
refuses v1.2.100
refuses v1.100.0
refuses v1.02.3
refuses v01.2.3
refuses v1.2.03
refuses v1.2.3-rc
refuses v1.2.3-rc.1
refuses v1.2.3.4
refuses 1.2.3
refuses V1.2.3
refuses v0.0.0
refuses v210000.0.0
refuses v1234567.0.0
refuses ' v1.2.3'
refuses 'v1.2.3 '
refuses v1.2.x
refuses v
refuses ''

out=$("$script" 2>&1); code=$?
if (( code == 2 )); then echo "ok   no argument: exit 2"; else echo "FAIL no argument: exit $code"; failures=$((failures + 1)); fi

echo "$failures failed"
exit "$failures"
