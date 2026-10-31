# Current held-pose 720p view while ARM5 remains locked at localization error.
# NOT the original failed algorithm input. No CPU stop/resume, reset, PWM or
# arm command is issued. Temporarily redirects only VDMA0, restores in finally.
namespace eval ::arm5faultview {
 variable root [file dirname [file normalize [info script]]]
 variable helper [file join $root locator_common.tcl]
 variable python {E:/competition_best/tools/python/python.exe}
 variable converter [file join $root convert_fault_view.py]
 variable active
 if {![info exists active]} {set active 0}
 variable expected
 variable pose
 variable claw
}
proc ::arm5faultview::video {destination} {
 set v [::locator::read 0x43000030 2]
 if {([lindex $v 0]&0x00ff003f)!=0x00010001 || ([lindex $v 1]&0x8ff1)} {
  error "FAULT_VIEW_VIDEO_NOT_HEALTHY; no image accepted"
 }
 if {[::locator::read 0x430000A0 4] ne [list 720 3840 3840 $destination]} {
  error "FAULT_VIEW_VIDEO_LAYOUT_CHANGED; no image accepted"
 }
}
proc ::arm5faultview::guard {} {
 variable expected;variable pose;variable claw
 ::locator::gate
 set a [::locator::read 0x00E00900 16]
 set h [lindex [::locator::read 0x43C10068 1] 0]
 if {[lindex $a 0]!=0x41354C31 || [lindex $a 1]!=1 ||
     [lindex $a 2]!=7 || [lindex $a 3]!=14 || ($h&0x1F)!=0x0B ||
     [lindex $a 10]!=$pose || [lindex $a 11]!=$claw ||
     [lindex [::locator::read 0x00E00980 1] 0]!=0} {
  error "FAULT_VIEW_REQUIRES_HELD_LOCATE_ERROR: keep the existing error locked, PWM enabled; do not RETRY/TEACH/START/DISABLE"
 }
 if {[::locator::read 0x00E80000 64] ne $expected} {
  error "FAULT_VIEW_LOCATOR_CHANGED; no image accepted"
 }
}
proc ::arm5faultview::destination {address} {
 mwr 0x430000AC $address
 mwr 0x430000A0 720
}
proc ::arm5faultview::fence {address} {
 set count 0;set deadline [expr {[clock milliseconds]+3000}]
 mwr 0x43000034 0x1000
 while {$count<3} {
  guard
  video $address
  set sr [lindex [::locator::read 0x43000034 1] 0]
  if {$sr&0x1000} {mwr 0x43000034 0x1000;incr count}
  if {[clock milliseconds]>$deadline} {error "FAULT_VIEW_FRAME_TIMEOUT"}
  if {$count<3} {after 10}
 }
 after 20
 guard
 video $address
}
proc ::arm5faultview::main {} {
 variable root;variable helper;variable python;variable converter
 variable active;variable expected;variable pose;variable claw
 if {$active} {error "FAULT_VIEW_ALREADY_ACTIVE"}
 foreach path [list $helper $python $converter] {
  if {![file isfile $path]} {error "FAULT_VIEW_MISSING_FILE: $path"}
 }
 source $helper
 targets -set -filter {name =~ "ARM*#0"}
 set saved [configparams force-mem-accesses]
 set active 1;set changed 0
 set rc [catch {
  configparams force-mem-accesses 1
  ::locator::gate
  set expected [::locator::read 0x00E80000 64]
  set a [::locator::read 0x00E00900 16]
  set pose [lindex $a 10];set claw [lindex $a 11]
  if {[lindex $expected 4]!=0 || [lindex $expected 5]!=2 ||
      [lindex $expected 43]!=0 || [lindex $expected 7]!=[lindex $a 13]} {
   error "FAULT_VIEW_REQUIRES_COMPLETED_CLEAN_CAPTURE_ERROR; no image requested"
  }
  guard
  video 67108864
  set out [file join $root captures]
  file mkdir $out
  set stamp [format {%s_%03d} [clock format [clock seconds] -format {%Y%m%d_%H%M%S}] [expr {[clock milliseconds]%1000}]]
  set prefix [file join $out fault_view_$stamp]
  foreach suffix {.bin .verify.bin .mail.txt .png .json} {
   if {[file exists ${prefix}$suffix]} {error "FAULT_VIEW_OUTPUT_EXISTS"}
  }
  puts "FAULT_VIEW_CAPTURING: keep the existing localization error locked and scene unchanged. No PC button or PL_KEY1; CPU and PWM remain running."
  set changed 1
  destination 134217728
  fence 134217728
  mrd -bin -file ${prefix}.bin 0x04000000 691200
  guard
  video 134217728
  mrd -bin -file ${prefix}.verify.bin 0x04000000 691200
  guard
  video 134217728
  set fd [open ${prefix}.mail.txt {WRONLY CREAT EXCL}]
  fconfigure $fd -encoding utf-8
  puts $fd [join $expected \n]
  close $fd
 } msg opts]
 # Restore the 720p write target even on read/timeout/state-check failure.
 set restore_rc 0;set restore_msg ""
 if {$changed} {
  set restore_rc [catch {
   set target [lindex [::locator::read 0x430000AC 1] 0]
   if {$target!=134217728 && $target!=67108864} {error "FAULT_VIEW_UNEXPECTED_TARGET; restore refused"}
   destination 67108864
   # A changed arm state must not prevent restoring the video write target.
   video 67108864
   set deadline [expr {[clock milliseconds]+3000}]
   set count 0;mwr 0x43000034 0x1000
   while {$count<3} {
    video 67108864
    if {[lindex [::locator::read 0x43000034 1] 0]&0x1000} {
     mwr 0x43000034 0x1000;incr count
    }
    if {[clock milliseconds]>$deadline} {error "FAULT_VIEW_RESTORE_TIMEOUT"}
    if {$count<3} {after 10}
   }
   after 20
   video 67108864
  } restore_msg]
 }
 catch {configparams force-mem-accesses $saved}
 set active 0
 if {$restore_rc} {error "FAULT_VIEW_VIDEO_RESTORE_FAILED: $restore_msg; original=$msg. No PNG accepted; send this output."}
 if {$rc} {return -options $opts $msg}
 puts [exec $python -B -I $converter ${prefix}.bin ${prefix}.verify.bin ${prefix}.mail.txt $prefix]
 puts "ARM5_FAULT_VIEW_SAVED ${prefix}.png; CURRENT held-pose view, not the original failed input. Video target restored; CPU/PWM/error lock unchanged."
}
::arm5faultview::main
