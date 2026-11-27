#!/bin/sh
# `make test`: runs the verified test programs on your RTL and prints PASS/FAIL.
# Do not edit anything in tests/.   Logs: build/test/<name>.log, program output: .out
SIM=$1
FW=$2
out=build/test
mkdir -p "$out"
npass=0

run() {         # run NAME ARGS...  -> $rc, $cycles, $why
  name=$1; shift
  "$SIM" "$@" --max-cycles 400000000 > "$out/$name.out" 2> "$out/$name.log"
  rc=$?
  cycles=$(sed -n 's/^\[sim\] cycles \([0-9]*\).*/\1/p' "$out/$name.log")
  why=$(grep -m1 -E '^\[sim\] (TRAP|TIMEOUT|ERROR)' "$out/$name.log" | sed 's/^\[sim\] //')
}
unit_ok=0
row() {         # row LABEL PASS|FAIL DETAIL [needs1 = counts only once test 1 passes]
  if [ "$2" = PASS ] && [ -n "${4:-}" ] && [ $unit_ok -eq 0 ]; then
    printf '  %-30s %-5s %s\n' "$1" "-" "passes, but counts only once test 1 passes"
    return
  fi
  printf '  %-30s %-5s %s\n' "$1" "$2" "$3"
  [ "$2" = PASS ] && npass=$((npass + 1))
}

echo
echo "  TEST                           RESULT"
echo "  ------------------------------ ------"

run unit_tests --elf tests/elf/unit_tests.elf
groups=$(grep -c '^PASS ' "$out/unit_tests.out")
if [ $rc -eq 0 ]; then unit_ok=1; row "1. instructions (unit_tests)" PASS "12/12 instruction groups"
else row "1. instructions (unit_tests)" FAIL "$groups/12 groups pass ${why:+- $why}  (see build/test/unit_tests.out)"; fi

run attn_head --elf tests/elf/attn_head.elf --blob tests/data/attn_head.blob
if [ $rc -eq 0 ]; then row "2. one attention head" PASS ""
else row "2. one attention head" FAIL "${why:-wrong result}"; fi

run tiny_gpt --elf "$FW" --blob ref/export/tinychar.blob
if [ $rc -eq 0 ] && cmp -s "$out/tiny_gpt.out" tests/expected/tiny_gpt.txt; then
  row "3. full model (tiny_gpt)" PASS "$cycles cycles"
else row "3. full model (tiny_gpt)" FAIL "${why:-output differs from tests/expected/tiny_gpt.txt}"; fi

run ordering_stress --elf tests/elf/ordering_stress.elf
if [ $rc -eq 0 ] && cmp -s "$out/ordering_stress.out" tests/expected/ordering_stress.txt; then
  row "4. load/store ordering" PASS "" needs1
else row "4. load/store ordering" FAIL "${why:-output differs from tests/expected/ordering_stress.txt}"; fi

n=0; trapped=0
for e in tests/elf/reserved/*.elf; do
  n=$((n + 1))
  run reserved "--elf" "$e"
  [ $rc -eq 2 ] && ! grep -q "NO TRAP" "$out/reserved.out" && trapped=$((trapped + 1))
done
if [ $trapped -eq $n ]; then row "5. reserved encodings trap" PASS "$n/$n" needs1
else row "5. reserved encodings trap" FAIL "$trapped/$n trap"; fi

echo
echo "  $npass/5 passed."
echo
