`timescale 1ns/1ps
module tb_arm5_regs;
reg clk=0;always #5 clk=~clk;
reg resetn=0,frame_start=0;
reg [6:0] s_axi_awaddr=0,s_axi_araddr=0;
reg [2:0] s_axi_awprot=0,s_axi_arprot=0;
reg s_axi_awvalid=0,s_axi_wvalid=0,s_axi_bready=1,s_axi_arvalid=0,s_axi_rready=1;
wire s_axi_awready,s_axi_wready,s_axi_bvalid,s_axi_arready,s_axi_rvalid;
reg [31:0] s_axi_wdata=0;
reg [3:0] s_axi_wstrb=0;
wire [1:0] s_axi_bresp,s_axi_rresp;
wire [31:0] s_axi_rdata;
wire [511:0] active_coords;wire [127:0] active_classes;wire [3:0] active_mask;
reg [31:0] arm_status=0,arm_read_data=32'h12345678;
wire arm_write,arm_data_write,arm_exec;wire [7:0] arm_command;
wire [3:0] arm_index;wire [31:0] arm_data;
integer executions=0,writes=0;
multibox_regs dut(.*);
always @(posedge clk) begin
 if(arm_exec) executions=executions+1;
 if(arm_data_write) writes=writes+1;
end
task wr(input [6:0] addr,input [31:0] value,input [3:0] strb,input integer data_first,input [1:0] resp);
begin
 @(negedge clk);
 if(data_first)begin s_axi_wvalid=1;s_axi_wdata=value;s_axi_wstrb=strb;end
 else begin s_axi_awvalid=1;s_axi_awaddr=addr;end
 @(negedge clk);s_axi_wvalid=0;s_axi_awvalid=0;
 repeat(3) @(negedge clk);
 if(data_first)begin s_axi_awvalid=1;s_axi_awaddr=addr;end
 else begin s_axi_wvalid=1;s_axi_wdata=value;s_axi_wstrb=strb;end
 @(negedge clk);s_axi_wvalid=0;s_axi_awvalid=0;
 while(!s_axi_bvalid)@(negedge clk);
 if(s_axi_bresp!==resp)$fatal(1,"AXI response mismatch");
 repeat(3)@(negedge clk);
end
endtask
initial begin
 repeat(3)@(negedge clk);resetn=1;
 wr('h74,6,15,0,0);if(arm_index!=6)$fatal(1,"index write");
 wr('h74,3,1,1,2);if(arm_index!=6)$fatal(1,"partial index altered");
 wr('h78,'h24a,15,1,0);if(arm_data!='h24a || writes!=1)$fatal(1,"data write");
 wr('h78,0,1,0,2);if(writes!=1)$fatal(1,"partial data accepted");
 wr('h7c,'h41583521,1,1,2);
 wr('h7c,0,15,0,2);
 if(executions!=0)$fatal(1,"bad execution accepted");
 wr('h7c,'h41583521,15,1,0);
 if(executions!=1)$fatal(1,"execution pulse mismatch");
 wr('h64,1,1,0,0);if(arm_command!=1)$fatal(1,"legacy command");
 $display("ARM5_AXI_REGS_PASS");$finish;
end
initial begin #100000;$fatal(1,"timeout");end
endmodule
