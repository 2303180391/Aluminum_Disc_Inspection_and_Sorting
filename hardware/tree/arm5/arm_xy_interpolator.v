`timescale 1ns/1ps
// 矩形四点双线性插值，仅估算角度，不代表逆运动学或实际位置反馈。
// 上层保证：1<=width,height<=1000，dx<=width，dy<=height，角度0..180。
// 四角顺序：0=(xmin,ymin),1=(xmax,ymin),2=(xmin,ymax),3=(xmax,ymax)。
// enable期间输入必须稳定；done保持到enable撤销，状态查询不会丢失结果。
// 撤销enable立即取消计算。无浮点、无运行时除法运算符，适用于综合。
module arm_xy_interpolator (
    input wire clk, rst_n, enable,
    input wire [9:0] width, height, dx, dy,
    input wire [31:0] corners0, corners2, // {角3,角2,角1,角0}
    output reg done,
    output reg [7:0] result0, result2
);
    localparam [2:0] WEIGHTS=0, SUM=1, DIV_INIT=2, DIVIDE=3, HOLD=4;
    reg [2:0] state;
    reg [1:0] index;
    reg lane;
    reg [19:0] weight [0:3];
    reg [19:0] denominator;
    reg [27:0] accumulator, dividend, quotient;
    reg [20:0] remainder;
    reg [4:0] bit_count;
    wire [9:0] wx = index[0] ? dx : width-dx;
    wire [9:0] wy = index[1] ? dy : height-dy;
    wire [31:0] selected_corners = lane ? corners2 : corners0;
    wire [7:0] selected_angle = selected_corners[index*8 +: 8];
    wire [27:0] product = selected_angle * weight[index];
    wire [20:0] shifted_remainder = {remainder[19:0],dividend[27]};
    wire subtract = shifted_remainder >= {1'b0,denominator};
    wire [20:0] next_remainder = subtract ?
        shifted_remainder-{1'b0,denominator} : shifted_remainder;
    wire [27:0] next_quotient = {quotient[26:0],subtract};
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state<=WEIGHTS; index<=0; lane<=0; done<=0;
            result0<=0; result2<=0; denominator<=0;
            accumulator<=0; dividend<=0; quotient<=0;
            remainder<=0; bit_count<=0;
        end else if (!enable) begin
            state<=WEIGHTS; index<=0; lane<=0; done<=0;
            accumulator<=0;
        end else begin
            case (state)
                WEIGHTS: begin
                    weight[index]<=wx*wy;
                    denominator<=width*height;
                    if (index==3) begin index<=0; state<=SUM; end
                    else index<=index+1'b1;
                end
                SUM: begin
                    // sum(angle[i]*weight[i])<=180*1,000,000，28位足够。
                    accumulator<=accumulator+product;
                    if (index==3) begin index<=0; state<=DIV_INIT; end
                    else index<=index+1'b1;
                end
                DIV_INIT: begin
                    // 只在最终结果四舍五入一次，避免逐轴舍入积累误差。
                    dividend<=accumulator+{9'd0,denominator[19:1]};
                    quotient<=0; remainder<=0; bit_count<=0; state<=DIVIDE;
                end
                DIVIDE: begin
                    // 每拍处理一位的无符号恢复除法，共28拍。
                    dividend<={dividend[26:0],1'b0};
                    remainder<=next_remainder; quotient<=next_quotient;
                    if (bit_count==27) begin
                        if (!lane) begin
                            result0<=next_quotient[7:0]; lane<=1;
                            accumulator<=0; state<=SUM;
                        end else begin
                            result2<=next_quotient[7:0]; done<=1; state<=HOLD;
                        end
                    end else bit_count<=bit_count+1'b1;
                end
                HOLD: ;
                default: state<=WEIGHTS;
            endcase
        end
    end
endmodule
