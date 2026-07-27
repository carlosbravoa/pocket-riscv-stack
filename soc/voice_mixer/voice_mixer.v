// Voice mixer core — NV hardware PCM voices, one shared voice multiplier plus a
// dedicated master-volume multiplier (~2 DSPs total).
// Contract: soc/voice_mixer/README.md (bit-exact vs gen_ref.py).
// P1: core only. Memory access + format decode live behind the fetch port.
// SPDX-License-Identifier: BSD-2-Clause
`timescale 1ns/1ps

module voice_mixer #(
    parameter NV = 32
) (
    input  wire        clk,
    input  wire        rst,

    // ---- config port (CSR glue in P2; TB in P1) ----
    input  wire [4:0]  cfg_sel,
    input  wire        cfg_we,
    input  wire [2:0]  cfg_field,   // 0 BASE,1 LEN,2 FMT|LOOP,3 LOOP_START,
                                    // 4 LOOP_END,5 STEP,6 VOLPAN,7 CTRL
    input  wire [31:0] cfg_data,
    input  wire [7:0]  master_vol,

    output wire [31:0] active_mask,
    input  wire [4:0]  pos_sel,
    output wire [23:0] pos_rd,

    // ---- sample fetch port (wrapper decodes memory to s16) ----
    output reg         f_req,
    output wire [4:0]  f_voice,
    output reg  [25:0] f_addr,
    output reg  [1:0]  f_fmt,
    input  wire        f_ack,
    input  wire signed [15:0] f_data,

    // ---- output ----
    input  wire        frame_tick,  // 48 kHz strobe
    output reg  signed [15:0] out_l,
    output reg  signed [15:0] out_r,
    output reg         out_valid
);

    // ------------------------------------------------------------- voice state
    reg [25:0] v_base  [0:NV-1];
    reg [23:0] v_len   [0:NV-1];
    reg [1:0]  v_fmt   [0:NV-1];    // 0 s8, 1 u8, 2 s16
    reg [1:0]  v_loop  [0:NV-1];    // 0 none, 1 fwd
    reg [23:0] v_ls    [0:NV-1];
    reg [23:0] v_le    [0:NV-1];
    reg [23:0] v_step  [0:NV-1];    // 8.16
    reg [7:0]  v_vol   [0:NV-1];
    reg [7:0]  v_pan   [0:NV-1];
    reg [39:0] v_pos   [0:NV-1];    // 24.16
    reg [NV-1:0] v_act;

    // key-on shadows
    reg [25:0] sh_base;
    reg [23:0] sh_len, sh_ls, sh_le;
    reg [1:0]  sh_fmt, sh_loop;

    assign active_mask = {32'd0} | v_act;
    assign pos_rd      = v_pos[pos_sel][39:16];

    // ------------------------------------------------------------- FSM
    localparam S_IDLE=0, S_LOAD=1, S_F0=2, S_F0W=3, S_F1=4, S_F1W=5,
               S_INTERP=6, S_VOL=7, S_PANL=8, S_PANR=9, S_ADV=10, S_WB=11,
               S_OUTL=12, S_OUTR=13;
    reg [3:0]  st;
    reg [4:0]  cv;
    reg signed [23:0] acc_l, acc_r;

    reg [39:0] w_pos;
    reg        outr_pend;
    reg [23:0] w_idx0, w_idx1;
    reg signed [15:0] w_s0, w_sv;
    // per-voice fields latched at S_LOAD: every downstream state reads a REG,
    // not a 32:1 array mux (the array cones broke 74.25 MHz timing closure).
    reg [25:0] w_base;
    reg [23:0] w_len, w_ls, w_le, w_step;
    reg [1:0]  w_fmt, w_loop;
    reg [7:0]  w_vol, w_pan;
    reg [39:0] w_np;

    // shared voice multiplier (s18 x s18)
    reg  signed [17:0] mul_a, mul_b;
    wire signed [35:0] mul_p = mul_a * mul_b;

    // dedicated master multiplier (s24 x u8 -> s32); only used at frame end
    reg  signed [23:0] mmul_a;
    wire signed [32:0] mmul_p = mmul_a * $signed({1'b0, master_vol});

    function [25:0] samp_addr(input [25:0] base, input [23:0] idx, input [1:0] fmt);
        samp_addr = (fmt == 2'd2) ? (base + {idx, 1'b0}) : (base + {2'b0, idx});
    endfunction

    function [23:0] next_idx(input [23:0] idx, input [1:0] lmode,
                             input [23:0] len, input [23:0] ls, input [23:0] le);
        begin
            next_idx = idx + 24'd1;
            if (lmode == 2'd1) begin
                if (next_idx >= le) next_idx = ls;
            end else begin
                if (next_idx >= len) next_idx = len - 24'd1;
            end
        end
    endfunction

    function signed [15:0] sat16(input signed [24:0] x);
        sat16 = (x > 25'sd32767)  ? 16'sd32767 :
                (x < -25'sd32768) ? -16'sd32768 : x[15:0];
    endfunction

    assign f_voice = cv;

    wire [15:0] w_frac = w_pos[15:0];
    reg  [39:0] np;

    always @(posedge clk) begin
        out_valid <= 1'b0;

        // ---------------- frame FSM ----------------
        case (st)
        S_IDLE: if (frame_tick) begin
            acc_l <= 24'sd0; acc_r <= 24'sd0; cv <= 5'd0; st <= S_LOAD;
        end
        S_LOAD: begin
            if (!v_act[cv[4:0]]) begin
                if (cv == NV-1) st <= S_OUTL; else cv <= cv + 5'd1;
            end else begin
                w_pos  <= v_pos[cv];
                w_idx0 <= v_pos[cv][39:16];
                w_base <= v_base[cv];  w_len <= v_len[cv];
                w_fmt  <= v_fmt[cv];   w_loop <= v_loop[cv];
                w_ls   <= v_ls[cv];    w_le  <= v_le[cv];
                w_step <= v_step[cv];  w_vol <= v_vol[cv];
                w_pan  <= v_pan[cv];
                st     <= S_F0;
            end
        end
        S_F0: begin
            f_addr <= samp_addr(w_base, w_idx0, w_fmt);
            f_fmt  <= w_fmt;
            f_req  <= 1'b1;
            w_idx1 <= next_idx(w_idx0, w_loop, w_len, w_ls, w_le);
            st     <= S_F0W;
        end
        S_F0W: if (f_ack) begin
            f_req <= 1'b0; w_s0 <= f_data; st <= S_F1;
        end
        S_F1: begin
            f_addr <= samp_addr(w_base, w_idx1, w_fmt);
            f_req  <= 1'b1;
            st     <= S_F1W;
        end
        S_F1W: if (f_ack) begin
            f_req <= 1'b0;
            mul_a <= $signed({2'b00, w_frac});          // frac (u16, positive)
            mul_b <= $signed({{2{f_data[15]}}, f_data}) - $signed({{2{w_s0[15]}}, w_s0});
            st    <= S_INTERP;
        end
        S_INTERP: begin
            // mul_p = (s1-s0)*frac ; s = s0 + (p >>> 16)
            mul_b <= $signed(w_s0) + $signed(mul_p[33:16]); // s = interpolated (s16-safe)
            mul_a <= $signed({10'b0, w_vol});
            st    <= S_VOL;
        end
        S_VOL: begin
            // mul_p = s * vol ; sv = p >>> 8
            w_sv  <= mul_p[23:8];
            mul_b <= $signed(mul_p[23:8]);
            mul_a <= $signed({10'b0, 8'd255 - w_pan});
            st    <= S_PANL;
        end
        S_PANL: begin
            // mul_p = sv * (255-pan) ; l = p >>> 8
            acc_l <= acc_l + $signed(mul_p[31:8]);
            mul_a <= $signed({10'b0, w_pan});
            mul_b <= $signed(w_sv);
            st    <= S_PANR;
        end
        S_PANR: begin
            st    <= S_ADV;                          // mul_p (= sv*pan) holds
        end
        S_ADV: begin
            acc_r <= acc_r + $signed(mul_p[31:8]);   // right-pan accumulate
            w_np  <= w_pos + {16'd0, w_step};        // 40-bit add, alone
            st    <= S_WB;
        end
        S_WB: begin
            np = w_np;
            if (w_loop == 2'd1) begin
                if (np[39:16] >= w_le)
                    np = np - {w_le - w_ls, 16'd0};
            end else begin
                if (np[39:16] >= w_len) v_act[cv] <= 1'b0;
            end
            v_pos[cv] <= np;
            if (cv == NV-1) st <= S_OUTL; else begin cv <= cv + 5'd1; st <= S_LOAD; end
        end
        S_OUTL: begin
            mmul_a <= acc_l;
            st     <= S_OUTR;
        end
        S_OUTR: begin
            out_l  <= sat16(mmul_p[32:8]);
            mmul_a <= acc_r;
            st     <= S_IDLE;                        // out_r lands next cycle
            out_valid <= 1'b0;
            // register out_r + valid one cycle later via the shadow below
            outr_pend <= 1'b1;
        end
        default: st <= S_IDLE;
        endcase

        // deferred R output (one cycle after S_OUTR so mmul_p reflects acc_r)
        if (outr_pend) begin
            out_r     <= sat16(mmul_p[32:8]);
            out_valid <= 1'b1;
            outr_pend <= 1'b0;
        end

        // ---------------- config port (wins over FSM on same-cycle conflicts) ----------------
        if (cfg_we) begin
            case (cfg_field)
                3'd0: sh_base <= cfg_data[25:0];
                3'd1: sh_len  <= cfg_data[23:0];
                3'd2: begin sh_fmt <= cfg_data[1:0]; sh_loop <= cfg_data[3:2]; end
                3'd3: sh_ls   <= cfg_data[23:0];
                3'd4: sh_le   <= cfg_data[23:0];
                3'd5: v_step[cfg_sel] <= cfg_data[23:0];
                3'd6: begin v_vol[cfg_sel] <= cfg_data[15:8]; v_pan[cfg_sel] <= cfg_data[7:0]; end
                3'd7: begin
                    if (cfg_data[0]) begin
                        v_base[cfg_sel] <= sh_base;
                        v_len [cfg_sel] <= sh_len;
                        v_fmt [cfg_sel] <= sh_fmt;
                        v_loop[cfg_sel] <= sh_loop;
                        v_ls  [cfg_sel] <= sh_ls;
                        v_le  [cfg_sel] <= sh_le;
                        v_pos [cfg_sel] <= 40'd0;
                        v_act [cfg_sel] <= 1'b1;
                    end
                    if (cfg_data[1]) v_act [cfg_sel] <= 1'b0;
                    if (cfg_data[2]) v_loop[cfg_sel] <= 2'd0;
                end
            endcase
        end

        if (rst) begin
            st <= S_IDLE; f_req <= 1'b0; v_act <= {NV{1'b0}};
            outr_pend <= 1'b0; out_valid <= 1'b0;
        end
    end

endmodule
