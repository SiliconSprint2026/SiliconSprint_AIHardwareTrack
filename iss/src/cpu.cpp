// RV32IM interpreter. One call to step() = one instruction.
#include <cstdio>

#include "cpu.h"

namespace {

int32_t imm_i(uint32_t i) { return int32_t(i) >> 20; }
int32_t imm_s(uint32_t i) { return (int32_t(i) >> 25 << 5) | ((i >> 7) & 31); }
int32_t imm_b(uint32_t i) {
    return (int32_t(i) >> 31 << 12) | ((i >> 7) & 1) << 11 | ((i >> 25) & 63) << 5 | ((i >> 8) & 15) << 1;
}
int32_t imm_j(uint32_t i) {
    return (int32_t(i) >> 31 << 20) | (i & 0xFF000) | ((i >> 20) & 1) << 11 | ((i >> 21) & 0x3FF) << 1;
}

// Base integer ALU (OP and OP-IMM). `alt` = funct7 bit 5 (SUB / SRA).
uint32_t alu(uint32_t f3, bool alt, uint32_t a, uint32_t b) {
    switch (f3) {
    case 0: return alt ? a - b : a + b;
    case 1: return a << (b & 31);
    case 2: return int32_t(a) < int32_t(b);
    case 3: return a < b;
    case 4: return a ^ b;
    case 5: return alt ? uint32_t(int32_t(a) >> (b & 31)) : a >> (b & 31);
    case 6: return a | b;
    default: return a & b;
    }
}

// M extension, including the RISC-V rules for divide by zero and overflow.
uint32_t muldiv(uint32_t f3, uint32_t a, uint32_t b) {
    int64_t sa = int32_t(a), sb = int32_t(b);
    switch (f3) {
    case 0: return a * b;                                             // MUL
    case 1: return uint32_t(uint64_t(sa * sb) >> 32);                 // MULH
    case 2: return uint32_t(uint64_t(sa * int64_t(b)) >> 32);         // MULHSU
    case 3: return uint32_t((uint64_t(a) * b) >> 32);                 // MULHU
    case 4: return b == 0 ? ~0u : (sa == INT32_MIN && sb == -1) ? a : uint32_t(sa / sb);  // DIV
    case 5: return b == 0 ? ~0u : a / b;                              // DIVU
    case 6: return b == 0 ? a : (sa == INT32_MIN && sb == -1) ? 0 : uint32_t(sa % sb);    // REM
    default: return b == 0 ? a : a % b;                               // REMU
    }
}

bool branch_taken(uint32_t f3, uint32_t a, uint32_t b) {
    switch (f3) {
    case 0: return a == b;
    case 1: return a != b;
    case 4: return int32_t(a) < int32_t(b);
    case 5: return int32_t(a) >= int32_t(b);
    case 6: return a < b;
    default: return a >= b;   // 7
    }
}

}  // namespace

