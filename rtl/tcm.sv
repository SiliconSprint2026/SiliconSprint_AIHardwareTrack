// Behavioral tightly-coupled memories. Both are blackboxed in synthesis (synth/synth.ys),
// so they cost no area; in a real chip they would be SRAM macros.
//
// Timing for every port: drive en (and we, wstrb/wmask, wdata) in cycle N, read data
// appears on rdata in cycle N+1. A read in the same cycle as a write to the same
// address returns the OLD data (read-first).
//
// The harness writes the ELF and the weight blob straight into the `mem` arrays
// before reset, which is why they are marked public.

// ITCM: 64 KB, one 32-bit port with byte strobes. Holds code and read-only data.
module itcm #(
  parameter int WORDS = 16384               // 64 KB / 4
) (
  input  logic        clk,
  input  logic        en,
  input  logic [3:0]  wstrb,                 // 0 = read
  input  logic [15:0] addr,                  // byte address inside the ITCM
  input  logic [31:0] wdata,
  output logic [31:0] rdata
);
  logic [31:0] mem [0:WORDS-1] /*verilator public_flat_rw*/;
  wire  [13:0] idx = addr[15:2];

  always_ff @(posedge clk) begin
    if (en) begin
      rdata <= mem[idx];
      for (int i = 0; i < 4; i++)
        if (wstrb[i]) mem[idx][8*i +: 8] <= wdata[8*i +: 8];
    end
  end
endmodule

// DTCM: 256 KB, true dual-port, stored as 16384 rows of 128 bits.
//   Port A (core):        32-bit, byte strobes, addr[17:2] picks the word.
//   Port B (coprocessor): 128-bit, 16 byte-enables, addr[17:4] picks the row;
//                         addr[3:0] is ignored, so every access is 16-byte aligned.
// If both ports write the same byte in the same cycle, port B wins.
module dtcm #(
  parameter int ROWS = 16384                // 256 KB / 16
) (
  input  logic         clk,
  // port A
  input  logic         a_en,
  input  logic [3:0]   a_wstrb,              // 0 = read
  input  logic [17:0]  a_addr,
  input  logic [31:0]  a_wdata,
  output logic [31:0]  a_rdata,
  // port B
  input  logic         b_en,
  input  logic         b_we,
  input  logic [15:0]  b_wmask,              // byte i of the row is written if b_we && b_wmask[i]
  input  logic [17:0]  b_addr,
  input  logic [127:0] b_wdata,
  output logic [127:0] b_rdata
);
  logic [127:0] mem [0:ROWS-1] /*verilator public_flat_rw*/;
  wire  [13:0]  a_row  = a_addr[17:4];
  wire  [1:0]   a_lane = a_addr[3:2];
  wire  [13:0]  b_row  = b_addr[17:4];

  always_ff @(posedge clk) begin
    if (a_en) begin
      a_rdata <= mem[a_row][32*a_lane +: 32];
      for (int i = 0; i < 4; i++)
        if (a_wstrb[i]) mem[a_row][32*a_lane + 8*i +: 8] <= a_wdata[8*i +: 8];
    end
    if (b_en) begin
      b_rdata <= mem[b_row];
      for (int i = 0; i < 16; i++)
        if (b_we && b_wmask[i]) mem[b_row][8*i +: 8] <= b_wdata[8*i +: 8];
    end
  end
endmodule
