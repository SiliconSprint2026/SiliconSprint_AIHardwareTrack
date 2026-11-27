#!/bin/sh
# make synth: Yosys (synth + abc, sky130_fd_sc_hd tt) then OpenSTA.
# Prints the cell area in um^2 (TCMs excluded) and the minimum clock period in ns.
# Logs and reports: build/synth/{yosys.log, area.txt, sta.log, critical_path.txt}.
set -u
out=build/synth
mkdir -p "$out"
: "${SKY130_LIB:?SKY130_LIB is not set (run inside the toolchain image)}"

# Copy of the liberty with power-gating (lpflow_*) and debug (probe*) cells marked dont_use.
export SYNTH_LIB="$out/sky130_hd_tt_synth.lib"
sed '/cell ("sky130_fd_sc_hd__\(lpflow_\|probe\)/a\        dont_use : true;' "$SKY130_LIB" > "$SYNTH_LIB"

echo ">> yosys (log: $out/yosys.log)"
start=$(date +%s)
yosys -q -l "$out/yosys.log" -c synth/synth.tcl > /dev/null 2>&1 || { tail -20 "$out/yosys.log"; echo "yosys failed"; exit 1; }
echo "   done in $(( $(date +%s) - start )) s"

echo ">> opensta (log: $out/sta.log)"
sta -no_init -exit synth/sta.tcl > "$out/sta.log" 2>&1 || { cat "$out/sta.log"; echo "sta failed"; exit 1; }
if grep -qi "^error" "$out/sta.log"; then cat "$out/sta.log"; echo "sta reported errors"; exit 1; fi

area=$(grep "Chip area for module" "$out/area.txt" | awk '{print $NF}')
cells=$(grep "Number of cells:" "$out/area.txt" | tail -1 | awk '{print $NF}')
period=$(sed -n 's/^MIN_PERIOD_NS //p' "$out/sta.log")
if [ -z "$area" ] || [ -z "$period" ]; then cat "$out/sta.log"; echo "could not parse results"; exit 1; fi

echo ">> critical path ($out/critical_path.txt)"
echo "   from $(sed -n 's/^Startpoint: //p' "$out/critical_path.txt" | head -1)"
echo "   to   $(sed -n 's/^Endpoint: //p' "$out/critical_path.txt" | head -1)"
echo "AREA_UM2      $area   ($cells cells, TCMs excluded)"
echo "MIN_PERIOD_NS $period   ($(awk "BEGIN{printf \"%.1f\", 1000/$period}") MHz)"
