// TENSOR-CORE SoC: PicoRV32 (RV32IM) + Xtc coprocessor + ITCM + dual-port DTCM + MMIO.
//
//   0x0000_0000  ITCM   64 KB   code and read-only data
//   0x1000_0000  DTCM  256 KB   data; port A = core (32-bit), port B = coprocessor (128-bit)
//   0x2000_0000  MMIO   16 B    UART, HALT, CYCLES, PROFILE (see mmio.sv)
//
// The core has one native memory bus. Every access takes 2 cycles: the request goes to
// the selected memory in the cycle mem_valid rises, and mem_ready comes back with the
// data in the next cycle. An access outside the three regions completes with rdata = 0
// and raises dbg_bad_access, which the harness reports as an error.
//
// The dbg_* outputs feed the simulation harness. They are only driven when the design
// is built with +define+RISCV_FORMAL (which also turns on PicoRV32's RVFI port);
// otherwise they are tied to 0 and synthesis removes the logic behind them.
module soc_top (
  input  logic         clk,
  input  logic         rst_n,
  output logic         trap,              // core stopped on an illegal instruction or misaligned access

  // MMIO events (one-cycle pulses)
  output logic         uart_valid,
  output logic [7:0]   uart_data,
  output logic         halt_valid,
  output logic [31:0]  halt_code,
  output logic         prof_valid,
  output logic [31:0]  prof_id,

  // simulation-only commit trace
  output logic         dbg_retire,        // one instruction retired this cycle
  output logic         dbg_retire_trap,   // ... and it trapped instead of completing
  output logic [31:0]  dbg_pc,
  output logic [31:0]  dbg_insn,
  output logic [4:0]   dbg_rd,            // 0 if no integer register was written
  output logic [31:0]  dbg_rd_data,
  output logic         dbg_vd_valid,      // straight from xtc_coproc (see its header)
  output logic [3:0]   dbg_vd_idx,
  output logic [127:0] dbg_vd_data,
  output logic         dbg_bad_access,    // one-cycle pulse: access to an unmapped address
  output logic [31:0]  dbg_bad_addr
);
  // ---------------- core ----------------
  logic        mem_valid, mem_ready;
  /* verilator lint_off UNUSEDSIGNAL */
  logic        mem_instr;                                 // fetch vs data: not needed here
  /* verilator lint_on UNUSEDSIGNAL */
  logic [31:0] mem_addr, mem_wdata, mem_rdata;
  logic [3:0]  mem_wstrb;

  logic        pcpi_valid, pcpi_wr, pcpi_wait, pcpi_ready;
  logic [31:0] pcpi_insn, pcpi_rs1, pcpi_rs2, pcpi_rd;

