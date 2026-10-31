`timescale 1ns/1ps

// Transparent AXI4-Stream tap for the camera-to-VDMA path. Stream signals
// pass through unchanged; counters and sticky flags are observation only.
module camera_axis_tap_timing (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME aclk, ASSOCIATED_BUSIF S_AXIS:M_AXIS, ASSOCIATED_RESET aresetn, FREQ_HZ 200000000" *)
    input wire aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 debug_clk CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME debug_clk, FREQ_HZ 50000000" *)
    input wire debug_clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME aresetn, POLARITY ACTIVE_LOW" *)
    input wire aresetn,
    input wire vid_overflow,
    input wire vid_underflow,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME S_AXIS, TDATA_NUM_BYTES 3, TUSER_WIDTH 1, HAS_TLAST 1, HAS_TREADY 1" *)
    input wire [23:0] s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TVALID" *) input wire s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TREADY" *) output wire s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TUSER" *) input wire s_axis_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TLAST" *) input wire s_axis_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME M_AXIS, TDATA_NUM_BYTES 3, TUSER_WIDTH 1, HAS_TLAST 1, HAS_TREADY 1" *)
    output wire [23:0] m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TVALID" *) output wire m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TREADY" *) input wire m_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TUSER" *) output wire m_axis_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TLAST" *) output wire m_axis_tlast,
    output wire [31:0] debug_status
);

assign m_axis_tdata = s_axis_tdata;
assign m_axis_tvalid = s_axis_tvalid;
assign m_axis_tuser = s_axis_tuser;
assign m_axis_tlast = s_axis_tlast;
assign s_axis_tready = m_axis_tready;

reg [7:0] frame_count, line_count, beat_count;
reg overflow_seen, underflow_seen, valid_seen, ready_seen, sof_seen, eol_seen;
wire [1:0] video_errors_sync;
// Overflow can originate in the video clock domain. These independent flags
// are synchronized before being accumulated; they are not a multi-bit counter.
xpm_cdc_array_single #(.WIDTH(2), .DEST_SYNC_FF(3),
    .SRC_INPUT_REG(0), .INIT_SYNC_FF(1)) u_video_error_sync (
    .src_clk(1'b0), .src_in({vid_underflow, vid_overflow}),
    .dest_clk(aclk), .dest_out(video_errors_sync)
);

always @(posedge aclk) begin
    if (!aresetn) begin
        frame_count <= 0; line_count <= 0; beat_count <= 0;
        overflow_seen <= 0; underflow_seen <= 0;
        valid_seen <= 0; ready_seen <= 0; sof_seen <= 0; eol_seen <= 0;
    end else begin
        if (video_errors_sync[0]) overflow_seen <= 1'b1;
        if (video_errors_sync[1]) underflow_seen <= 1'b1;
        if (s_axis_tvalid) valid_seen <= 1'b1;
        if (m_axis_tready) ready_seen <= 1'b1;
        if (s_axis_tvalid && m_axis_tready) begin
            beat_count <= beat_count + 1'b1;
            if (s_axis_tuser) begin
                frame_count <= frame_count + 1'b1;
                line_count <= 0;
                sof_seen <= 1'b1;
            end
            if (s_axis_tlast) begin
                line_count <= line_count + 1'b1;
                eol_seen <= 1'b1;
            end
        end
    end
end

wire [31:0] axis_status = {frame_count, line_count, beat_count,
    underflow_seen, overflow_seen, valid_seen, ready_seen,
    sof_seen, eol_seen, s_axis_tvalid, m_axis_tready};

camera_debug_snapshot #(.WIDTH(32)) u_axis_debug_snapshot (
    .src_clk(aclk), .dst_clk(debug_clk),
    .src_data(axis_status), .dst_data(debug_status)
);

endmodule
