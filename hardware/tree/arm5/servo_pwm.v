`timescale 1ns/1ps

// Parameterized single-channel servo PWM generator.
// The counter runs at CLK_FREQ_HZ and repeats every PWM_PERIOD_US.
// angle_deg is clamped to 0..180, then linearly mapped to MIN_PULSE_US..MAX_PULSE_US.
module servo_pwm #(
    parameter integer CLK_FREQ_HZ   = 27_000_000,
    parameter integer PWM_PERIOD_US = 20_000,
    parameter integer MIN_PULSE_US  = 500,
    parameter integer MAX_PULSE_US  = 2_500,
    parameter integer RESET_ANGLE_DEG = 0
) (
    input  wire       clk,
    input  wire       rst_n,
    input  wire [7:0] angle_deg,
    output reg        pwm_out
);

    localparam integer PERIOD_TICKS = (64'd1 * CLK_FREQ_HZ * PWM_PERIOD_US + 500000) / 1000000;
    localparam integer MIN_TICKS    = (64'd1 * CLK_FREQ_HZ * MIN_PULSE_US + 500000) / 1000000;
    localparam integer MAX_TICKS    = (64'd1 * CLK_FREQ_HZ * MAX_PULSE_US + 500000) / 1000000;
    localparam integer COUNTER_W    = (PERIOD_TICKS <= 2) ? 1 : $clog2(PERIOD_TICKS);
    localparam integer RESET_TICKS  = MIN_TICKS +
        ((RESET_ANGLE_DEG * (MAX_TICKS - MIN_TICKS) + 90) / 180);
    localparam integer TICKS_PER_DEG = (MAX_TICKS - MIN_TICKS) / 180;
    localparam integer TICKS_REMAINDER = (MAX_TICKS - MIN_TICKS) % 180;

    reg [COUNTER_W-1:0] period_count;
    reg [COUNTER_W-1:0] pulse_ticks;
    reg primed;
    integer angle_used;
    integer next_pulse_ticks;

    always @* begin
        angle_used = (angle_deg > 8'd180) ? 180 : angle_deg;
        // For the default parameters TICKS_REMAINDER=0 and each degree is 300 clocks.
        next_pulse_ticks = MIN_TICKS + angle_used * TICKS_PER_DEG +
            ((angle_used * TICKS_REMAINDER + 90) / 180);
    end

    // Compute pulse width at each period boundary. Latching avoids mid-pulse jitter.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            period_count <= {COUNTER_W{1'b0}};
            pulse_ticks  <= RESET_TICKS;
            pwm_out      <= 1'b0;
            primed       <= 1'b0;
        end else if(!primed) begin
            pulse_ticks <= next_pulse_ticks[COUNTER_W-1:0];
            period_count <= 0;pwm_out <= 0;primed <= 1;
        end else begin
            if (period_count == PERIOD_TICKS - 1) begin
                period_count <= {COUNTER_W{1'b0}};
                pulse_ticks <= next_pulse_ticks[COUNTER_W-1:0];
            end else begin
                period_count <= period_count + 1'b1;
            end

            pwm_out <= (period_count < pulse_ticks);
        end
    end

endmodule
