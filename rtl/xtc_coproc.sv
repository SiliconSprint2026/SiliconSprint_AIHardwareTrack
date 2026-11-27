// Xtc coprocessor: EMPTY STUB. It accepts no instruction, so every Xtc instruction
// (CUSTOM-0 0x0B, CUSTOM-1 0x2B) ends in PicoRV32's illegal-instruction trap.
// Your job is to fill this in. The port list below is fixed: soc_top and the
// simulation harness depend on it.
//
// ---------------------------------------------------------------------------
// PCPI handshake (PicoRV32 "Pico Co-Processor Interface")
// ---------------------------------------------------------------------------
// When the core decodes an instruction it does not implement itself, it raises
// pcpi_valid and holds pcpi_insn, pcpi_rs1, pcpi_rs2 (the values of x[rs1], x[rs2])
// stable until the instruction is finished. The core waits; nothing else retires.
//
//   * pcpi_valid is also raised for RV32M instructions, which PicoRV32 handles with
//     its own internal units. Decode the opcode and ignore anything that is not yours.
//   * To finish, raise pcpi_ready for exactly one cycle. In that same cycle set
//     pcpi_wr = 1 and pcpi_rd = result if the instruction writes rd, else pcpi_wr = 0.
//     pcpi_valid drops at the next clock edge.
//   * TIMEOUT: if neither pcpi_wait nor pcpi_ready is high within 16 cycles of
//     pcpi_valid rising, the core declares the instruction illegal and traps. An
//     instruction that takes longer than that must raise pcpi_wait (it may do so in
//     the first cycle) and keep it high until the cycle of pcpi_ready.
//   * Reserved encodings: simply never answer them; the timeout turns them into the
//     required illegal-instruction trap.
//
// ---------------------------------------------------------------------------
// DTCM port B (128-bit, 1-cycle read latency)
// ---------------------------------------------------------------------------
// Drive dtcm_b_req in cycle N with dtcm_b_addr (byte address; only bits [17:4] are used,
// so the access is always to the 16-byte row containing it). For a write also drive
// dtcm_b_we = 1, dtcm_b_wdata and dtcm_b_wmask (bit i enables byte i). For a read,
// dtcm_b_rdata holds the row in cycle N+1. The core uses port A at the same time; the
// two ports never stall each other.
//
// ---------------------------------------------------------------------------
// Trace interface (simulation only, removed by synthesis because nothing uses it)
// ---------------------------------------------------------------------------
// The harness prints one commit-trace line per retired instruction and compares it
// with the ISS. For an instruction that writes a vector register, raise
// trace_vd_valid for one cycle, together with pcpi_ready of that instruction, with
// trace_vd_idx = the register number and trace_vd_data = its new 128-bit value
// (byte 0 in bits [7:0]). Report the architectural value even if your design writes
// the register file later. Keep trace_vd_valid low at all other times.
module xtc_coproc (
  input  logic         clk,
  input  logic         rst_n,

  // PCPI
  input  logic         pcpi_valid,
  input  logic [31:0]  pcpi_insn,
  input  logic [31:0]  pcpi_rs1,
  input  logic [31:0]  pcpi_rs2,
  output logic         pcpi_wr,
  output logic [31:0]  pcpi_rd,
  output logic         pcpi_wait,
  output logic         pcpi_ready,

  // DTCM port B
  output logic         dtcm_b_req,
  output logic         dtcm_b_we,
  output logic [31:0]  dtcm_b_addr,
  output logic [127:0] dtcm_b_wdata,
  output logic [15:0]  dtcm_b_wmask,
  input  logic [127:0] dtcm_b_rdata,

  // trace (simulation only)
  output logic         trace_vd_valid,
  output logic [3:0]   trace_vd_idx,
  output logic [127:0] trace_vd_data
);
  // The stub uses none of its inputs.
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused = &{1'b0, clk, rst_n, pcpi_valid, pcpi_insn, pcpi_rs1, pcpi_rs2, dtcm_b_rdata};
  /* verilator lint_on UNUSEDSIGNAL */

  assign pcpi_wr        = 1'b0;
  assign pcpi_rd        = 32'h0;
  assign pcpi_wait      = 1'b0;
  assign pcpi_ready     = 1'b0;

  assign dtcm_b_req     = 1'b0;
  assign dtcm_b_we      = 1'b0;
  assign dtcm_b_addr    = 32'h0;
  assign dtcm_b_wdata   = 128'h0;
  assign dtcm_b_wmask   = 16'h0;

  assign trace_vd_valid = 1'b0;
  assign trace_vd_idx   = 4'h0;
  assign trace_vd_data  = 128'h0;
endmodule
