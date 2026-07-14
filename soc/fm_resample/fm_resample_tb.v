`timescale 1ns/1ps
module tb;
    reg clk=0, rst=1, in_valid=0;
    reg signed [15:0] in_l=0, in_r=0;
    wire out_valid; wire signed [15:0] out_l, out_r;
    fm_resample dut(.clk(clk),.rst(rst),.in_valid(in_valid),.in_l(in_l),.in_r(in_r),
                    .out_valid(out_valid),.out_l(out_l),.out_r(out_r));
    always #5 clk=~clk;                       // 100 MHz sim clock

    reg [15:0] xin  [0:8191];
    reg [15:0] gold [0:8191];
    integer nin, ng, i, k, oidx, errs;
    initial begin
        $readmemh("tb_in.txt",  xin);
        $readmemh("tb_gold.txt",gold);
        nin=4000;
        ng=3875;
        repeat(4) @(posedge clk); rst=0; @(posedge clk);
        oidx=0; errs=0;
        for (i=0;i<nin;i=i+1) begin
            @(posedge clk); in_valid<=1; in_l<=xin[i]; in_r<=xin[i];
            @(posedge clk); in_valid<=0;
            repeat(40) @(posedge clk);        // let the MAC finish (>=32 cyc)
        end
        $display("TB: fed %0d inputs, captured %0d outputs, golden %0d; mismatches=%0d",
                 nin, oidx, ng, errs);
        if (errs==0 && oidx>100) $display("RESULT: PASS (Verilog == bit-exact reference)");
        else $display("RESULT: FAIL");
        $finish;
    end
    // capture + compare on each out_valid
    always @(posedge clk) if (!rst && out_valid) begin
        if (oidx<ng && out_l!==$signed(gold[oidx])) begin
            if (errs<8) $display("  mismatch @%0d: got %0d exp %0d", oidx, out_l, $signed(gold[oidx]));
            errs=errs+1;
        end
        if (out_r!==out_l) begin if(errs<8)$display("  L!=R @%0d",oidx); errs=errs+1; end
        oidx=oidx+1;
    end
endmodule