`ifdef RISCV_FORMAL
  logic        rvfi_valid, rvfi_trap;
  logic [31:0] rvfi_insn, rvfi_pc_rdata, rvfi_rd_wdata;
  logic [4:0]  rvfi_rd_addr;
`endif

  // IRQs stay off: with ENABLE_IRQ PicoRV32 decodes its own getq/setq/maskirq/timer
  // instructions in opcode 0x0B, which belongs to Xtc (CUSTOM-0).
  // Counters stay off: rdcycle/rdinstret are not part of the SoC ISA and must trap,
  // exactly as on the ISS. Use the MMIO cycle counter instead.
  picorv32 #(
    .ENABLE_MUL     (1),
    .ENABLE_DIV     (1),
    .ENABLE_PCPI    (1),
    .ENABLE_IRQ     (0),
    .ENABLE_IRQ_QREGS (0),
    .ENABLE_COUNTERS   (0),
    .ENABLE_COUNTERS64 (0),
    .PROGADDR_RESET (32'h0000_0000)
  ) u_cpu (
    .clk        (clk),
    .resetn     (rst_n),
    .trap       (trap),
    .mem_valid  (mem_valid),
    .mem_instr  (mem_instr),
    .mem_ready  (mem_ready),
    .mem_addr   (mem_addr),
    .mem_wdata  (mem_wdata),
    .mem_wstrb  (mem_wstrb),
    .mem_rdata  (mem_rdata),
    .mem_la_read  (),
    .mem_la_write (),
    .mem_la_addr  (),
    .mem_la_wdata (),
    .mem_la_wstrb (),
    .pcpi_valid (pcpi_valid),
    .pcpi_insn  (pcpi_insn),
    .pcpi_rs1   (pcpi_rs1),
    .pcpi_rs2   (pcpi_rs2),
    .pcpi_wr    (pcpi_wr),
    .pcpi_rd    (pcpi_rd),
    .pcpi_wait  (pcpi_wait),
    .pcpi_ready (pcpi_ready),
    .irq        (32'h0),
    .eoi        (),
`ifdef RISCV_FORMAL
    .rvfi_valid (rvfi_valid),
    .rvfi_order (),
    .rvfi_insn  (rvfi_insn),
    .rvfi_trap  (rvfi_trap),
    .rvfi_halt  (),
    .rvfi_intr  (),
    .rvfi_mode  (),
    .rvfi_ixl   (),
    .rvfi_rs1_addr  (),
    .rvfi_rs2_addr  (),
    .rvfi_rs1_rdata (),
    .rvfi_rs2_rdata (),
    .rvfi_rd_addr   (rvfi_rd_addr),
    .rvfi_rd_wdata  (rvfi_rd_wdata),
    .rvfi_pc_rdata  (rvfi_pc_rdata),
    .rvfi_pc_wdata  (),
    .rvfi_mem_addr  (),
    .rvfi_mem_rmask (),
    .rvfi_mem_wmask (),
    .rvfi_mem_rdata (),
    .rvfi_mem_wdata (),
    .rvfi_csr_mcycle_rmask   (),
    .rvfi_csr_mcycle_wmask   (),
    .rvfi_csr_mcycle_rdata   (),
    .rvfi_csr_mcycle_wdata   (),
    .rvfi_csr_minstret_rmask (),
    .rvfi_csr_minstret_wmask (),
    .rvfi_csr_minstret_rdata (),
    .rvfi_csr_minstret_wdata (),
`endif
    .trace_valid (),
    .trace_data  ()
  );

  // ---------------- address decode ----------------
  wire sel_itcm = mem_addr[31:16] == 16'h0000;
  wire sel_dtcm = mem_addr[31:18] == 14'h0400;          // 0x1000_0000 .. 0x1003_FFFF
  wire sel_mmio = mem_addr[31:4]  == 28'h200_0000;      // 0x2000_0000 .. 0x2000_000F

  // A request is issued in the first cycle of mem_valid; mem_ready follows one cycle later.
  logic ready_q;
  wire  req = mem_valid && !ready_q;

  logic [1:0] sel_q;                                     // which region answered: 0 itcm, 1 dtcm, 2 mmio, 3 none
  always_ff @(posedge clk) begin
    if (!rst_n) ready_q <= 1'b0;
    else        ready_q <= req;
    if (req) sel_q <= sel_itcm ? 2'd0 : sel_dtcm ? 2'd1 : sel_mmio ? 2'd2 : 2'd3;
  end
  assign mem_ready = ready_q;

  logic [31:0] itcm_rdata, dtcm_rdata, mmio_rdata;
  always_comb begin
    case (sel_q)
      2'd0:    mem_rdata = itcm_rdata;
      2'd1:    mem_rdata = dtcm_rdata;
      2'd2:    mem_rdata = mmio_rdata;
      default: mem_rdata = 32'h0;
    endcase
  end

  // ---------------- memories ----------------
  logic         dtcm_b_req, dtcm_b_we;
  /* verilator lint_off UNUSEDSIGNAL */
  logic [31:0]  dtcm_b_addr;                             // only bits [17:4] reach the DTCM
  /* verilator lint_on UNUSEDSIGNAL */
  logic [127:0] dtcm_b_wdata, dtcm_b_rdata;
  logic [15:0]  dtcm_b_wmask;

  itcm u_itcm (
    .clk   (clk),
    .en    (req && sel_itcm),
    .wstrb (mem_wstrb),
    .addr  (mem_addr[15:0]),
    .wdata (mem_wdata),
    .rdata (itcm_rdata)
  );

  dtcm u_dtcm (
    .clk     (clk),
    .a_en    (req && sel_dtcm),
    .a_wstrb (mem_wstrb),
    .a_addr  (mem_addr[17:0]),
    .a_wdata (mem_wdata),
    .a_rdata (dtcm_rdata),
    .b_en    (dtcm_b_req),
    .b_we    (dtcm_b_we),
    .b_wmask (dtcm_b_wmask),
    .b_addr  (dtcm_b_addr[17:0]),
    .b_wdata (dtcm_b_wdata),
    .b_rdata (dtcm_b_rdata)
  );

  mmio u_mmio (
    .clk        (clk),
    .rst_n      (rst_n),
    .en         (req && sel_mmio),
    .wstrb      (mem_wstrb),
    .addr       (mem_addr[3:0]),
    .wdata      (mem_wdata),
    .rdata      (mmio_rdata),
    .uart_valid (uart_valid),
    .uart_data  (uart_data),
    .halt_valid (halt_valid),
    .halt_code  (halt_code),
    .prof_valid (prof_valid),
    .prof_id    (prof_id)
  );

  // ---------------- coprocessor ----------------
  /* verilator lint_off UNUSEDSIGNAL */                    // unused unless RISCV_FORMAL
  logic         trace_vd_valid;
  logic [3:0]   trace_vd_idx;
  logic [127:0] trace_vd_data;
  /* verilator lint_on UNUSEDSIGNAL */

  xtc_coproc u_xtc (
    .clk            (clk),
    .rst_n          (rst_n),
    .pcpi_valid     (pcpi_valid),
    .pcpi_insn      (pcpi_insn),
    .pcpi_rs1       (pcpi_rs1),
    .pcpi_rs2       (pcpi_rs2),
    .pcpi_wr        (pcpi_wr),
    .pcpi_rd        (pcpi_rd),
    .pcpi_wait      (pcpi_wait),
    .pcpi_ready     (pcpi_ready),
    .dtcm_b_req     (dtcm_b_req),
    .dtcm_b_we      (dtcm_b_we),
    .dtcm_b_addr    (dtcm_b_addr),
    .dtcm_b_wdata   (dtcm_b_wdata),
    .dtcm_b_wmask   (dtcm_b_wmask),
    .dtcm_b_rdata   (dtcm_b_rdata),
    .trace_vd_valid (trace_vd_valid),
    .trace_vd_idx   (trace_vd_idx),
    .trace_vd_data  (trace_vd_data)
  );

  // ---------------- simulation trace outputs ----------------
`ifdef RISCV_FORMAL
  logic        bad_q;
  logic [31:0] bad_addr_q;
  always_ff @(posedge clk) begin
    bad_q <= rst_n && req && !(sel_itcm || sel_dtcm || sel_mmio);
    if (req) bad_addr_q <= mem_addr;
  end
  assign dbg_retire      = rvfi_valid;
  assign dbg_retire_trap = rvfi_trap;
  assign dbg_pc          = rvfi_pc_rdata;
  assign dbg_insn        = rvfi_insn;
  assign dbg_rd          = rvfi_rd_addr;
  assign dbg_rd_data     = rvfi_rd_wdata;
  assign dbg_vd_valid    = trace_vd_valid;
  assign dbg_vd_idx      = trace_vd_idx;
  assign dbg_vd_data     = trace_vd_data;
  assign dbg_bad_access  = bad_q;
  assign dbg_bad_addr    = bad_addr_q;
`else
  assign dbg_retire      = 1'b0;
  assign dbg_retire_trap = 1'b0;
  assign dbg_pc          = 32'h0;
  assign dbg_insn        = 32'h0;
  assign dbg_rd          = 5'h0;
  assign dbg_rd_data     = 32'h0;
  assign dbg_vd_valid    = 1'b0;
  assign dbg_vd_idx      = 4'h0;
  assign dbg_vd_data     = 128'h0;
  assign dbg_bad_access  = 1'b0;
  assign dbg_bad_addr    = 32'h0;
`endif
endmodule
