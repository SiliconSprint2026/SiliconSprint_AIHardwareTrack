// RV32IM core plus the Xtc coprocessor state.
#pragma once
#include <cstdint>
#include <string>

#include "mem.h"

struct VReg {
    uint8_t b[16] = {};   // b[i] is lane .b[i]; .w[k] is bytes 4k..4k+3, little-endian
};

// What one retired instruction wrote; this is exactly one trace line.
struct Retire {
    uint32_t pc = 0, insn = 0;
    int rd = 0;            // 0: no scalar write (writes to x0 are not reported)
    uint32_t rd_val = 0;
    int vd = -1;           // -1: no vector write
    VReg vd_val;
};

enum class Step { Ok, Illegal, Fault };

struct Cpu {
    Mem& mem;
    uint32_t pc = 0;
    uint32_t x[32] = {};
    VReg v[16];
    uint64_t instret = 0;
    bool test_mode = false;      // --riscv-tests: Zicsr, ECALL traps to mtvec, MRET
    uint32_t csr[4096] = {};
    std::string error;           // set when step() does not return Ok

    explicit Cpu(Mem& m) : mem(m) { mem.instret = &instret; }

    // Execute one instruction. On Ok it retired and r describes it. Otherwise nothing
    // changed and `error` says why.
    Step step(Retire& r);

    void write_x(Retire& r, int rd, uint32_t val) {
        if (rd == 0) return;
        x[rd] = val;
        r.rd = rd;
        r.rd_val = val;
    }
    void write_v(Retire& r, int vd, const VReg& val) {
        v[vd] = val;
        r.vd = vd;
        r.vd_val = val;
    }
};

// Xtc decode + execute (xtc.cpp). Returns false for a reserved encoding; then nothing
// has changed. May throw Fault (XLD/XST outside DTCM) before changing anything.
bool xtc_execute(Cpu& c, uint32_t insn, Retire& r);

// "PC INSN RD VD", see docs/ISA_SPEC.md, "Trace format".
std::string trace_line(const Retire& r);
