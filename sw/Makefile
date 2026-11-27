# Firmware.  bin/tiny_gpt.elf (Xtc kernels) and bin/baseline_sw.elf (plain RV32IM).
# Built automatically by the top-level `make test` / `make run`.

CROSS   ?= riscv32-unknown-elf-
CC      := $(CROSS)gcc
ARCH    := -march=rv32im -mabi=ilp32
DEFINES ?=
CFLAGS  := $(ARCH) -O2 -Wall -Wextra -ffreestanding -ffunction-sections -fdata-sections \
           -I. -Ikernels $(addprefix -D,$(DEFINES))
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -static -T link.ld -Wl,--gc-sections
LIBS    := -lgcc

KERNELS := $(wildcard kernels/*.c)
HEADERS := $(wildcard *.h kernels/*.h)
MODEL   := crt0.S model.c $(KERNELS)

.PHONY: all clean
all: bin/tiny_gpt.elf bin/baseline_sw.elf

bin/tiny_gpt.elf: $(MODEL) $(HEADERS) link.ld
	@mkdir -p bin
	$(CC) $(CFLAGS) -DUSE_XTC=1 $(LDFLAGS) -o $@ $(MODEL) $(LIBS)

bin/baseline_sw.elf: $(MODEL) $(HEADERS) link.ld
	@mkdir -p bin
	$(CC) $(CFLAGS) -DUSE_XTC=0 $(LDFLAGS) -o $@ $(MODEL) $(LIBS)

clean:
	rm -rf bin
