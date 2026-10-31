# Physical-isolation recovery. Disconnect ALL five J37 PWM signal wires and
# external servo power before sourcing. Verified common GND may remain.
# Resets the system and replaces PL + ELF.
# No old PL registers are read. Fresh PL defaults PWM off.
set a5coldroot [file dirname [file normalize [info script]]]
puts "ARM5_COLD_ENTRY: All five PWM signal wires and external servo power must be disconnected. Verified common GND may remain. System reset will be requested."
set ::arm5_cold_start 1
set a5coldrc [catch {source [file join $a5coldroot load_arm5.tcl]} a5coldmsg a5coldopts]
unset -nocomplain ::arm5_cold_start
if {$a5coldrc} {
 set a5failedstage ENTRY
 if {[info exists ::arm5_load_stage]} {set a5failedstage $::arm5_load_stage}
 puts "ARM5_COLD_FAILED stage=$a5failedstage; stop here and send full output. No automatic retry."
 return -options $a5coldopts $a5coldmsg
}
