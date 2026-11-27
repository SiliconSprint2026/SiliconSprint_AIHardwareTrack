# Yosys synthesis of soc_top for sky130_fd_sc_hd, typical corner (tt_025C_1v80).
#   yosys -c synth/synth.tcl     (run by synth/run_synth.sh; env SYNTH_LIB = liberty file)
# The TCMs (rtl/tcm.sv) are read as blackboxes: they are SRAM macros, not logic.
# Output: build/synth/soc_top.v (gate-level netlist) and build/synth/area.txt.
yosys -import
# run_synth.sh passes a copy of the liberty with the lpflow_* and probe_* cells marked
# dont_use (power-gating and debug cells, not for general logic).
set lib $::env(SYNTH_LIB)

# Every file in rtl/ except the TCM models; DEFINES (space-separated) become -D flags.
set defs {}
if {[info exists ::env(DEFINES)]} {
  foreach d $::env(DEFINES) { lappend defs "-D$d" }
}
read_verilog -sv -lib rtl/tcm.sv
foreach f [lsort [glob rtl/*.v rtl/*.sv]] {
  if {$f eq "rtl/tcm.sv"} continue
  if {[file extension $f] eq ".sv"} { read_verilog -sv {*}$defs $f } else { read_verilog {*}$defs $f }
}

synth -top soc_top -flatten
dfflibmap -liberty $lib
# Plain mapping, then ABC buffers high-fanout nets and sizes gates against a 10 ns target.
# (The default script maps a coprocessor-sized design in 30+ minutes; this takes ~2.)
abc -D 10000 -script "+strash;map,{D};topo;buffer,-p;upsize,{D};dnsize,{D};stime,-p" -liberty $lib
opt_clean -purge

# Constants become tie cells, so the netlist is plain cell instances for OpenSTA.
hilomap -singleton -hicell sky130_fd_sc_hd__conb_1 HI -locell sky130_fd_sc_hd__conb_1 LO
setundef -zero
splitnets -ports -format __
opt_clean -purge

tee -o build/synth/area.txt stat -liberty $lib
write_verilog -noattr -noexpr -nohex -nodec build/synth/soc_top.v
