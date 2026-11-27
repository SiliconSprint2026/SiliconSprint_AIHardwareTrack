#!/bin/sh
# make trace: commit traces from the RTL and from the ISS for the same ELF, then tracediff.
# Env (set by the Makefile): ELF, BLOB (may be empty), SIM, ISS, TRACEDIFF, MAX_CYCLES.
set -u
out=build/trace
name=$(basename "$ELF" .elf)
mkdir -p "$out"
blob_arg=""
[ -n "$BLOB" ] && blob_arg="--blob $BLOB"

echo ">> RTL: $out/$name.rtl.txt"
"$SIM" --elf "$ELF" $blob_arg --max-cycles "$MAX_CYCLES" --trace "$out/$name.rtl.txt" > "$out/$name.rtl.uart"
rtl_status=$?
echo "   RTL exit status $rtl_status, $(wc -l < "$out/$name.rtl.txt") lines"

# Build the ISS if its sources are there but the binary is not.
if [ ! -x "$ISS" ] && [ -f iss/Makefile ]; then make -s -C iss >/dev/null 2>&1; fi
if [ ! -x "$ISS" ]; then
  echo ">> ISS not found at $ISS: only the RTL trace was written"
  exit 2
fi
echo ">> ISS: $out/$name.iss.txt"
"$ISS" --elf "$ELF" $blob_arg --trace "$out/$name.iss.txt" > "$out/$name.iss.uart"
echo "   ISS exit status $?, $(wc -l < "$out/$name.iss.txt") lines"

if [ -f "$TRACEDIFF" ]; then
  python3 "$TRACEDIFF" "$out/$name.iss.txt" "$out/$name.rtl.txt"
  status=$?
else
  echo ">> $TRACEDIFF not found; falling back to cmp"
  if cmp "$out/$name.rtl.txt" "$out/$name.iss.txt"; then
    echo "traces identical: 0 mismatches"; status=0
  else
    status=1
  fi
fi
if cmp -s "$out/$name.rtl.uart" "$out/$name.iss.uart"; then echo ">> UART output identical"
else echo ">> UART output DIFFERS ($out/$name.rtl.uart vs $out/$name.iss.uart)"; status=1; fi
exit $status
