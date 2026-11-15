#!/system/bin/sh
# Throwaway: runs on the Pixel 8a from /data/local/tmp/hb53 (see run.sh).
# bench = unmodified Adaptive.cpp, bench-fix = eval-order fix (hash only).
cd /data/local/tmp/hb53
# Sections: facts pinned mov concurrent photos hashes sustained (default all).
# Run it detached (nohup ... &) so a dropped adb shell does not stop it.
SECTIONS=${*:-facts pinned mov concurrent photos hashes sustained}
want() { case " $SECTIONS " in *" $1 "*) return 0;; esac; return 1; }
temps() { dumpsys thermalservice | grep -E "mName=(BIG|MID|LITTLE|soc_therm|VIRTUAL-SKIN)," | head -5; }
if want facts; then
echo "== facts"
for c in 0 4 8; do echo "cpu$c max $(cat /sys/devices/system/cpu/cpu$c/cpufreq/cpuinfo_max_freq)"; done
grep -m1 Features /proc/cpuinfo
grep "CPU part" /proc/cpuinfo | sort | uniq -c
fi
if want pinned; then
echo "== pinned fingers"
for m in 1 10 100; do echo "## mask $m"; taskset $m ./bench --reps 30 fix/fingers.ppm | grep -E "RESULT|STAGES|MEMORY"; done
fi
if want mov; then
echo "== mov unpinned"
./bench --reps 10 --warmup 2 mov/*.ppm | grep -E "RESULT|STAGES"
fi
if want concurrent; then
echo "== concurrent"
for n in 1 2 3; do
  echo "## $n processes"
  i=0; while [ $i -lt $n ]; do ./bench --sustained 10 fix/fingers.ppm > conc$i.txt 2>&1 & i=$((i+1)); done
  wait
  cat conc*.txt | grep SUSTAINED | awk -v n=$n '{split($4,a,"="); s+=a[2]} END {print "aggregate fps", s/10, "with", n}'
  rm conc*.txt
done
fi
if want photos; then
echo "== photos"
for d in 1024 2048 full; do for f in photos/$d/*.ppm; do
  ./bench --reps 5 --warmup 1 --budget 40 $f | grep -E "RESULT|STAGES|MEMORY" || echo "FAILED $f"
done; done
fi
if want hashes; then
echo "== hashes (bench-fix)"
./bench-fix --reps 1 --warmup 0 fix/*.ppm mov/*.ppm photos/1024/*.ppm photos/2048/*.ppm photos/full/*.ppm | grep RESULT
fi
if want sustained; then
echo "== sustained 60 s"; temps
./bench --sustained 60 fix/fingers.ppm | grep SUSTAINED
temps
fi
echo "== end"
