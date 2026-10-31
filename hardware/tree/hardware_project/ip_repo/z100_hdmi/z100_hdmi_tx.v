`timescale 1ns / 1ps

// Z100 board HDMI transmitter adapter.
// The board uses an MS7210 parallel-RGB-to-HDMI transmitter, not direct TMDS pins.
module z100_hdmi_tx #(
    parameter integer CFG_CLK_FREQ_HZ = 74250000
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 cfg_clk CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME cfg_clk, FREQ_HZ 74250000, ASSOCIATED_RESET rst_n" *)
    input  wire        cfg_clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 pixel_clk CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME pixel_clk, FREQ_HZ 74250000, ASSOCIATED_RESET rst_n" *)
    input  wire        pixel_clk,
    // The MS7210 samples the parallel RGB bus with this forwarded clock.
    // The board vendor reference drives it 45 degrees after the data clock
    // to provide setup/hold margin at the transmitter input.
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 hdmi_clk_shifted CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME hdmi_clk_shifted, FREQ_HZ 74250000, PHASE 45.0" *)
    input  wire        hdmi_clk_shifted,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rst_n RST" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME rst_n, POLARITY ACTIVE_LOW" *)
    input  wire        rst_n,

    input  wire [23:0] video_in_data,
    input  wire        video_in_hs,
    input  wire        video_in_vs,
    input  wire        video_in_de,

    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 hdmi_rst_n RST" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME hdmi_rst_n, POLARITY ACTIVE_LOW" *)
    output wire        hdmi_rst_n,
    output wire        hdmi_iic_scl,
    inout  wire        hdmi_iic_sda,
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 hdmi_clk CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME hdmi_clk, FREQ_HZ 74250000, ASSOCIATED_RESET hdmi_rst_n" *)
    output wire        hdmi_clk,
    output wire [23:0] hdmi_data,
    output wire        hdmi_hs,
    output wire        hdmi_vs,
    output wire        hdmi_de
);

assign hdmi_clk  = hdmi_clk_shifted;
assign hdmi_data = video_in_data;
assign hdmi_hs   = video_in_hs;
assign hdmi_vs   = video_in_vs;
assign hdmi_de   = video_in_de;

// The vendor source assumes a 10 MHz input by default. Override it so the
// I2C divider remains correct when driven by the existing 74.25 MHz pixel clock.
ms72xx_ctl #(
    .CLK_FREQ(CFG_CLK_FREQ_HZ)
) u_ms72xx_ctl (
    .clk      (cfg_clk),
    .rst_n    (rst_n),
    .rstn_out (hdmi_rst_n),
    .init_over(),
    .iic_scl  (hdmi_iic_scl),
    .iic_sda  (hdmi_iic_sda)
);

endmodule
