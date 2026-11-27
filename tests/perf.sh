#!/bin/sh
# `make perf`: your Phase 2 numbers. Runs the full model (cycles) and synthesis (area,
# clock), then T = cycles x clock period and the performance points it would earn.
SIM=$1
FW=$2
T0=442936446.68        # ns
T1=32218547.956        # ns
AMAX=1063495           # um^2
mkdir -p build/test

"$SIM" --elf "$FW" --blob ref/export/tinychar.blob --max-cycles 400000000 \
    > build/test/perf.out 2> build/test/perf.log
ok=$?
cycles=$(sed -n 's/^\[sim\] cycles \([0-9]*\).*/\1/p' build/test/perf.log)
if [ $ok -ne 0 ] || ! cmp -s build/test/perf.out tests/expected/tiny_gpt.txt; then
  echo "the full model does not produce the expected output; fix that first (make test)"
  exit 1
fi

sh synth/run_synth.sh > build/test/synth.txt 2>&1 || { cat build/test/synth.txt; exit 1; }
area=$(sed -n 's/^AREA_UM2 *\([0-9.]*\).*/\1/p' build/test/synth.txt)
period=$(sed -n 's/^MIN_PERIOD_NS *\([0-9.]*\).*/\1/p' build/test/synth.txt)

awk -v c="$cycles" -v p="$period" -v a="$area" -v t0="$T0" -v t1="$T1" -v amax="$AMAX" 'BEGIN {
  T = c * p
  pts = 35 * log(t0 / T) / log(t0 / t1)
  if (pts > 40) pts = 40
  if (pts < 0) pts = 0
  if (a > amax) pts = 0
  printf "\n  cycles           %d\n", c
  printf "  clock period     %.3f ns\n", p
  printf "  T                %.3f ms   (cycles x period)\n", T / 1e6
  verdict = (a > amax) ? "OVER THE CAP" : "ok"
  printf "  area             %.0f um^2   (cap %d: %s)\n", a, amax, verdict
  printf "  performance pts  %.2f / 40   (estimate; hidden prompts must also be correct)\n\n", pts
}'
