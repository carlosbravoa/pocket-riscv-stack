// Behavioral stand-ins for the Quartus IP used by core_top — SIMULATION ONLY.
// Compiled by soc/sim/run_sim.sh instead of the vendor files; never seen by
// Quartus. Functional models: no metastability, no timing.
//
// SPDX-License-Identifier: BSD-2-Clause

// ---------------------------------------------------------------------------
// mf_pllbase: 74.25 MHz -> "12.288 MHz". Sim divides by 6 (12.375 equivalent);
// exact audio/video rates are irrelevant to the flows under test.
// ---------------------------------------------------------------------------
module mf_pllbase (
    input  wire refclk,
    input  wire rst,
    output reg  outclk_0,
    output wire outclk_1,
    output reg  locked
);
    reg [1:0] div = 0;
    reg [4:0] lockcnt = 0;
    initial begin outclk_0 = 0; locked = 0; end
    always @(posedge refclk) begin
        if (div == 2) begin
            div      <= 0;
            outclk_0 <= ~outclk_0;
        end else
            div <= div + 1'b1;
        if (~&lockcnt) lockcnt <= lockcnt + 1'b1;
        locked <= &lockcnt;
    end
    assign outclk_1 = 1'b0;   // 90-degree video clock: unused in sim
endmodule

// ---------------------------------------------------------------------------
// dcfifo: dual-clock FIFO, the subset data_loader/data_unloader use
// (showahead OFF, 4 deep). Functional: binary pointers, cross-domain reads
// are exact (fine for a 2-state, zero-delay simulator).
// ---------------------------------------------------------------------------
module dcfifo #(
    parameter lpm_width  = 8,
    parameter lpm_widthu = 2,
    parameter lpm_numwords = 4,
    parameter lpm_showahead = "OFF",
    parameter lpm_type = "dcfifo",
    parameter clocks_are_synchronized = "FALSE",
    parameter intended_device_family = "",
    parameter overflow_checking  = "ON",
    parameter underflow_checking = "ON",
    parameter use_eab = "ON",
    parameter rdsync_delaypipe = 0,
    parameter wrsync_delaypipe = 0
) (
    input  wire [lpm_width-1:0] data,
    input  wire rdclk,
    input  wire rdreq,
    input  wire wrclk,
    input  wire wrreq,
    output reg  [lpm_width-1:0] q,
    output wire rdempty,
    output wire wrfull,
    output wire wrempty,
    input  wire aclr,
    output wire [lpm_widthu-1:0] rdusedw,
    output wire [lpm_widthu-1:0] wrusedw,
    output wire rdfull,
    output wire eccstatus
);
    reg [lpm_width-1:0] mem [0:lpm_numwords-1];
    reg [lpm_widthu:0]  wp = 0, rp = 0;    // one extra bit for full/empty

    always @(posedge wrclk)
        if (wrreq) begin
            mem[wp[lpm_widthu-1:0]] <= data;
            wp <= wp + 1'b1;
        end
    always @(posedge rdclk)
        if (rdreq) begin
            q  <= mem[rp[lpm_widthu-1:0]];
            rp <= rp + 1'b1;
        end

    assign rdempty = (wp == rp);
    assign wrfull  = (wp[lpm_widthu-1:0] == rp[lpm_widthu-1:0]) && (wp != rp);
    assign wrempty = rdempty;
    assign rdfull  = wrfull;
    assign rdusedw = wp - rp;
    assign wrusedw = wp - rp;
    assign eccstatus = 0;
endmodule

// ---------------------------------------------------------------------------
// mf_datatable: 256 x 32 true-dual-port RAM with registered outputs (matches
// the altsyncram config: q registered, 2-cycle read visibility).
// ---------------------------------------------------------------------------
module mf_datatable (
    input  wire [7:0]  address_a,
    input  wire [7:0]  address_b,
    input  wire        clock_a,
    input  wire        clock_b,
    input  wire [31:0] data_a,
    input  wire [31:0] data_b,
    input  wire        wren_a,
    input  wire        wren_b,
    output reg  [31:0] q_a,
    output reg  [31:0] q_b
);
    reg [31:0] mem [0:255];
    integer i;
    initial for (i = 0; i < 256; i = i + 1) mem[i] = 0;
    always @(posedge clock_a) begin
        if (wren_a) mem[address_a] <= data_a;
        q_a <= mem[address_a];
    end
    always @(posedge clock_b) begin
        if (wren_b) mem[address_b] <= data_b;
        q_b <= mem[address_b];
    end
endmodule

// ---------------------------------------------------------------------------
// DFF: Altera primitive used by LiteX's AsyncResetSynchronizer on the Altera
// platform (async clear/preset, active low).
// ---------------------------------------------------------------------------
module DFF (
    input  wire clk,
    input  wire d,
    input  wire clrn,
    input  wire prn,
    output reg  q
);
    always @(posedge clk or negedge clrn or negedge prn)
        if      (!clrn) q <= 1'b0;
        else if (!prn)  q <= 1'b1;
        else            q <= d;
endmodule
