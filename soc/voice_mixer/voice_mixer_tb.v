// Bit-exact bench for voice_mixer.v — serves the fetch port from a behavioral
// RAM (tb_samples.txt), programs voices from tb_cfg.txt, runs N frames and
// compares L/R against tb_gold.txt. RESULT: PASS/FAIL on stdout.
// SPDX-License-Identifier: BSD-2-Clause
`timescale 1ns/1ps

module voice_mixer_tb;
    localparam NVU      = 12;      // voices used by gen_ref.py
    localparam NFRAMES  = 2000;
    localparam [7:0] MASTER = 8'hE0;

    reg clk = 0, rst = 1;
    always #5 clk = ~clk;

    // ---- DUT ----
    reg  [4:0]  cfg_sel  = 0;
    reg         cfg_we   = 0;
    reg  [2:0]  cfg_field= 0;
    reg  [31:0] cfg_data = 0;
    wire [31:0] active_mask;
    wire [23:0] pos_rd;
    wire        f_req;
    wire [4:0]  f_voice;
    wire [25:0] f_addr;
    wire [1:0]  f_fmt;
    reg         f_ack = 0;
    reg  signed [15:0] f_data = 0;
    reg         frame_tick = 0;
    wire signed [15:0] out_l, out_r;
    wire        out_valid;

    voice_mixer #(.NV(32)) dut (
        .clk(clk), .rst(rst),
        .cfg_sel(cfg_sel), .cfg_we(cfg_we), .cfg_field(cfg_field),
        .cfg_data(cfg_data), .master_vol(MASTER),
        .active_mask(active_mask), .pos_sel(5'd0), .pos_rd(pos_rd),
        .f_req(f_req), .f_voice(f_voice), .f_addr(f_addr), .f_fmt(f_fmt),
        .f_ack(f_ack), .f_data(f_data),
        .frame_tick(frame_tick),
        .out_l(out_l), .out_r(out_r), .out_valid(out_valid)
    );

    // ---- sample memory + fetch server (2-cycle latency) ----
    reg [7:0] mem [0:65535];
    reg [1:0] srv;
    always @(posedge clk) begin
        f_ack <= 1'b0;
        if (rst) srv <= 0;
        else case (srv)
            0: if (f_req) srv <= 1;
            1: srv <= 2;
            2: begin
                case (f_fmt)
                    2'd0: f_data <= $signed({mem[f_addr[15:0]], 8'd0});          // s8 << 8
                    2'd1: f_data <= $signed({mem[f_addr[15:0]] - 8'd128, 8'd0}); // (u8-128) << 8
                    default: f_data <= $signed({mem[f_addr[15:0]+16'd1], mem[f_addr[15:0]]});
                endcase
                f_ack <= 1'b1;
                srv   <= 3;
            end
            3: if (!f_req) srv <= 0;   // wait for req drop before next serve
        endcase
    end

    // ---- vectors ----
    reg [15:0] gold [0:2*NFRAMES-1];
    reg [27:0] c_base [0:NVU-1];
    reg [23:0] c_len  [0:NVU-1];
    reg [3:0]  c_fmt  [0:NVU-1];
    reg [3:0]  c_loop [0:NVU-1];
    reg [23:0] c_ls   [0:NVU-1];
    reg [23:0] c_le   [0:NVU-1];
    reg [23:0] c_step [0:NVU-1];
    reg [7:0]  c_vol  [0:NVU-1];
    reg [7:0]  c_pan  [0:NVU-1];

    integer fd, i, n, frame, errs, guard;

    task cfg_write(input [4:0] sel, input [2:0] field, input [31:0] data);
        begin
            @(posedge clk);
            cfg_sel <= sel; cfg_field <= field; cfg_data <= data; cfg_we <= 1;
            @(posedge clk);
            cfg_we <= 0;
        end
    endtask

    initial begin
        $readmemh("tb_samples.txt", mem);
        $readmemh("tb_gold.txt",    gold);

        fd = $fopen("tb_cfg.txt", "r");
        if (fd == 0) begin $display("RESULT: FAIL (no tb_cfg.txt)"); $finish; end
        for (i = 0; i < NVU; i = i + 1) begin
            n = $fscanf(fd, "%h %h %h %h %h %h %h %h %h",
                        c_base[i], c_len[i], c_fmt[i], c_loop[i],
                        c_ls[i], c_le[i], c_step[i], c_vol[i], c_pan[i]);
            if (n != 9) begin $display("RESULT: FAIL (cfg parse %0d)", i); $finish; end
        end
        $fclose(fd);

        repeat (4) @(posedge clk); rst = 0; repeat (2) @(posedge clk);

        // program + key-on each voice (shadows are shared: one voice at a time)
        for (i = 0; i < NVU; i = i + 1) begin
            cfg_write(i[4:0], 3'd0, {6'd0, c_base[i][25:0]});
            cfg_write(i[4:0], 3'd1, {8'd0, c_len[i]});
            cfg_write(i[4:0], 3'd2, {28'd0, c_loop[i][1:0], c_fmt[i][1:0]});
            cfg_write(i[4:0], 3'd3, {8'd0, c_ls[i]});
            cfg_write(i[4:0], 3'd4, {8'd0, c_le[i]});
            cfg_write(i[4:0], 3'd5, {8'd0, c_step[i]});
            cfg_write(i[4:0], 3'd6, {16'd0, c_vol[i], c_pan[i]});
            cfg_write(i[4:0], 3'd7, 32'h1);            // key_on (commit)
        end

        errs = 0;
        for (frame = 0; frame < NFRAMES; frame = frame + 1) begin
            @(posedge clk); frame_tick <= 1;
            @(posedge clk); frame_tick <= 0;
            guard = 0;
            while (!out_valid && guard < 4000) begin @(posedge clk); guard = guard + 1; end
            if (guard >= 4000) begin
                $display("RESULT: FAIL (frame %0d timed out)", frame); $finish;
            end
            if (out_l !== $signed(gold[2*frame]) || out_r !== $signed(gold[2*frame+1])) begin
                if (errs < 10)
                    $display("  mismatch frame %0d: got (%0d,%0d) exp (%0d,%0d)",
                             frame, out_l, out_r,
                             $signed(gold[2*frame]), $signed(gold[2*frame+1]));
                errs = errs + 1;
            end
            repeat (3) @(posedge clk);
        end

        // one-shots (voices 6,7,8) must have self-deactivated
        if (active_mask[6] || active_mask[7] || active_mask[8]) begin
            $display("  one-shot voices still active: mask=%08x", active_mask);
            errs = errs + 1;
        end

        $display("TB: %0d frames, mismatches=%0d, final active_mask=%08x",
                 NFRAMES, errs, active_mask);
        if (errs == 0) $display("RESULT: PASS (Verilog == bit-exact reference)");
        else           $display("RESULT: FAIL");
        $finish;
    end
endmodule