Step Cpu::step(Retire& r) {
    r = Retire{};
    uint32_t insn = 0;
    try {
        insn = mem.fetch(pc);
        r.pc = pc;
        r.insn = insn;
        const uint32_t opc = insn & 0x7F, rd = (insn >> 7) & 31, f3 = (insn >> 12) & 7;
        const uint32_t rs1 = (insn >> 15) & 31, rs2 = (insn >> 20) & 31, f7 = insn >> 25;
        const uint32_t a = x[rs1], b = x[rs2];
        uint32_t next = pc + 4;
        bool ok = true;

        switch (opc) {
        case 0x37: write_x(r, rd, insn & 0xFFFFF000); break;              // LUI
        case 0x17: write_x(r, rd, pc + (insn & 0xFFFFF000)); break;       // AUIPC
        case 0x6F: next = pc + imm_j(insn); break;                         // JAL (link below)
        case 0x67:                                                         // JALR (link below)
            ok = f3 == 0;
            next = (a + imm_i(insn)) & ~1u;
            break;
        case 0x63:                                                         // branches
            ok = f3 != 2 && f3 != 3;
            if (ok && branch_taken(f3, a, b)) next = pc + imm_b(insn);
            break;
        case 0x03: {                                                       // loads
            uint32_t addr = a + imm_i(insn);
            switch (f3) {
            case 0: write_x(r, rd, uint32_t(int8_t(mem.load(addr, 1)))); break;
            case 1: write_x(r, rd, uint32_t(int16_t(mem.load(addr, 2)))); break;
            case 2: write_x(r, rd, mem.load(addr, 4)); break;
            case 4: write_x(r, rd, mem.load(addr, 1)); break;
            case 5: write_x(r, rd, mem.load(addr, 2)); break;
            default: ok = false;
            }
            break;
        }
        case 0x23:                                                         // stores
            ok = f3 <= 2;
            if (ok) mem.store(a + imm_s(insn), 1 << f3, b);
            break;
        case 0x13: {                                                       // OP-IMM
            uint32_t imm = uint32_t(imm_i(insn));
            if (f3 == 1) ok = f7 == 0;
            if (f3 == 5) ok = f7 == 0 || f7 == 0x20;
            if (ok) write_x(r, rd, alu(f3, f3 == 5 && f7 == 0x20, a, f3 == 1 || f3 == 5 ? rs2 : imm));
            break;
        }
        case 0x33:                                                         // OP
            if (f7 == 1) write_x(r, rd, muldiv(f3, a, b));
            else if (f7 == 0 || (f7 == 0x20 && (f3 == 0 || f3 == 5))) write_x(r, rd, alu(f3, f7 == 0x20, a, b));
            else ok = false;
            break;
        case 0x0F:                                                         // FENCE (a no-op here)
            ok = f3 == 0;
            break;
        case 0x73:                                                         // SYSTEM
            if (!test_mode) {
                if (insn == 0x00000073) throw Fault("ECALL");
                if (insn == 0x00100073) throw Fault("EBREAK");
                ok = false;
            } else if (insn == 0x00000073) {                               // ECALL -> mtvec
                csr[0x341] = pc;                                           // mepc
                csr[0x342] = 11;                                           // mcause: M-mode ecall
                next = csr[0x305] & ~3u;                                   // mtvec
            } else if (insn == 0x30200073) {                               // MRET
                next = csr[0x341];
            } else if ((f3 & 3) != 0) {                                    // Zicsr
                uint32_t n = insn >> 20, old = csr[n];
                uint32_t src = (f3 & 4) ? rs1 : a;                         // CSRR*I use the rs1 field
                if ((f3 & 3) == 1) csr[n] = src;
                if ((f3 & 3) == 2 && rs1) csr[n] = old | src;
                if ((f3 & 3) == 3 && rs1) csr[n] = old & ~src;
                csr[0xF14] = 0;                                            // mhartid stays 0
                write_x(r, rd, old);
            } else {
                ok = false;
            }
            break;
        case 0x0B:
        case 0x2B:
            ok = xtc_execute(*this, insn, r);
            break;
        default:
            ok = false;
        }

        if (!ok) {
            r = Retire{};
            error = "illegal or reserved instruction " + hex32(insn) + " at pc " + hex32(pc);
            return Step::Illegal;
        }
        if (next & 3) throw Fault("misaligned jump target " + hex32(next));
        if (opc == 0x6F || opc == 0x67) write_x(r, rd, pc + 4);   // link only once the jump is known good
        pc = next;
        instret++;
        return Step::Ok;
    } catch (const Fault& f) {
        // Only stores and XST can have side effects before a fault, and they fault before
        // writing, so the architectural state is unchanged here.
        error = std::string(f.what()) + " (pc " + hex32(pc) + ", insn " + hex32(insn) + ")";
        r = Retire{};
        return Step::Fault;
    }
}

std::string trace_line(const Retire& r) {
    char buf[96];
    int n = std::snprintf(buf, sizeof buf, "%08x %08x ", r.pc, r.insn);
    if (r.rd) n += std::snprintf(buf + n, sizeof buf - n, "x%d=%08x ", r.rd, r.rd_val);
    else n += std::snprintf(buf + n, sizeof buf - n, "- ");
    if (r.vd >= 0) {
        n += std::snprintf(buf + n, sizeof buf - n, "v%d=", r.vd);
        for (int i = 15; i >= 0; i--) n += std::snprintf(buf + n, sizeof buf - n, "%02x", r.vd_val.b[i]);
    } else {
        std::snprintf(buf + n, sizeof buf - n, "-");
    }
    return buf;
}
