#!/bin/sh
# Throwaway: run every variant on one CPU (taskset) and print hash + total, or
# with -v the full stage table. usage: run.sh [-v] REPS [profile args...] IMAGES
B=${BUILD:-.exp-build}
verbose=0
if [ "$1" = -v ]; then verbose=1; shift; fi
reps=$1
shift
for v in base pairs edges colour all allt4; do
    echo "== $v"
    if [ $verbose = 1 ]; then
        taskset -c ${CPUS:-2} "$B/v/$v" -r "$reps" "$@"
    else
        taskset -c ${CPUS:-2} "$B/v/$v" -r "$reps" "$@" | grep -E 'hash|median'
    fi
done
