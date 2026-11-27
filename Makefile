# TENSOR-CORE. Run every command inside the container:  bash docker/run.sh make <target>
#
#   make test          build your RTL + firmware, run the 5 verified tests (PASS/FAIL)
#   make run           run the model on your RTL: text, cycles, cycles per kernel
#   make perf          Phase 2 numbers: cycles, clock period, T, area vs the cap, points
#   make synth         area and clock period only
#
#   optional debugging:
#   make trace ELF=..  compare your RTL with the instruction-set simulator, first mismatch
#   make wave  ELF=..  waveform of a run in build/wave/ (open it outside the container)
#   make lint          Verilator lint of rtl/
#
#   DEFINES="XTC_NO_FOO"  turns feature FOO off in RTL and firmware (any target)

ELF     ?= sw/bin/tiny_gpt.elf
BLOB    ?= ref/export/tinychar.blob
DEFINES ?=
export DEFINES

RTL     := $(sort $(wildcard rtl/*.sv rtl/*.v))
DFLAGS  := $(addprefix -D,$(DEFINES))
empty   :=
space   := $(empty) $(empty)
SIM_DIR := build/sim$(if $(strip $(DEFINES)),_$(subst $(space),_,$(strip $(DEFINES))))
SIM     := $(SIM_DIR)/Vsoc_top
FW      := sw/bin/tiny_gpt.elf

WAVE_EXT  := $(if $(wildcard /usr/include/zlib.h),fst,vcd)
WAVE_FLAG := $(if $(filter fst,$(WAVE_EXT)),--trace-fst,--trace)
VFLAGS := --cc --exe --build -j 0 --top-module soc_top -DRISCV_FORMAL $(DFLAGS) $(WAVE_FLAG) \
          --timescale 1ns/1ps -O3 -Wno-fatal sim/verilator.vlt -CFLAGS -O2

.PHONY: help test run perf synth sw trace wave lint clean
help:
	@sed -n '2,14p' Makefile | sed 's/^# \{0,1\}//'

$(SIM): $(RTL) sim/harness.cpp sim/elf_load.h sim/verilator.vlt
	@mkdir -p $(SIM_DIR)
	@echo ">> building the simulator (Verilator)"
	@verilator $(VFLAGS) -Mdir $(SIM_DIR)/obj -o ../Vsoc_top $(RTL) $(abspath sim/harness.cpp) \
	    > $(SIM_DIR)/build.log 2>&1 || { grep -E '%Error' $(SIM_DIR)/build.log | head -20; exit 1; }

sw:
	@$(MAKE) -s -C sw DEFINES="$(DEFINES)"

test: $(SIM) sw
	@sh tests/run_tests.sh $(SIM) $(FW)

run: $(SIM) sw
	$(SIM) --elf $(ELF) --blob $(BLOB) --profile

perf: $(SIM) sw
	@sh tests/perf.sh $(SIM) $(FW)

synth:
	@sh synth/run_synth.sh

trace: $(SIM) sw
	@$(MAKE) -s -C iss
	@ELF=$(ELF) BLOB="$(BLOB)" SIM=$(SIM) ISS=iss/build/xtc-iss TRACEDIFF=iss/tracediff \
	    MAX_CYCLES=400000000 sh sim/trace.sh

wave: $(SIM) sw
	@mkdir -p build/wave
	$(SIM) --elf $(ELF) --blob $(BLOB) --wave build/wave/run.$(WAVE_EXT)
	@echo "waveform: build/wave/run.$(WAVE_EXT)  (open with GTKWave or Surfer on your machine)"

lint:
	verilator --lint-only -Wall --timescale 1ns/1ps --top-module soc_top -DRISCV_FORMAL $(DFLAGS) sim/verilator.vlt $(RTL)
	@echo "lint OK"

clean:
	rm -rf build sw/bin iss/build
