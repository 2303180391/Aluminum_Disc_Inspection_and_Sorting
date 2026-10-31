# After verified J37 wiring, with EXTERNAL SERVO POWER PHYSICALLY DISCONNECTED.
# Five PWM wires and verified common GND may stay connected. Support the arm.
# Resets PS/PL, loads the same checked bit/ELF, then verifies PWM remains off.
# Never use while the external servo supply is powered; no auto-enable/start.
set a5wiredroot [file dirname [file normalize [info script]]]
puts "ARM5_WIRED_POWEROFF_ENTRY: PWM/GND may remain connected. Physically disconnect external servo power and support the arm before this system reset."
set ::arm5_cold_start 1
set a5wiredrc [catch {source [file join $a5wiredroot load_arm5.tcl]} a5wiredmsg a5wiredopts]
unset -nocomplain ::arm5_cold_start
if {$a5wiredrc} {
 set a5failedstage ENTRY
 if {[info exists ::arm5_load_stage]} {set a5failedstage $::arm5_load_stage}
 puts "ARM5_WIRED_LOAD_FAILED stage=$a5failedstage; keep servo power disconnected and send full output. No automatic retry."
 return -options $a5wiredopts $a5wiredmsg
}
