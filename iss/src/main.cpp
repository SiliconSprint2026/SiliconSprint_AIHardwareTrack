// xtc-iss: instruction-set simulator for the TENSOR-CORE SoC (RV32IM + Xtc).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "cpu.h"
#include "elf.h"

namespace {

const char* USAGE =
    "usage: xtc-iss --elf FILE [options]\n"
    "  --elf FILE        program to run (PT_LOAD segments go to their p_paddr; pc = e_entry)\n"
    "  --blob FILE       load FILE into DTCM at 0x10010000\n"
    "  --trace FILE      write one line per retired instruction (docs/ISA_SPEC.md)\n"
    "  --max-insns N     stop with an error after N instructions (default 1000000000)\n"
    "  --profile         print instructions per PROFILE id to stderr at exit\n"
    "  --riscv-tests     run a riscv-tests 'p' binary: 1 MB RAM at 0x80000000, Zicsr,\n"
    "                    ECALL/MRET, pass/fail via `tohost`\n"
    "  --xtc-vectors     test mode: read one instruction test per stdin line (iss/tests)\n"
    "exit status: the HALT code (low 8 bits); 124 = --max-insns reached;\n"
    "             125 = illegal/reserved instruction or bad access; 126 = usage or load error\n";

[[noreturn]] void die(int code, const std::string& msg) {
    std::fflush(stdout);
    std::fprintf(stderr, "xtc-iss: %s\n", msg.c_str());
    std::exit(code);
}

bool parse_hex_vreg(const std::string& s, VReg& v) {   // 32 hex digits, byte 15 first
    if (s.size() != 32) return false;
    for (int i = 0; i < 16; i++) v.b[15 - i] = uint8_t(std::stoul(s.substr(2 * i, 2), nullptr, 16));
    return true;
}

// --xtc-vectors. Each stdin line is one test:
//   INSN RS1VAL RS2VAL ADDR MEM16 V0 .. V15          (all hex; MEM16 and Vn as 32 digits, byte 15 first)
// x[rs1 field] = RS1VAL, then x[rs2 field] = RS2VAL (x0 stays 0), v0..v15 are set, the 16 DTCM
// bytes at ADDR are set to MEM16, then INSN is executed at pc 0 through the normal step().
// Output line: TRAP | FAULT | "<trace line> <16 DTCM bytes at ADDR after>".
int run_vectors() {
    Mem mem;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string t[21];
        for (auto& s : t) in >> s;
        if (t[20].empty()) die(126, "bad vector line: " + line);
        Cpu cpu(mem);
        uint32_t insn = std::stoul(t[0], nullptr, 16), addr = std::stoul(t[3], nullptr, 16);
        uint32_t rs1 = (insn >> 15) & 31, rs2 = (insn >> 20) & 31;
        cpu.x[rs1] = std::stoul(t[1], nullptr, 16);
        cpu.x[rs2] = std::stoul(t[2], nullptr, 16);
        cpu.x[0] = 0;
        VReg m;
        bool ok = parse_hex_vreg(t[4], m);
        for (int i = 0; i < 16; i++) ok = ok && parse_hex_vreg(t[5 + i], cpu.v[i]);
        uint8_t* p = mem.ram(addr, 16);
        if (!ok || !p || addr < DTCM_BASE) die(126, "bad vector line: " + line);
        std::memcpy(p, m.b, 16);
        mem.store(0, 4, insn);
        cpu.pc = 0;

        Retire r;
        Step s = cpu.step(r);
        if (s == Step::Illegal) { std::puts("TRAP"); continue; }
        if (s == Step::Fault) { std::puts("FAULT"); continue; }
        std::string out = trace_line(r) + " ";
        char hx[3];
        for (int i = 15; i >= 0; i--) { std::snprintf(hx, sizeof hx, "%02x", p[i]); out += hx; }
        std::puts(out.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string elf, blob, trace;
    uint64_t max_insns = 1000000000;
    bool profile = false, riscv_tests = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto arg = [&]() -> std::string {
            if (i + 1 >= argc) die(126, a + " needs an argument\n" + USAGE);
            return argv[++i];
        };
        if (a == "--elf") elf = arg();
        else if (a == "--blob") blob = arg();
        else if (a == "--trace") trace = arg();
        else if (a == "--max-insns") max_insns = std::strtoull(arg().c_str(), nullptr, 0);
        else if (a == "--profile") profile = true;
        else if (a == "--riscv-tests") riscv_tests = true;
        else if (a == "--xtc-vectors") return run_vectors();
        else if (a == "-h" || a == "--help") { std::fputs(USAGE, stdout); return 0; }
        else die(126, "unknown option " + a + "\n" + USAGE);
    }
    if (elf.empty()) die(126, std::string("no --elf given\n") + USAGE);

    Mem mem;
    Cpu cpu(mem);
    cpu.test_mode = riscv_tests;
    if (riscv_tests) mem.test_ram.resize(TEST_SIZE);
    try {
        ElfInfo info = load_elf(mem, elf);
        cpu.pc = info.entry;
        if (riscv_tests) {
            if (!info.symbols.count("tohost")) die(126, elf + ": no tohost symbol");
            mem.tohost = info.symbols["tohost"];
        }
        if (!blob.empty()) {
            std::vector<uint8_t> b = read_file(blob);
            uint8_t* dst = mem.ram(BLOB_ADDR, uint32_t(b.size()));
            if (!dst || BLOB_ADDR + b.size() > DTCM_BASE + DTCM_SIZE) die(126, blob + ": does not fit in DTCM");
            std::memcpy(dst, b.data(), b.size());
        }
    } catch (const Fault& f) {
        die(126, f.what());
    }

    std::unique_ptr<FILE, int (*)(FILE*)> tf(nullptr, std::fclose);
    if (!trace.empty()) {
        tf.reset(std::fopen(trace.c_str(), "w"));
        if (!tf) die(126, "cannot write " + trace);
        static char buf[1 << 20];
        std::setvbuf(tf.get(), buf, _IOFBF, sizeof buf);
    }

    auto print_profile = [&]() {
        if (!profile) return;
        for (auto& [id, n] : mem.profile)
            std::fprintf(stderr, "profile id %u: %llu instructions\n", id, (unsigned long long)n);
    };

    Retire r;
    while (true) {
        if (cpu.instret >= max_insns) {
            if (tf) std::fflush(tf.get());
            die(124, "stopped after --max-insns " + std::to_string(max_insns) + " at pc " + hex32(cpu.pc));
        }
        uint32_t prof = mem.profile_id;
        if (cpu.step(r) != Step::Ok) {
            if (tf) std::fflush(tf.get());
            die(125, cpu.error);
        }
        mem.profile[prof]++;
        if (tf) std::fprintf(tf.get(), "%s\n", trace_line(r).c_str());
        if (mem.halted) {
            std::fflush(stdout);
            std::fprintf(stderr, "xtc-iss: HALT %u after %llu instructions\n", mem.halt_code,
                         (unsigned long long)cpu.instret);
            print_profile();
            return int(mem.halt_code & 0xFF);
        }
        if (mem.tohost_written) {
            uint32_t v = mem.tohost_value;
            if (v == 1) return 0;
            std::fprintf(stderr, "xtc-iss: riscv-test FAILED, test %u (tohost %s)\n", v >> 1, hex32(v).c_str());
            return 1;
        }
    }
}
