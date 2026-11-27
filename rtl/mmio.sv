// Memory-mapped I/O at 0x2000_0000 (only addr[3:2] is decoded here; soc_top does the rest).
//
//   +0x0  UART TX   write: send byte wdata[7:0]
//   +0x4  HALT      write: stop the simulation, exit code = wdata
//   +0x8  CYCLES    read : cycles since reset (32-bit, wraps)
//   +0xC  PROFILE   write: current kernel id (0 = none); the harness sums cycles per id
//
// Same timing as the TCMs: request in cycle N, rdata in cycle N+1. Every write produces a
// one-cycle pulse on the matching *_valid output in cycle N+1, which the harness watches.
module mmio (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        en,
  input  logic [3:0]  wstrb,                 // 0 = read
  input  logic [3:0]  addr,
  input  logic [31:0] wdata,
  output logic [31:0] rdata,

  output logic        uart_valid,
  output logic [7:0]  uart_data,
  output logic        halt_valid,
  output logic [31:0] halt_code,
  output logic        prof_valid,
  output logic [31:0] prof_id
);
  logic [31:0] cycles;
  wire         wr = en && (wstrb != 4'b0);

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      cycles     <= 0;
      uart_valid <= 0;
      halt_valid <= 0;
      prof_valid <= 0;
    end else begin
      cycles     <= cycles + 1;
      uart_valid <= wr && addr[3:2] == 2'd0;
      halt_valid <= wr && addr[3:2] == 2'd1;
      prof_valid <= wr && addr[3:2] == 2'd3;
    end
    if (wr) begin
      uart_data <= wdata[7:0];
      halt_code <= wdata;
      prof_id   <= wdata;
    end
    if (en) rdata <= (addr[3:2] == 2'd2) ? cycles : 32'h0;
  end
endmodule
