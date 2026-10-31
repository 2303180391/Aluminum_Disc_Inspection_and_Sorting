# Source-only helper. No download, stop, resume or motion command here.
namespace eval ::locator {
 variable root [file dirname [file normalize [info script]]]
 variable base 0x00E80000
}
proc ::locator::number {s} {
 if {[string match -nocase 0x* $s]} {scan $s %x value;return $value}
 if {[regexp {^[0-9A-Fa-f]{8}$} $s] && [regexp {[A-Fa-f]} $s]} {scan $s %x value;return $value}
 return [expr {wide($s)&0xFFFFFFFF}]
}
proc ::locator::read {address count} {
 set result {};foreach value [mrd -value $address $count] {lappend result [number $value]}
 if {[llength $result]!=$count} {error "LOCATOR_SHORT_MEMORY_READ"}
 return $result
}
proc ::locator::gate {} {
 variable base
 set m [read $base 4]
 if {[lindex $m 0]!=0x444C4332 || [lindex $m 1]!=3 || [lindex $m 2]!=0x20261023 || [lindex $m 3]!=64} {
  error "LOCATOR_FIRMWARE_ABSENT: use load_arm5.tcl first"
 }
 set d [read 0x00E00000 4]
 if {[lindex $d 0]!=0x59444731 || [lindex $d 2]==255 || [lindex $d 3]!=0} {
  error "LOCATOR_DIAGNOSTIC_FAULT: inspect board; no request sent"
 }
 if {[lindex [read 0x43C0000C 1] 0]!=0x43535131} {error "LOCATOR_WRONG_HARDWARE"}
 if {[lindex [read 0x43C1006C 1] 0]!=0x41524D35} {error "LOCATOR_REQUIRES_ARM5"}
 if {[lindex [read 0x00E800E0 1] 0]!=0x52444631 || [lindex [read 0x00E800D0 1] 0]!=0xF5BDF86D} {
  error "LOCATOR_RED_FRAME_FIRMWARE_MISMATCH: load this red-frame package first"
 }
 if {[lindex [read 0x00E800E4 1] 0]!=0x45444732} {
  error "LOCATOR_EDGE_POLICY_MISMATCH: load this EDG2 package first"
 }
}
proc ::locator::print_result {m} {
 set status [lindex $m 21]
 set names {OK NO_FRAME MULTIPLE_FRAMES NO_ORIGIN NO_DISC MULTIPLE_DISCS BAD_GEOMETRY REFERENCE_MISMATCH}
 puts "LOCATOR_RESULT transport=[lindex $m 5] sequence=[lindex $m 6] geometry=[lindex $names $status] frame_valid=[lindex $m 22] origin_valid=[lindex $m 23] disc_valid=[lindex $m 16]"
 puts "LOCATOR_TIME capture_fence_us=[lindex $m 8] compute_us=[lindex $m 9] total_us=[lindex $m 10]"
 if {[lindex $m 55]==0x4F434331} {
  puts "LOCATOR_CALIBRATION ready=[lindex $m 53] used_partial_occlusion=[lindex $m 54]; calibration clears after reboot, re-teach or PWM disable"
 }
 if {[lindex $m 56]==0x52444631} {puts "LOCATOR_FRAME_MODE RED_TAPE RDF1; green table, 100x100 mm inner edges"}
 if {[lindex $m 57]==0x45444732} {puts "LOCATOR_EDGE_MODE EDG2; same-frame calibration plus padded border search"}
 if {[lindex $m 57]==0x45444732} {puts "LOCATOR_CURRENT_EDGE_REFIT used=[lindex $m 58]; coordinates use current visible-edge geometry"}
 if {[lindex $m 57]==0x45444732} {puts "LOCATOR_OUTLINE_RECOVERY used=[lindex $m 59]; bounded concavity recovery from measured outer arc"}
 if {[lindex $m 51]==1} {
  puts [format "LOCATOR_ORIGIN mode=FIXED_REFERENCE reference_tag=0x%08X; O=bottom-left X=right Y=up in the reference native view; white marker not required" [lindex $m 52]]
 } else {puts "LOCATOR_ORIGIN mode=WHITE_MARKER"}
 if {[lindex $m 5]!=2} {puts "LOCATOR_REJECTED_OR_CAPTURE_FAILED: coordinates must not be used";return}
 if {[lindex $m 23]} {
  for {set i 0} {$i<4} {incr i} {
   puts "CORNER [lindex {O X XY Y} $i] pixel=[expr {round([lindex $m [expr {29+2*$i}]]/1000.0)}],[expr {round([lindex $m [expr {30+2*$i}]]/1000.0)}]"
  }
 }
 if {[lindex $m 16]} {
  puts "DISC pixel_x=[expr {round([lindex $m 18]/1000.0)}] pixel_y=[expr {round([lindex $m 19]/1000.0)}]"
  puts "DISC X_mm=[expr {round([lindex $m 25]/1000.0)}] Y_mm=[expr {round([lindex $m 26]/1000.0)}] mm_valid=[lindex $m 24]"
  if {![lindex $m 24]} {puts "Dimensions assumed 100x100 mm inner edges; millimetres are provisional until measured."}
 }
 puts "CPU/PWM unchanged by this script; no sorting requested."
}
