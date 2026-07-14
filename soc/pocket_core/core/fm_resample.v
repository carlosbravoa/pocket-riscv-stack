// fm_resample.v — polyphase FIR resampler for the FM audio path.
//
// Converts the OPL sample stream (12.288 MHz / 248 = 49548.4 Hz) to the audio
// drain rate (12.288 MHz / 256 = 48000 Hz) — an EXACT 31/32 ratio — replacing the
// zero-order-hold latch in core_top. 31/32 polyphase, 16 taps/phase, int16 Q15
// coefficients (fir_coeffs.hex, phase-major). Steady-state alias floor ~ -97 dBc
// (vs the ZOH's -12.8) — ~85 dB cleaner. See soc/fm_resample/ for the DSP
// prototype + the bit-exact reference this matches (diff == 0).
//
// Schedule (input-driven, proven == scipy reference): shift the delay line on each
// OPL sample; emit an output using coefficient bank[phase] unless this input is the
// 1-in-32 "skip"; after a phase-30 emit, skip the next input and wrap phase to 0.
//
// Timing: OPL samples are 248 clk apart; the 32-cycle stereo MAC (16 taps x 2 ch,
// one multiplier time-multiplexed) finishes long before the next sample, so the
// delay line is stable throughout.
//
// SPDX-License-Identifier: BSD-2-Clause
`default_nettype none

module fm_resample #(
    parameter TAPS = 16,
    parameter L    = 31,       // phases (upsample factor)
    parameter M    = 32        // downsample factor
)(
    input  wire               clk,        // 12.288 MHz
    input  wire               rst,
    input  wire               in_valid,   // OPL sample strobe (~49548 Hz)
    input  wire signed [15:0] in_l,
    input  wire signed [15:0] in_r,
    output reg                out_valid,   // ~48000 Hz
    output reg  signed [15:0] out_l,
    output reg  signed [15:0] out_r
);
    // coefficient ROM: L*TAPS int16, phase-major (phase*TAPS + tap)
    reg signed [15:0] coeff [0:L*TAPS-1];
    initial $readmemh("fir_coeffs.hex", coeff);

    // delay lines (newest at index 0)
    reg signed [15:0] ll [0:TAPS-1];
    reg signed [15:0] rl [0:TAPS-1];

    // --- schedule: shift on each OPL sample, decide emit vs skip -----------------
    reg [4:0] phase;
    reg       skip;
    reg       emit;
    reg [4:0] mac_phase;
    integer i;
    always @(posedge clk) begin
        emit <= 1'b0;
        if (rst) begin
            phase <= 0; skip <= 0;
            for (i=0;i<TAPS;i=i+1) begin ll[i]<=0; rl[i]<=0; end
        end else if (in_valid) begin
            for (i=TAPS-1;i>0;i=i-1) begin ll[i]<=ll[i-1]; rl[i]<=rl[i-1]; end
            ll[0]<=in_l; rl[0]<=in_r;
            if (skip)
                skip <= 1'b0;
            else begin
                emit      <= 1'b1;
                mac_phase <= phase;
                skip      <= (phase==L-1);
                phase     <= (phase==L-1) ? 5'd0 : phase+5'd1;
            end
        end
    end

    // --- one time-multiplexed multiplier: 16 taps of L, then 16 of R ------------
    localparam IDLE=2'd0, MACL=2'd1, MACR=2'd2, EMIT=2'd3;
    reg [1:0]         st;
    reg [4:0]         j;
    reg signed [39:0] accl, accr;
    wire signed [15:0] cf   = coeff[mac_phase*TAPS + j];
    wire signed [15:0] smp  = (st==MACL) ? ll[j] : rl[j];
    wire signed [39:0] prod = cf * smp;   // one 16x16 multiplier (1 DSP block)

    function signed [15:0] sat16;
        input signed [39:0] a;
        reg signed [39:0] r;
        begin
            r = (a + 40'sd16384) >>> 15;                 // round Q15 -> int
            if (r >  40'sd32767)      sat16 =  16'sd32767;
            else if (r < -40'sd32768) sat16 = -16'sd32768;
            else                      sat16 = r[15:0];
        end
    endfunction

    always @(posedge clk) begin
        out_valid <= 1'b0;
        if (rst) begin st<=IDLE; accl<=0; accr<=0; j<=0; end
        else case (st)
            IDLE: if (emit) begin accl<=0; accr<=0; j<=0; st<=MACL; end
            MACL: begin
                accl <= accl + prod;
                if (j==TAPS-1) begin j<=0; st<=MACR; end else j<=j+5'd1;
            end
            MACR: begin
                accr <= accr + prod;
                if (j==TAPS-1) st<=EMIT; else j<=j+5'd1;
            end
            EMIT: begin
                out_l     <= sat16(accl);
                out_r     <= sat16(accr);
                out_valid <= 1'b1;
                st        <= IDLE;
            end
        endcase
    end
endmodule
