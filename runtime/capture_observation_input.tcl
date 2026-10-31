# ARM5/OCC1 exact localization input, while PWM holding torque stays enabled.
# This script ONLY arms diagnostic image saving; it sends no arm, locate,
# START, enable, disable, reset, stop or resume command. The user must click
# LOCATE ONLY in the PC app after the ARMED message. It may return the arm to
# the taught observation pose; it never requests grasping or sorting.
namespace eval ::arm5capture {
 variable root [file dirname [file normalize [info script]]]
 variable helper [file join $root locator_common.tcl]
 variable python {E:/competition_best/tools/python/python.exe}
 variable converter [file join $root convert_locator_input.py]
 variable active
 if {![info exists active]} {set active 0}
}

proc ::arm5capture::readarm {} {
 set a [::locator::read 0x00E00900 16]
 if {[lindex $a 0]!=0x41354C31 || [lindex $a 1]!=1} {error "ARM5_CAPTURE_WRONG_FIRMWARE"}
 return $a
}
proc ::arm5capture::holding {} {
 set hw [lindex [::locator::read 0x43C10068 1] 0]
 if {!($hw&1)} {error "ARM5_CAPTURE_PWM_DISABLED: enable/home in PC app first; do not disable to capture"}
 return $hw
}
proc ::arm5capture::main {} {
 variable root;variable helper;variable python;variable converter;variable active
 if {$active} {error "ARM5_CAPTURE_ALREADY_ACTIVE"}
 foreach f [list $helper $python $converter] {
  if {![file isfile $f]} {error "ARM5_CAPTURE_MISSING_FILE: $f"}
 }
 source $helper
 targets -set -filter {name =~ "ARM*#0"}
 set saved [configparams force-mem-accesses]
 set armed 0;set active 1
 set rc [catch {
  configparams force-mem-accesses 1
  ::locator::gate
  set m [::locator::read 0x00E80000 64]
  if {[lindex $m 55]!=0x4F434331} {error "ARM5_CAPTURE_REQUIRES_OCC1: no capture armed"}
  set a [readarm];set hw [holding]
  if {[lindex $a 2]!=0 || [lindex $a 3]!=0 || ($hw&0x16) || !($hw&8)} {
   error "ARM5_CAPTURE_NOT_IDLE_READY: inspect/clear the existing fault in PC app first"
  }
  if {([lindex $a 9]&3)!=3 || [lindex $a 12]!=1} {error "ARM5_CAPTURE_OBSERVATION_NOT_TAUGHT"}
  if {[lindex $m 4]!=0 || [lindex $m 5]==1 || [lindex $m 43]!=0} {error "ARM5_CAPTURE_DIAGNOSTIC_REQUEST_PENDING"}
  set previous [lindex $m 6]
  set output [file join $root captures]
  file mkdir $output
  mwr 0x00E800AC 1
  set armed 1
  puts "ARM5_CAPTURE_ARMED: PWM stays enabled. Now click ONLY LOCATE validation in the PC app within 90 seconds. Do not click START, TEACH or DISABLE, press PL_KEY1, or change the scene."
  puts "The PC LOCATE action may return to the taught observation pose, then measure once. This script sends no motion commands."
  set deadline [expr {[clock milliseconds]+90000}]
  set ready 0
  while {[clock milliseconds]<$deadline} {
   holding
   ::locator::gate
   set a [readarm]
   set phase [lindex $a 2]
   if {$phase==1 || ($phase>=4 && $phase<=6) || (($phase==2 || $phase==3) && [lindex $a 15]!=1)} {
    error "ARM5_CAPTURE_OTHER_ACTION: no image accepted; no action was interrupted"
   }
   set m [::locator::read 0x00E80000 64]
   if {[lindex $m 6]!=$previous && [lindex $m 5]>=2} {
    # arm5_poll publishes its phase/mode after disc_board_poll returns.
    if {[lindex $a 13]!=[lindex $m 7]} {after 50;continue}
    if {[lindex $a 15]!=1 || ($phase!=0 && $phase!=7)} {error "ARM5_CAPTURE_NOT_LOCATE_ONLY_RESULT"}
    set token [lindex $m 7]
    if {[lindex $m 5]!=2 || !$token || [lindex $m 47]!=$token || [lindex $m 63]!=$token ||
        [lindex $m 44]!=0x08400000 || [lindex $m 45]!=2764800} {error "ARM5_CAPTURE_SNAPSHOT_INVALID: inspect transport/diagnostics"}
    set ready 1;break
   }
   after 100
  }
  if {!$ready} {error "ARM5_CAPTURE_TIMEOUT: no completed LOCATE-only snapshot; no command was sent or retried"}
  set stamp [format {%s_%03d} [clock format [clock seconds] -format {%Y%m%d_%H%M%S}] [expr {[clock milliseconds]%1000}]]
  set prefix [file join $output observation_$stamp]
  set raw ${prefix}.bin;set verify ${prefix}.verify.bin;set mail ${prefix}.mail.txt
  foreach f [list $raw $verify $mail ${prefix}.png ${prefix}.json] {
   if {[file exists $f]} {error "ARM5_CAPTURE_OUTPUT_EXISTS: $f"}
  }
  puts "ARM5_CAPTURE_READING: keep pose and scene unchanged; do not issue another command."
  mrd -bin -file $raw 0x08400000 691200
  holding
  mrd -bin -file $verify 0x08400000 691200
  holding
  set final [::locator::read 0x00E80000 64]
  set lastarm [readarm]
  if {$final ne $m || [lindex $lastarm 13]!=$token || [lindex $lastarm 15]!=1 ||
      ([lindex $lastarm 2]!=0 && [lindex $lastarm 2]!=7)} {error "ARM5_CAPTURE_RESULT_CHANGED: no PNG accepted"}
  set fd [open $mail {WRONLY CREAT EXCL}]
  fconfigure $fd -encoding utf-8
  puts $fd [join $m \n]
  close $fd
  ::locator::print_result $m
 } msg opts]
 if {$armed} {catch {mwr 0x00E800AC 0}}
 catch {configparams force-mem-accesses $saved}
 set active 0
 if {$rc} {return -options $opts $msg}
 puts [exec $python -B -I $converter $raw $verify $mail $prefix]
 puts "ARM5_OBSERVATION_INPUT_SAVED ${prefix}.png; exact LOCATE-only input/result saved. CPU/PWM unchanged by this script; no sorting requested."
}
::arm5capture::main
