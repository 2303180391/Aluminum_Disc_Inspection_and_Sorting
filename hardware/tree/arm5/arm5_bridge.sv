`timescale 1ns/1ps
module arm5_bridge #(parameter integer CLK_HZ=74250000)(
 input wire clk,resetn,key_n,write_command,input wire [7:0] command,
 input wire [3:0] index,input wire [31:0] write_data,
 input wire data_write,execute,output reg [31:0] read_data,
 output wire [31:0] status,output wire [4:0] pwm);
 reg enabled,locked,request,done;
 reg [1:0] category;
 reg [15:0] params[0:5];reg [10:0] descriptor;
 reg cmd_valid;reg [7:0] cmd;reg [2:0] argc;
 wire busy,reply_valid,observation_valid,calibration_valid;
 wire [3:0] reply_code,stage,cal_id;wire cal_plane;
 wire [31:0] cal_pose;wire [7:0] a0,a1,a2,a3,a4;
 reg [15:0] reply_sequence;reg [3:0] last_reply;
 wire [4:0] raw_pwm;
 reg key_meta,key_sync,key_stable,key_armed;reg [31:0] debounce;
 localparam integer DB=(CLK_HZ/50<1)?1:CLK_HZ/50;
 wire idle=enabled && !locked && !request && !busy && !cmd_valid;
 wire disable_now=write_command && !command[0];
 assign pwm=(enabled && !disable_now)?raw_pwm:5'b0;
 assign status={21'b0,stage[2:0],category,done,busy,(enabled && !busy),request,locked,enabled};
 integer i;
 always @* begin
  read_data=0;
  case(index)
   0,1,2,3,4,5:read_data={16'b0,params[index]};
   6:read_data={21'b0,descriptor};
   7:read_data={reply_sequence,12'b0,last_reply};
   8:read_data={a3,a2,a1,a0};
   9:read_data={24'b0,a4};
   10:read_data=cal_pose;
   11:read_data={27'b0,cal_plane,cal_id};
   12:read_data={26'b0,stage,calibration_valid,observation_valid};
  endcase
 end
 always @(posedge clk) begin
  if(!resetn)begin
   enabled<=0;locked<=0;request<=0;done<=0;category<=0;
   descriptor<=0;cmd_valid<=0;cmd<=0;argc<=0;reply_sequence<=0;last_reply<=0;
   key_meta<=1;key_sync<=1;key_stable<=0;key_armed<=0;debounce<=0;
   for(i=0;i<6;i=i+1)params[i]<=0;
  end else begin
   cmd_valid<=0;key_meta<=key_n;key_sync<=key_meta;
   if(reply_valid)begin
    reply_sequence<=reply_sequence+1'b1;last_reply<=reply_code;
    if(reply_code==4)done<=1;
   end
   if(!idle)begin key_armed<=0;key_stable<=0;debounce<=0;end
   else if(key_sync==key_stable)begin debounce<=0;if(key_stable)key_armed<=1;end
   else if(debounce==DB-1)begin
    debounce<=0;key_stable<=key_sync;
    if(!key_sync && key_armed)begin request<=1;locked<=1;key_armed<=0;end
   end else debounce<=debounce+1;
   if(data_write)begin
    if(index<6)params[index]<=write_data[15:0];
    if(index==6)descriptor<=write_data[10:0];
   end
   if(execute)begin
    // Configuration/read commands are allowed with PWM off; motion is not.
    if(enabled || descriptor[7:0]=="W" || descriptor[7:0]=="D" ||
       descriptor[7:0]=="E" || descriptor[7:0]=="Q" || descriptor[7:0]=="S" || descriptor[7:0]=="X")begin
     cmd<=descriptor[7:0];argc<=descriptor[10:8];cmd_valid<=1;done<=0;
     if(descriptor[7:0]=="K")category<=params[0][1:0];
    end else begin reply_sequence<=reply_sequence+1'b1;last_reply<=11;end
   end
   if(write_command)begin
    enabled<=command[0];
    if(!command[0])begin
     locked<=0;request<=0;done<=0;cmd<="X";argc<=0;cmd_valid<=1;
    end else begin
     locked<=command[1];
     if(command[2])request<=0;
     if(command[6] && idle)begin request<=1;locked<=1;end
    end
   end
  end
 end
 arm5_controller #(.CLK_HZ(CLK_HZ)) controller(
  .clk(clk),.rst_n(resetn),.ready(1'b1),.command_valid(cmd_valid),.command_error(1'b0),
  .command(cmd),.argc(argc),.p0(params[0]),.p1(params[1]),.p2(params[2]),
  .p3(params[3]),.p4(params[4]),.p5(params[5]),.angle0(a0),.angle1(a1),
  .angle2(a2),.angle3(a3),.angle4(a4),.reply_valid(reply_valid),.reply_code(reply_code),
  .calibration_reply_pose(cal_pose),.calibration_reply_id(cal_id),.calibration_reply_plane(cal_plane),
  .busy(busy),.observation_valid(observation_valid),.calibration_valid(calibration_valid),.current_stage(stage));
 servo_pwm_5ch #(.CLK_FREQ_HZ(CLK_HZ)) outputs5(.clk_27m(clk),.rst_n(resetn && enabled),
  .angle_ch0(a0),.angle_ch1(a1),.angle_ch2(a2),.angle_ch3(a3),.angle_ch4(a4),
  .pwm_ch0(raw_pwm[0]),.pwm_ch1(raw_pwm[1]),.pwm_ch2(raw_pwm[2]),.pwm_ch3(raw_pwm[3]),.pwm_ch4(raw_pwm[4]));
endmodule
