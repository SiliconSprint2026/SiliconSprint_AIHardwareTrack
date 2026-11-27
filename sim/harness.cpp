// Verilator harness for soc_top.
//
//   Vsoc_top --elf prog.elf [--blob weights.blob] [--trace out.txt] [--wave out.fst|out.vcd]
//            [--max-cycles N] [--profile]
//
// Loads the ELF (each PT_LOAD at p_paddr) and the blob (at 0x1001_0000) straight into the
// TCM arrays, releases reset and clocks until the firmware writes HALT. UART bytes go to
// stdout; everything the harness says goes to stderr, prefixed with "[sim]".
//
// Process exit status: 0 = HALT with code 0, 1 = HALT with a nonzero code, 2 = trap
// (illegal instruction / misaligned access), 3 = cycle limit reached, 4 = access to an
// unmapped address, 5 = bad command line or file.
//
// Commit trace (--trace): one line per retired instruction, same format as xtc-iss:
//   PC INSN RD VD
//   PC, INSN : 8 lowercase hex digits
//   RD       : x<N>=<8 hex> if the instruction wrote x1..x31, else -
//   VD       : v<N>=<32 hex> (byte 15 first) if it wrote a vector register, else -
// RD comes from PicoRV32's RVFI port. VD comes from the coprocessor's trace_vd_* outputs,
// which pulse together with pcpi_ready; the value is attached to the next retirement,
// which is that same instruction because PCPI blocks the core.
#include "Vsoc_top.h"
#include "Vsoc_top___024root.h"
#include "verilated.h"
// The Makefile builds with --trace-fst when zlib is available, else --trace (VCD).
#if VM_TRACE_FST
#include "verilated_fst_c.h"
using WaveFile = VerilatedFstC;
#else
#include "verilated_vcd_c.h"
using WaveFile = VerilatedVcdC;
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "elf_load.h"

static const uint32_t ITCM_BASE = 0x00000000, ITCM_SIZE = 64 * 1024;
static const uint32_t DTCM_BASE = 0x10000000, DTCM_SIZE = 256 * 1024;
static const uint32_t BLOB_ADDR = 0x10010000;

enum { EXIT_HALT_OK = 0, EXIT_HALT_ERR = 1, EXIT_TRAP = 2, EXIT_TIMEOUT = 3, EXIT_BAD_ADDR = 4,
       EXIT_USAGE = 5 };

// Write one byte straight into the ITCM or DTCM array.
static bool poke(Vsoc_top___024root *r, uint32_t addr, uint8_t v) {
  uint32_t shift = (addr & 3) * 8;
  if (addr - ITCM_BASE < ITCM_SIZE) {
    uint32_t &w = r->soc_top__DOT__u_itcm__DOT__mem[(addr - ITCM_BASE) >> 2];
    w = (w & ~(0xFFu << shift)) | (uint32_t)v << shift;
    return true;
  }
  if (addr - DTCM_BASE < DTCM_SIZE) {
    uint32_t off = addr - DTCM_BASE;
    uint32_t &w = r->soc_top__DOT__u_dtcm__DOT__mem[off >> 4][(off >> 2) & 3];
    w = (w & ~(0xFFu << shift)) | (uint32_t)v << shift;
    return true;
  }
  return false;
}

static const char *kernel_name(uint32_t id) {
  static const char *names[] = {"other", "matvec", "prefill", "attention", "softmax", "rmsnorm"};
  return id < 6 ? names[id] : "?";
}

// Why did this instruction trap? PicoRV32 only tells us that it did.
static const char *trap_reason(uint32_t insn) {
  switch (insn & 0x7F) {
    case 0x03: case 0x23: return "misaligned load/store";
    case 0x63: case 0x67: case 0x6F: return "misaligned jump/branch target";
    case 0x0B: case 0x2B: return "Xtc instruction not accepted by the coprocessor (reserved encoding or no coprocessor)";
    case 0x73: return "ECALL/EBREAK or unsupported SYSTEM instruction";
    default: return "illegal instruction";
  }
}

static void usage() {
  fprintf(stderr,
          "usage: Vsoc_top --elf FILE [--blob FILE] [--trace FILE] [--wave FILE]\n"
          "                [--max-cycles N] [--profile]\n");
  exit(EXIT_USAGE);
}

