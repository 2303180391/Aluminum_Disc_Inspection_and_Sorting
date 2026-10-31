`timescale 1ns/1ps
// Independent integer reference, including extreme dimensions and cancellation.
module tb_xy_interpolator;
    reg clk=0;
    always #5 clk=~clk;
    reg rst_n=0, enable=0;
    reg [9:0] width,height,dx,dy;
    reg [31:0] corners0,corners2;
    wire done;
    wire [7:0] result0,result2;
    arm_xy_interpolator dut(.*);
    integer seed=14209;
    integer a0[0:3], a2[0:3];
    integer w[0:3];
    integer n,k,sum0,sum2,den,expected0,expected2,cycles;
    task check;
        begin
            w[0]=(width-dx)*(height-dy); w[1]=dx*(height-dy);
            w[2]=(width-dx)*dy; w[3]=dx*dy;
            sum0=0; sum2=0;
            for(k=0;k<4;k=k+1) begin
                sum0=sum0+a0[k]*w[k]; sum2=sum2+a2[k]*w[k];
            end
            den=width*height;
            expected0=(sum0+den/2)/den; expected2=(sum2+den/2)/den;
            @(negedge clk); enable=1; cycles=0;
            while(!done && cycles<100) begin @(negedge clk); cycles=cycles+1; end
            if(!done || result0!==expected0[7:0] || result2!==expected2[7:0])
                $fatal(1,"Interpolation %0d/%0d x %0d/%0d expected %0d,%0d got %0d,%0d done=%b",
                    dx,width,dy,height,expected0,expected2,result0,result2,done);
            repeat(5) @(negedge clk);
            if(!done || result0!==expected0[7:0] || result2!==expected2[7:0])
                $fatal(1,"Result not held");
            enable=0; @(negedge clk);
            if(done) $fatal(1,"Done not cleared");
        end
    endtask
    initial begin
        repeat(2) @(negedge clk); rst_n=1;
        for(n=0;n<350;n=n+1) begin
            width=1+($random(seed)&32'h7fffffff)%1000;
            height=1+($random(seed)&32'h7fffffff)%1000;
            dx=($random(seed)&32'h7fffffff)%(width+1);
            dy=($random(seed)&32'h7fffffff)%(height+1);
            if(n<8) begin
                width=(n<4)?1000:1; height=width;
                dx=n[0]?width:0; dy=n[1]?height:0;
            end
            for(k=0;k<4;k=k+1) begin
                a0[k]=($random(seed)&32'h7fffffff)%181;
                a2[k]=($random(seed)&32'h7fffffff)%181;
                if(n==8) begin a0[k]=180; a2[k]=0; end
                corners0[k*8 +: 8]=a0[k]; corners2[k*8 +: 8]=a2[k];
            end
            if(n==8) begin width=1000; height=1000; dx=500; dy=500; end
            check;
        end
        // Abort in the middle of a division, then calculate new inputs.
        enable=1; repeat(20) @(negedge clk); enable=0;
        repeat(3) @(negedge clk);
        if(done) $fatal(1,"Cancelled calculation completed");
        width=3; height=7; dx=1; dy=5; check;
        $display("PASS: 351 bilinear reference cases, rounding, full range, hold and cancellation");
        $finish;
    end
endmodule
