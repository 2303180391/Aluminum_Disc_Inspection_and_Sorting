`timescale 1ns/1ps
// Coherent observation-only bus transfer. Do not synchronize binary counters
// bit by bit. Hold a complete snapshot until the XPM handshake acknowledges it.
module camera_debug_snapshot #(parameter WIDTH = 32)(
    input wire src_clk,
    input wire dst_clk,
    input wire [WIDTH-1:0] src_data,
    output wire [WIDTH-1:0] dst_data
);
    reg [WIDTH-1:0] snapshot = 0;
    reg send = 0;
    wire received;
    always @(posedge src_clk) begin
        if (!send && !received) begin
            snapshot <= src_data;
            send <= 1'b1;
        end else if (received) begin
            send <= 1'b0;
        end
    end
    xpm_cdc_handshake #(
        .WIDTH(WIDTH), .DEST_EXT_HSK(0), .DEST_SYNC_FF(3),
        .SRC_SYNC_FF(3), .INIT_SYNC_FF(1), .SIM_ASSERT_CHK(1)
    ) u_snapshot (
        .src_clk(src_clk), .src_in(snapshot), .src_send(send),
        .src_rcv(received), .dest_clk(dst_clk), .dest_out(dst_data),
        .dest_req(), .dest_ack(1'b0)
    );
endmodule
