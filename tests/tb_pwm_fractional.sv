`timescale 1ns/1ps
module tb_pwm_fractional;
 reg clk=0;always #1 clk=~clk;
 reg rst_n=0;wire pwm;reg [7:0] angle_deg=90;
 integer cycle=0,rise=-1,high_count=0,periods=0;
 servo_pwm #(.CLK_FREQ_HZ(74250000)) dut(.clk(clk),.rst_n(rst_n),.angle_deg(angle_deg),.pwm_out(pwm));
 reg last=0;
 always @(posedge clk)begin
  #0.1;cycle=cycle+1;
  if(pwm && !last)begin
   if(rise>=0)begin
    if(cycle-rise!=1485000)$fatal(1,"wrong 20ms period %d",cycle-rise);
    periods=periods+1;
    if(periods==1)begin $display("ARM5_PWM_74250000_PASS");$finish;end
   end
   rise=cycle;high_count=0;
  end
  if(pwm)high_count=high_count+1;
  if(!pwm && last && high_count!=111375)$fatal(1,"wrong 1500us pulse %d",high_count);
  last=pwm;
 end
 initial begin repeat(3)@(negedge clk);rst_n=1;end
 initial begin #4000000;$fatal(1,"timeout");end
endmodule