int main(int argc, char **argv) {
  std::string elf, blob, trace_path, wave_path;
  uint64_t max_cycles = 2000000000ull;
  bool profile = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { if (i + 1 >= argc) usage(); return argv[++i]; };
    if (a == "--elf") elf = next();
    else if (a == "--blob") blob = next();
    else if (a == "--trace") trace_path = next();
    else if (a == "--wave") wave_path = next();
    else if (a == "--max-cycles") max_cycles = strtoull(next().c_str(), nullptr, 0);
    else if (a == "--profile") profile = true;
    else if (a.rfind("+verilator", 0) == 0) continue;
    else usage();
  }
  if (elf.empty()) usage();

  setvbuf(stdout, nullptr, _IONBF, 0);   // UART text and [sim] messages stay in order
  auto ctx = std::make_unique<VerilatedContext>();
  ctx->commandArgs(argc, argv);
  auto top = std::make_unique<Vsoc_top>(ctx.get());
  Vsoc_top___024root *root = top->rootp;

  // ---- load memories ----
  uint32_t entry = 0;
  std::string err = load_elf(elf, [&](uint32_t a, uint8_t v) { return poke(root, a, v); }, &entry);
  if (!err.empty()) { fprintf(stderr, "[sim] ERROR: %s\n", err.c_str()); return EXIT_USAGE; }
  if (entry != 0) fprintf(stderr, "[sim] warning: ELF entry is 0x%08x but the core starts at 0\n", entry);
  if (!blob.empty()) {
    std::vector<uint8_t> b;
    if (!read_file(blob, b)) { fprintf(stderr, "[sim] ERROR: cannot read blob %s\n", blob.c_str()); return EXIT_USAGE; }
    if (b.size() > DTCM_BASE + DTCM_SIZE - BLOB_ADDR) {
      fprintf(stderr, "[sim] ERROR: blob is %zu bytes, only %u fit above 0x%08x\n", b.size(),
              DTCM_BASE + DTCM_SIZE - BLOB_ADDR, BLOB_ADDR);
      return EXIT_USAGE;
    }
    for (size_t i = 0; i < b.size(); i++) poke(root, BLOB_ADDR + (uint32_t)i, b[i]);
  }

  FILE *trace = nullptr;
  if (!trace_path.empty()) {
    trace = fopen(trace_path.c_str(), "w");
    if (!trace) { fprintf(stderr, "[sim] ERROR: cannot write %s\n", trace_path.c_str()); return EXIT_USAGE; }
    setvbuf(trace, nullptr, _IOFBF, 1 << 20);
  }
  std::unique_ptr<WaveFile> wave;
  if (!wave_path.empty()) {
    ctx->traceEverOn(true);
    wave = std::make_unique<WaveFile>();
    top->trace(wave.get(), 99);
    wave->open(wave_path.c_str());
  }

  // One clock cycle. Outputs are sampled after the falling-edge eval, i.e. they show the
  // values present during this cycle, just before the rising edge that ends it.
  auto tick = [&]() {
    top->clk = 0; top->eval();
    if (wave) wave->dump(ctx->time());
    ctx->timeInc(1);
  };
  auto rise = [&]() {
    top->clk = 1; top->eval();
    if (wave) wave->dump(ctx->time());
    ctx->timeInc(1);
  };

  top->rst_n = 0;
  for (int i = 0; i < 4; i++) { tick(); rise(); }
  top->rst_n = 1;

  // ---- run ----
  auto t0 = std::chrono::steady_clock::now();
  uint64_t cycle = 0, insns = 0, halt_cycle = 0;
  std::map<uint32_t, uint64_t> prof_cycles;
  uint32_t prof_cur = 0;
  uint64_t prof_run = 0;    // cycles spent in prof_cur since it was set
  bool vd_pending = false;
  uint32_t vd_idx = 0, vd_w[4] = {0, 0, 0, 0};
  uint32_t last_pc = 0;
  int status = -1;         // set when the run ends
  int drain = -1;          // cycles left to let the HALT store retire
  int trap_wait = 4;       // cycles to wait for RVFI to name the trapping instruction
  uint32_t halt_code = 0;
  char line[96];

  while (status < 0) {
    tick();

    // per-kernel cycle accounting: a cycle belongs to the id that was current during it
    if (drain < 0) prof_run++;
    if (top->prof_valid) { prof_cycles[prof_cur] += prof_run; prof_run = 0; prof_cur = top->prof_id; }

    if (top->uart_valid) putchar(top->uart_data);

    if (top->dbg_vd_valid) {
      vd_pending = true;
      vd_idx = top->dbg_vd_idx;
      for (int k = 0; k < 4; k++) vd_w[k] = top->dbg_vd_data[k];
    }

    if (top->dbg_retire) {
      last_pc = top->dbg_pc;
      if (top->dbg_retire_trap) {
        fprintf(stderr, "[sim] TRAP at pc 0x%08x, insn 0x%08x: %s\n", top->dbg_pc, top->dbg_insn,
                trap_reason(top->dbg_insn));
        status = EXIT_TRAP;
        halt_cycle = cycle;
        break;
      }
      insns++;
      if (trace) {
        int n = snprintf(line, sizeof line, "%08x %08x ", top->dbg_pc, top->dbg_insn);
        if (top->dbg_rd) n += snprintf(line + n, sizeof line - n, "x%u=%08x ", top->dbg_rd, top->dbg_rd_data);
        else n += snprintf(line + n, sizeof line - n, "- ");
        if (vd_pending)
          n += snprintf(line + n, sizeof line - n, "v%u=%08x%08x%08x%08x\n", vd_idx, vd_w[3], vd_w[2], vd_w[1], vd_w[0]);
        else
          n += snprintf(line + n, sizeof line - n, "-\n");
        fwrite(line, 1, n, trace);
      }
      vd_pending = false;
      if (drain >= 0) { status = halt_code ? EXIT_HALT_ERR : EXIT_HALT_OK; break; }
    }

    if (top->dbg_bad_access) {
      fprintf(stderr, "[sim] ERROR: access to unmapped address 0x%08x (last retired pc 0x%08x)\n",
              top->dbg_bad_addr, last_pc);
      status = EXIT_BAD_ADDR;
      halt_cycle = cycle;
      break;
    }

    if (top->halt_valid && drain < 0) {
      halt_code = top->halt_code;
      halt_cycle = cycle;
      drain = 32;          // the HALT store itself retires a few cycles later
    }
    if (drain >= 0 && --drain < 0) { status = halt_code ? EXIT_HALT_ERR : EXIT_HALT_OK; break; }

    if (top->trap && status < 0) {
      // PicoRV32 reports the trapping instruction on RVFI one cycle after trap rises.
      if (--trap_wait < 0) {
        fprintf(stderr, "[sim] TRAP (pc unknown, last retired pc 0x%08x)\n", last_pc);
        status = EXIT_TRAP;
        halt_cycle = cycle;
        break;
      }
    }

    if (cycle >= max_cycles) {
      fprintf(stderr, "[sim] TIMEOUT: no HALT after %llu cycles (last retired pc 0x%08x)\n",
              (unsigned long long)cycle, last_pc);
      status = EXIT_TIMEOUT;
      halt_cycle = cycle;
      break;
    }

    rise();
    cycle++;
  }

  double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  fflush(stdout);
  if (trace) fclose(trace);
  if (wave) wave->close();
  top->final();

  if (status == EXIT_HALT_OK || status == EXIT_HALT_ERR)
    fprintf(stderr, "\n[sim] HALT, exit code %u (0x%x)\n", halt_code, halt_code);
  fprintf(stderr, "[sim] cycles %llu, instructions %llu, CPI %.2f\n", (unsigned long long)halt_cycle,
          (unsigned long long)insns, insns ? (double)halt_cycle / insns : 0.0);
  fprintf(stderr, "[sim] wall time %.2f s (%.2f M cycles/s)\n", wall, wall > 0 ? cycle / wall / 1e6 : 0.0);
  if (trace) fprintf(stderr, "[sim] commit trace: %s\n", trace_path.c_str());
  if (wave) fprintf(stderr, "[sim] waveform: %s\n", wave_path.c_str());

  prof_cycles[prof_cur] += prof_run;
  if (profile) {
    uint64_t total = 0;
    for (auto &p : prof_cycles) total += p.second;
    fprintf(stderr, "[sim] cycles per PROFILE id:\n");
    fprintf(stderr, "[sim]   %4s  %-10s %14s %7s\n", "id", "kernel", "cycles", "share");
    for (auto &p : prof_cycles)
      fprintf(stderr, "[sim]   %4u  %-10s %14llu %6.2f%%\n", p.first, kernel_name(p.first),
              (unsigned long long)p.second, total ? 100.0 * p.second / total : 0.0);
    fprintf(stderr, "[sim]   %4s  %-10s %14llu\n", "", "total", (unsigned long long)total);
  }
  return status;
}
