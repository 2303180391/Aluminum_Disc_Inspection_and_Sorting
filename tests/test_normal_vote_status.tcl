set root [file dirname [file dirname [file normalize [info script]]]]
set access 0
set old_firmware 0
set changed_record 0
proc configparams {name args} {
 if {[llength $args]} {set ::access [lindex $args 0]}
 return $::access
}
proc targets {args} {}
foreach forbidden {mwr con stop rst dow fpga} {
 proc $forbidden {args} {error "Unexpected hardware mutation in read-only status"}
}
proc mrd {flag addr count} {
 if {$addr==0x43C1006C} {return {0x41524D35}}
 if {$addr==0x00E00900} {return [lrange {0x41354C31 1 7 23 45 57 2300 11 3 3 1093834330 37 1 8 0 0} 0 [expr {$count-1}]]}
 if {$addr==0x00E08000} {return [list 0x4D545231 2 128 80 1 0 [expr {$::old_firmware?0:0x4E564631}] 0]}
 if {$addr==0x00E08010} {return {1}}
 if {$addr==0x00E08020} {
  if {$count==1} {return [list [expr {$::changed_record?2:1}]]}
  set v [lrepeat 80 0]
  foreach {i value} {0 1 1 2301 4 0 5 0 72 3 74 40 79 1} {lset v $i $value}
  return $v
 }
 error "Unexpected read $addr $count"
}
source [file join $root runtime read_normal_vote_status.tcl]
if {$access!=0} {error "Read-only status leaked access setting"}
set old_firmware 1
if {![catch {source [file join $root runtime read_normal_vote_status.tcl]} msg] || ![string match *FIX_NOT_LOADED* $msg]} {error "Old firmware accepted"}
if {$access!=0} {error "Failure leaked access setting"}
set old_firmware 0;set changed_record 1
if {![catch {source [file join $root runtime read_normal_vote_status.tcl]} msg] || ![string match *SNAPSHOT_CHANGED* $msg]} {error "Torn record accepted"}
if {$access!=0} {error "Torn-record failure leaked access setting"}
puts "NORMAL_VOTE_READ_ONLY_STATUS_PASS; old ELF and torn record rejected; CPU/PWM untouched"
