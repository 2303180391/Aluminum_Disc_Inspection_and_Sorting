# Read-only, leaves CPU and PWM unchanged.
source [file join [file dirname [info script]] locator_common.tcl]
targets -set -filter {name =~ "ARM*#0"}
set loc_access [configparams force-mem-accesses]
set loc_rc [catch {
 configparams force-mem-accesses 1
 ::locator::gate
 set loc_m [::locator::read 0x00E80000 64]
 if {[lindex $loc_m 5]==0} {puts "LOCATOR_IDLE: no result yet"} elseif {[lindex $loc_m 5]==1} {
  puts "LOCATOR_BUSY: result not ready"
 } else {::locator::print_result $loc_m}
} loc_msg loc_opts]
configparams force-mem-accesses $loc_access
if {$loc_rc} {return -options $loc_opts $loc_msg}
