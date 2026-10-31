`timescale 1ns/1ps
// Same-clock AXI-Lite and video top. resetn must be released synchronously to clk.
module multibox_overlay (
 (* X_INTERFACE_PARAMETER="ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET resetn" *)
 (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 clk CLK" *) input wire clk,
 (* X_INTERFACE_PARAMETER="POLARITY ACTIVE_LOW" *)
 (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 resetn RST" *) input wire resetn,
 input wire [6:0] s_axi_awaddr, input wire [2:0] s_axi_awprot,
 input wire s_axi_awvalid, output wire s_axi_awready,
 input wire [31:0] s_axi_wdata, input wire [3:0] s_axi_wstrb,
 input wire s_axi_wvalid, output wire s_axi_wready,
 output wire [1:0] s_axi_bresp, output wire s_axi_bvalid, input wire s_axi_bready,
 input wire [6:0] s_axi_araddr, input wire [2:0] s_axi_arprot,
 input wire s_axi_arvalid, output wire s_axi_arready,
 output wire [31:0] s_axi_rdata, output wire [1:0] s_axi_rresp,
 output wire s_axi_rvalid, input wire s_axi_rready,
 input wire vga_hsync_i,vga_vsync_i,vga_de_i,input wire [23:0] vga_data_i,
 output wire vga_hsync_o,vga_vsync_o,vga_de_o,output wire [23:0] vga_data_o,
 input wire arm_key_n, output wire [4:0] arm_pwm
);
 wire [511:0] coords;wire [127:0] classes;wire [3:0] mask;wire frame_start;
 wire [31:0] arm_status;wire arm_write;wire [7:0] arm_command;
 wire [3:0] arm_index;wire [31:0] arm_data,arm_read_data;wire arm_data_write,arm_exec;
 arm5_bridge arm(.clk(clk),.resetn(resetn),.key_n(arm_key_n),
 .write_command(arm_write),.command(arm_command),
 .status(arm_status),.pwm(arm_pwm),.index(arm_index),.write_data(arm_data),
 .data_write(arm_data_write),.execute(arm_exec),.read_data(arm_read_data));
 multibox_regs regs(.clk(clk),.resetn(resetn),.frame_start(frame_start),
 .s_axi_awaddr(s_axi_awaddr),.s_axi_awprot(s_axi_awprot),.s_axi_awvalid(s_axi_awvalid),.s_axi_awready(s_axi_awready),
 .s_axi_wdata(s_axi_wdata),.s_axi_wstrb(s_axi_wstrb),.s_axi_wvalid(s_axi_wvalid),.s_axi_wready(s_axi_wready),
 .s_axi_bresp(s_axi_bresp),.s_axi_bvalid(s_axi_bvalid),.s_axi_bready(s_axi_bready),
 .s_axi_araddr(s_axi_araddr),.s_axi_arprot(s_axi_arprot),.s_axi_arvalid(s_axi_arvalid),.s_axi_arready(s_axi_arready),
 .s_axi_rdata(s_axi_rdata),.s_axi_rresp(s_axi_rresp),.s_axi_rvalid(s_axi_rvalid),.s_axi_rready(s_axi_rready),
 .active_coords(coords),.active_classes(classes),.active_mask(mask),
 .arm_status(arm_status),.arm_write(arm_write),.arm_command(arm_command),
 .arm_index(arm_index),.arm_data(arm_data),.arm_data_write(arm_data_write),
 .arm_exec(arm_exec),.arm_read_data(arm_read_data));
 multibox_video video(.clk(clk),.resetn(resetn),.active_coords(coords),.active_classes(classes),.active_mask(mask),
 .hsync_i(vga_hsync_i),.vsync_i(vga_vsync_i),.de_i(vga_de_i),.rgb_i(vga_data_i),
 .hsync_o(vga_hsync_o),.vsync_o(vga_vsync_o),.de_o(vga_de_o),.rgb_o(vga_data_o),.frame_start(frame_start));
endmodule
