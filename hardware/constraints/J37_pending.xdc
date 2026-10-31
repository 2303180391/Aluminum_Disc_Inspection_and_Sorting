# User measured VCC_B10=3.3V on 2026-10-15.
# Do not generate/program a release by suppressing UCIO/NSTD checks.
set_property PACKAGE_PIN AD13 [get_ports {arm_pwm[0]}]
set_property PACKAGE_PIN AD14 [get_ports {arm_pwm[1]}]
set_property PACKAGE_PIN AC16 [get_ports {arm_pwm[2]}]
set_property PACKAGE_PIN AC17 [get_ports {arm_pwm[3]}]
set_property PACKAGE_PIN AB16 [get_ports {arm_pwm[4]}]
set_property PACKAGE_PIN H13 [get_ports arm_key_n]
set_property IOSTANDARD LVCMOS18 [get_ports arm_key_n]
set_property PULLUP true [get_ports arm_key_n]
# PWM IOSTANDARD must match measured Bank 10 supply.
set_property IOSTANDARD LVCMOS33 [get_ports {arm_pwm[*]}]
set_property SLEW SLOW [get_ports {arm_pwm[*]}]
set_property DRIVE 4 [get_ports {arm_pwm[*]}]
