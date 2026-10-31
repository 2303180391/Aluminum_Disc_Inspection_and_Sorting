`timescale 1ns/1ps
// Independent 50 Hz PWM outputs; angle registers are supplied by arm5_controller.
module servo_pwm_5ch #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer PWM_PERIOD_US = 20_000,
    parameter integer MIN_PULSE_US = 500,
    parameter integer MAX_PULSE_US = 2_500
) (
    input wire clk_27m, rst_n,
    input wire [7:0] angle_ch0,angle_ch1,angle_ch2,angle_ch3,angle_ch4,
    output wire pwm_ch0,pwm_ch1,pwm_ch2,pwm_ch3,pwm_ch4
);
    servo_pwm #(.CLK_FREQ_HZ(CLK_FREQ_HZ),.PWM_PERIOD_US(PWM_PERIOD_US),
        .MIN_PULSE_US(MIN_PULSE_US),.MAX_PULSE_US(MAX_PULSE_US),
        .RESET_ANGLE_DEG(90)) s0
        (.clk(clk_27m),.rst_n(rst_n),.angle_deg(angle_ch0),.pwm_out(pwm_ch0));
    servo_pwm #(.CLK_FREQ_HZ(CLK_FREQ_HZ),.PWM_PERIOD_US(PWM_PERIOD_US),
        .MIN_PULSE_US(MIN_PULSE_US),.MAX_PULSE_US(MAX_PULSE_US),
        .RESET_ANGLE_DEG(150)) s1
        (.clk(clk_27m),.rst_n(rst_n),.angle_deg(angle_ch1),.pwm_out(pwm_ch1));
    servo_pwm #(.CLK_FREQ_HZ(CLK_FREQ_HZ),.PWM_PERIOD_US(PWM_PERIOD_US),
        .MIN_PULSE_US(MIN_PULSE_US),.MAX_PULSE_US(MAX_PULSE_US),
        .RESET_ANGLE_DEG(50)) s2
        (.clk(clk_27m),.rst_n(rst_n),.angle_deg(angle_ch2),.pwm_out(pwm_ch2));
    servo_pwm #(.CLK_FREQ_HZ(CLK_FREQ_HZ),.PWM_PERIOD_US(PWM_PERIOD_US),
        .MIN_PULSE_US(MIN_PULSE_US),.MAX_PULSE_US(MAX_PULSE_US),
        .RESET_ANGLE_DEG(65)) s3
        (.clk(clk_27m),.rst_n(rst_n),.angle_deg(angle_ch3),.pwm_out(pwm_ch3));
    servo_pwm #(.CLK_FREQ_HZ(CLK_FREQ_HZ),.PWM_PERIOD_US(PWM_PERIOD_US),
        .MIN_PULSE_US(MIN_PULSE_US),.MAX_PULSE_US(MAX_PULSE_US),
        .RESET_ANGLE_DEG(37)) s4
        (.clk(clk_27m),.rst_n(rst_n),.angle_deg(angle_ch4),.pwm_out(pwm_ch4));
endmodule
