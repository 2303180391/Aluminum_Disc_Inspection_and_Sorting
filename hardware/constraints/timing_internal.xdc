# Divider clocks are real clocks used by the two vendor I2C state machines.
# The dividers toggle every floor((CLK_FREQ/I2C_FREQ)/4)/2 input cycles.
# OV5640: 50 MHz / 50; MS7210: 74.25 MHz / 74 (integer RTL division).
create_generated_clock -name camera_i2c_clk -divide_by 50 \
    -source [get_pins design_1_i/ov5640_top_0/inst/u_z100_ov5640_dri/u_i2c_dr/dri_clk_reg/C] \
    [get_pins design_1_i/ov5640_top_0/inst/u_z100_ov5640_dri/u_i2c_dr/dri_clk_reg/Q]
create_generated_clock -name hdmi_i2c_clk -divide_by 74 \
    -source [get_pins design_1_i/z100_hdmi_tx_0/inst/u_ms72xx_ctl/u_i2c_dri/dri_clk_reg/C] \
    [get_pins design_1_i/z100_hdmi_tx_0/inst/u_ms72xx_ctl/u_i2c_dri/dri_clk_reg/Q]
# CDC exceptions are supplied by the instantiated XPM synchronizers. No
# blanket false path is applied to the camera, YOLO datapath, or reset tree.
