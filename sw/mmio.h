// SoC memory-mapped I/O at 0x2000_0000.
#pragma once
#include <stdint.h>

#define MMIO_UART    (*(volatile uint32_t *)0x20000000u)  // write a byte to the console
#define MMIO_HALT    (*(volatile uint32_t *)0x20000004u)  // write the exit code; the run stops
#define MMIO_CYCLES  (*(volatile uint32_t *)0x20000008u)  // read: cycles since reset
#define MMIO_PROFILE (*(volatile uint32_t *)0x2000000Cu)  // write a kernel id on entry, 0 on exit

// Kernel ids for PROFILE (the harness sums cycles per id).
enum { PROF_OTHER = 0, PROF_MATVEC = 1, PROF_PREFILL = 2, PROF_ATTENTION = 3,
       PROF_SOFTMAX = 4, PROF_RMSNORM = 5 };

#ifndef HOST
static inline void putchar_(char c) { MMIO_UART = (uint8_t)c; }
static inline uint32_t cycles(void) { return MMIO_CYCLES; }
static inline void prof(uint32_t id) { MMIO_PROFILE = id; }
static inline void halt(uint32_t code) { MMIO_HALT = code; for (;;) {} }
#else   // host build (sw/host/): plain stdio, no profiling
#include <stdio.h>
#include <stdlib.h>
static inline void putchar_(char c) { putchar(c); }
static inline uint32_t cycles(void) { return 0; }
static inline void prof(uint32_t id) { (void)id; }
static inline void halt(uint32_t code) { exit((int)code); }
#endif
static inline void puts_(const char *s) { while (*s) putchar_(*s++); }

static inline void put_hex(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) putchar_("0123456789abcdef"[(v >> i) & 15]);
}
static inline void put_dec(uint32_t v) {
    char buf[11]; int n = 0;
    do { buf[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) putchar_(buf[--n]);
}
