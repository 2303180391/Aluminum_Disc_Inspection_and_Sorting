`timescale 1ns/1ps
module tb_arm5_bridge;
 reg clk=0;always #5 clk=~clk;
 reg resetn=0,key_n=1,write_command=0,data_write=0,execute=0;
 reg [7:0] command=0;reg [3:0] index=0;reg [31:0] write_data=0;
 wire [31:0] read_data,status;wire [4:0] pwm;
 arm5_bridge #(.CLK_HZ(1000000)) dut(.*);
 task tick(input integer n);repeat(n)begin @(posedge clk);#1;end endtask
 task ctl(input [7:0] c);begin @(negedge clk);command=c;write_command=1;tick(1);@(negedge clk);write_command=0;tick(3);end endtask
 task paramwrite(input [3:0] i,input [31:0] d);begin @(negedge clk);index=i;write_data=d;data_write=1;tick(1);@(negedge clk);data_write=0;tick(2);end endtask
 task issue(input [7:0] c,input [2:0] n);begin paramwrite(6,c|(n<<8));@(negedge clk);execute=1;tick(1);@(negedge clk);execute=0;tick(8);end endtask
 initial begin
  tick(4);resetn=1;tick(20);
  if(pwm || status[0])$fatal(1,"PWM not disabled at reset");
  paramwrite(0,1);paramwrite(1,100);issue("J",2);
  index=7;#1;if(read_data[3:0]!=11)$fatal(1,"motion allowed disabled");
  ctl(1);issue("O",0);index=12;#1;
  if(!read_data[0])$fatal(1,"observation not stored");
  paramwrite(0,1);paramwrite(1,95);issue("J",2);
  if(!status[4])$fatal(1,"manual motion not started");
  ctl(0);tick(200);
  if(pwm || status[4] || status[0])$fatal(1,"disable failed");
  ctl(1);tick(40000);ctl(8'h41);
  if(!status[2] || !status[1])$fatal(1,"trigger not latched");
  ctl(7);if(status[2])$fatal(1,"request not acknowledged");
  ctl(8'h43);if(status[2])$fatal(1,"locked retrigger accepted");
  ctl(5);key_n=1;tick(40000);key_n=0;tick(21000);
  if(!status[2] || !status[1])$fatal(1,"physical key not accepted");
  ctl(5);tick(40000);
  if(status[2])$fatal(1,"held key retriggered");
  key_n=1;tick(21000);key_n=0;tick(21000);
  if(!status[2])$fatal(1,"new physical key press rejected");
  ctl(0);$display("ARM5_BRIDGE_PASS");$finish;
 end
 initial begin #5000000;$fatal(1,"timeout");end
endmodule
