`timescale 1ns/1ps

// Z100/XC7Z035 camera adapter.
// The board camera connector provides its own sensor clock, so no XCLK pin is
// driven by the FPGA.  The external interface is kept compatible with the
// existing YOLO video pipeline (1280x720 RGB888 plus sync/clock-enable).
module ov5640_top_timing (
    input  wire        s_rst_n,
    input  wire        clk_sys24m,

    output wire        ov5640_pwdn,
    output wire        ov5640_rst_n,
    output wire        ov5640_iic_scl,
    inout  wire        ov5640_iic_sda,
    input  wire        ov5640_pclk,
    input  wire        ov5640_href,
    input  wire        ov5640_vsync,
    input  wire [7:0]  ov5640_data,

    output wire        vid_ce,
    output wire        m_vsync,
    output wire        m_hsync,
    output wire        m_href,
    output wire [23:0] m_data,

    // JTAG/VIO-only observability.  These signals do not participate in the
    // video path; they make camera bring-up possible without a UART console.
    output wire [31:0] debug_status
);

localparam [12:0] CMOS_H_PIXEL  = 13'd1280;
localparam [12:0] CMOS_V_PIXEL  = 13'd720;
// Match the vendor's proven 1280x720 OV5640 profile.  These are sensor line
// and frame totals, not the HDMI active-area timing.
localparam [12:0] TOTAL_H_PIXEL = 13'd2570;
localparam [12:0] TOTAL_V_PIXEL = 13'd980;

wire        cam_init_done;
wire [15:0] cam_sensor_id;
wire        cam_i2c_error;
wire        frame_vsync;
wire        frame_href;
wire        pixel_valid;
wire [15:0] pixel_rgb565;

reg  [7:0]  debug_sys_counter;
reg  [7:0]  debug_pclk_counter;
reg  [7:0]  debug_vsync_counter;
reg         debug_vsync_d;
wire        debug_pclk_reset_n;
wire [11:0] debug_pixel_snapshot;

// Reset assertion is asynchronous; release is local to the sensor clock.
xpm_cdc_async_rst #(.DEST_SYNC_FF(3), .INIT_SYNC_FF(0),
    .RST_ACTIVE_HIGH(0)) u_debug_pclk_reset (
    .src_arst(s_rst_n), .dest_clk(ov5640_pclk),
    .dest_arst(debug_pclk_reset_n)
);
camera_debug_snapshot #(.WIDTH(12)) u_pixel_debug_snapshot (
    .src_clk(ov5640_pclk), .dst_clk(clk_sys24m),
    .src_data({debug_vsync_counter, ov5640_vsync, ov5640_href,
               pixel_valid, debug_pclk_counter[7]}),
    .dst_data(debug_pixel_snapshot)
);

// A changing system counter proves that the camera configuration clock is
// alive.  The PCLK and VSYNC counters independently prove that the sensor is
// producing a pixel clock and complete frames after SCCB initialization.
always @(posedge clk_sys24m or negedge s_rst_n) begin
    if (!s_rst_n)
        debug_sys_counter <= 8'd0;
    else
        debug_sys_counter <= debug_sys_counter + 1'b1;
end

always @(posedge ov5640_pclk or negedge debug_pclk_reset_n) begin
    if (!debug_pclk_reset_n) begin
        debug_pclk_counter  <= 8'd0;
        debug_vsync_counter <= 8'd0;
        debug_vsync_d       <= 1'b0;
    end else begin
        debug_pclk_counter <= debug_pclk_counter + 1'b1;
        debug_vsync_d      <= ov5640_vsync;
        if (ov5640_vsync && !debug_vsync_d)
            debug_vsync_counter <= debug_vsync_counter + 1'b1;
    end
end

// Driver and register sequence are taken from the board vendor's
// XC7Z035/34_5640_hdmi example.  Keep the control clock and CLK_FREQ at the
// vendor design's 50 MHz while diagnosing SCCB/chip-ID reads.
z100_ov5640_dri #(
    .CLK_FREQ (27'd50_000_000),
    .I2C_FREQ (18'd250_000)
) u_z100_ov5640_dri (
    .clk              (clk_sys24m),
    .rst_n            (s_rst_n),
    .cam_pclk         (ov5640_pclk),
    .cam_vsync        (ov5640_vsync),
    .cam_href         (ov5640_href),
    .cam_data         (ov5640_data),
    .cam_rst_n        (ov5640_rst_n),
    .cam_pwdn         (ov5640_pwdn),
    .cam_scl          (ov5640_iic_scl),
    .cam_sda          (ov5640_iic_sda),
    .cmos_h_pixel     (CMOS_H_PIXEL),
    .cmos_v_pixel     (CMOS_V_PIXEL),
    .total_h_pixel    (TOTAL_H_PIXEL),
    .total_v_pixel    (TOTAL_V_PIXEL),
    .capture_start    (cam_init_done),
    .cam_init_done    (cam_init_done),
    .cam_sensor_id    (cam_sensor_id),
    .cam_i2c_error    (cam_i2c_error),
    .cmos_frame_vsync (frame_vsync),
    .cmos_frame_href  (frame_href),
    .cmos_frame_valid (pixel_valid),
    .cmos_frame_data  (pixel_rgb565)
);

// Preserve the timing convention expected by Video In to AXI4-Stream:
// clock enable is asserted throughout blanking and once per RGB565 pixel
// during active video (the camera bus transfers one pixel in two byte clocks).
assign m_vsync = frame_vsync;
assign m_href  = frame_href;
assign m_hsync = ~frame_href;
assign vid_ce  = m_hsync | pixel_valid;

assign m_data = {
    pixel_rgb565[15:11], 3'b000,
    pixel_rgb565[10:5],  2'b00,
    pixel_rgb565[4:0],   3'b000
};

assign debug_status = {
    debug_pixel_snapshot[11:4], // 31:24, coherent raw frame count
    cam_sensor_id,       // 23:8, expected OV5640 ID is 16'h5640
    cam_i2c_error,       // 7: sticky SCCB NACK detected
    cam_init_done,       // 6: SCCB register sequence completed
    debug_pixel_snapshot[3], // 5: sampled raw sensor VSYNC
    debug_pixel_snapshot[2], // 4: sampled raw sensor HREF
    debug_pixel_snapshot[1], // 3: sampled pixel valid
    ov5640_rst_n,        // 2: sensor reset released
    ~ov5640_pwdn,        // 1: sensor powered
    debug_pixel_snapshot[0] // 0: sampled PCLK heartbeat
};

endmodule
