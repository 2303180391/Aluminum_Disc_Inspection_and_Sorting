# Explicit cold load. Requires a released manifest and PWM disabled beforehand.
set a5root [file dirname [file normalize [info script]]]
set a5cold [expr {[info exists ::arm5_cold_start] && $::arm5_cold_start eq "1"}]
unset -nocomplain ::arm5_cold_start
set ::arm5_load_stage FILE_VERIFY
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
source [file join $a5root arm5_common.tcl]
source [file join $a5root release_manifest.tcl]
source [file join $a5root build_rebuilt symbol_addresses.tcl]
foreach {a5file a5expected} $arm5_release_hashes {
 set a5path [file join $a5root $a5file]
 if {![file isfile $a5path]} {error "ARM5_MISSING_FILE: $a5path"}
 set a5hash [::arm5::sha256 $a5path]
 if {$a5hash ne $a5expected} {error "ARM5_HASH_MISMATCH: $a5file"}
}
catch {connect}
if {$a5cold} {
 # Explicit power-isolated reset: external servo power must be disconnected.
 # Entry wrappers document whether PWM wires are absent or already verified.
 # Do not touch old PL registers before programming the new hardware.
 set ::arm5_load_stage SYSTEM_RESET
 puts "ARM5_LOAD_STAGE $::arm5_load_stage; external servo power must be disconnected"
 targets -set -filter {name == "APU"}
 rst -system
 after 2000
} else {
set ::arm5_load_stage EXISTING_HARDWARE_CHECK
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
targets -set -filter {name =~ "ARM*#0"}
set a5access [configparams force-mem-accesses]
set a5rc [catch {
 configparams force-mem-accesses 1
 if {[catch {set id [lindex [::arm5::read 0x43C1006C] 0]} a5probe]} {
  error "ARM5_PREFLIGHT_UNREADABLE: $a5probe. Stop; use load_arm5_cold.tcl only after disconnecting J37 and external servo power."
 }
 if {$id!=0x41524D35 && $id!=0x41524D31} {error "ARM5_EXISTING_HARDWARE_UNKNOWN: use physical-isolation cold entry"}
 if {[lindex [::arm5::read 0x43C10068] 0]&0x17} {
  error "ARM5_LOAD_REFUSED: PWM/motion active. Support the arm and explicitly disable first."
 }
} a5msg a5opts]
configparams force-mem-accesses $a5access
if {$a5rc} {return -options $a5opts $a5msg}
}
set ::arm5_load_stage STOP_CORES
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
source [file join $a5root replay_stop.tcl]
catch {targets -set -filter {name =~ "ARM*#1"};replay_stop}
targets -set -filter {name =~ "ARM*#0"}
replay_stop
source [file join $a5root hardware ps7_init.tcl]
set ::arm5_load_stage PS_INIT
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
ps7_init
set ::arm5_load_stage PROGRAM_PL
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
targets -set -filter {name == "xc7z035"}
fpga [file join $a5root hardware design_1_wrapper_arm5.bit]
targets -set -filter {name =~ "ARM*#0"}
set ::arm5_load_stage POST_CONFIG
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
ps7_post_config
rst -processor
set ::arm5_load_stage NEW_HARDWARE_CHECK
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
set a5access [configparams force-mem-accesses]
set a5rc [catch {
 configparams force-mem-accesses 1
 if {[lindex [::arm5::read 0x43C1006C] 0]!=0x41524D35 ||
     [lindex [::arm5::read 0x43C1004C] 0]!=0x4D423402 ||
     [lindex [::arm5::read 0x43C0000C] 0]!=0x43535131 ||
     ([lindex [::arm5::read 0x43C10068] 0]&0x17)} {error "ARM5_HARDWARE_CHECK_FAILED"}
} a5msg a5opts]
configparams force-mem-accesses $a5access
if {$a5rc} {return -options $a5opts $a5msg}
set ::arm5_load_stage DOWNLOAD_ELF
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
dow [file join $a5root build_rebuilt yolo_metal_defect_3class.elf]
set a5access [configparams force-mem-accesses]
set a5rc [catch {
 configparams force-mem-accesses 1
 # Retained DDR signatures must not satisfy the fresh-boot readiness check.
 mwr 0x00E00000 0
 mwr 0x00E00900 0
 mwr 0x00E00B00 0
} a5msg a5opts]
configparams force-mem-accesses $a5access
if {$a5rc} {return -options $a5opts $a5msg}
set ::arm5_load_stage WAIT_FIRMWARE
puts "ARM5_LOAD_STAGE $::arm5_load_stage"
con
set a5access [configparams force-mem-accesses]
set a5rc [catch {
 configparams force-mem-accesses 1
 set deadline [expr {[clock milliseconds]+30000}]
 set ok 0
 while {[clock milliseconds]<$deadline} {
  set d [::arm5::read 0x00E00000 6]
  set m [::arm5::read 0x00E00900 14]
  if {[lindex $d 0]==0x59444731 && [lindex $d 2]==255} {error "ARM5_BOOT_FAILED diagnostic=[lindex $d 3]"}
  if {[lindex $m 0]==0x41354C31 && [lindex $m 12]==1 && [lindex $d 5]>1} {set ok 1;break}
  after 150
 }
 if {!$ok} {error "ARM5_BOOT_TIMEOUT; inspect status, do not enable"}
 if {[::arm5::read 0x00E800CC 2] ne {1 4122867821}} {error "ARM5_ORIGIN_REFERENCE_MISMATCH: expected red-frame reference F5BDF86D"}
 if {[lindex [::arm5::read 0x00E800DC 1] 0]!=0x4F434331} {error "ARM5_OCCLUSION_POLICY_MISMATCH: expected OCC1 software"}
 if {[lindex [::arm5::read 0x00E800E0 1] 0]!=0x52444631} {error "ARM5_RED_FRAME_POLICY_MISMATCH: expected RDF1 software"}
 if {[lindex [::arm5::read 0x00E800E4 1] 0]!=0x45444732} {error "ARM5_EDGE_POLICY_MISMATCH: expected EDG2 software"}
 if {[lindex [::arm5::read 0x00E08018 1] 0]!=0x4E564631} {error "ARM5_NORMAL_VOTE_FIX_MISMATCH: expected NVF1 software"}
 if {[::arm5::read $build_symbol(layers_config) 9] ne {0 8 16 1 416 23547 7 0 115}} {error "ARM5_MODEL_CONFIG_MISMATCH"}
 set expected {0xFFFFFD9F 0xFFFFFFE5 0x0000019F 0xFFFFFFE7 0xFFFFFF1A 0xFFFFFF21 0x00000069 0x00000010 0x00000212 0x00000645 0xFFFFFFE5 0x000007FB 0xFFFFFD7A 0xFFFFFE0C 0xFFFFFD82 0xFFFFFF18}
 set actual [::arm5::read 0x02000000 16]
 for {set i 0} {$i<16} {incr i} {
  if {[lindex $actual $i]!=[expr {[lindex $expected $i]+0}]} {error "ARM5_MODEL_PREFIX_MISMATCH"}
 }
 if {[lindex [::arm5::read 0x43C10068] 0]&0x17} {error "ARM5_UNEXPECTED_PWM_STATE"}
} a5msg a5opts]
configparams force-mem-accesses $a5access
if {$a5rc} {
 replay_stop
 puts "ARM5_BOOT_DIAGNOSTICS_BEGIN"
 if {[catch {source [file join $a5root read_status.tcl]} a5dumpmsg]} {
  puts "ARM5_BOOT_DIAGNOSTICS_UNAVAILABLE: $a5dumpmsg"
 }
 puts "ARM5_BOOT_DIAGNOSTICS_END"
 return -options $a5opts $a5msg
}
::arm5::status
puts "ARM5_FIXED_ORIGIN_READY reference=F5BDF86D O=bottom-left X=right Y=up inner_mm=100x100; white marker not required"
puts "ARM5_RED_FRAME_READY RDF1; direct matte-red inner-frame detection. Model and joint calibration unchanged."
puts "ARM5_EDGE_LOCATOR_READY EDG2; bounded image translation, current visible-edge refit and rim-outline recovery."
puts "ARM5_PARTIAL_OCCLUSION_READY OCC1; first locate with unobscured tape to calibrate. Re-teach/disable/reboot clears calibration. No motion requested."
set ::arm5_load_stage READY
puts "ARM5_NORMAL_VOTE_FIX_READY NVF1; five fresh clear frames classify normal; rejected edge/geometry proposals do not veto. No motion requested."
puts "ARM5_SORT_ANGLES_READY ANG20261029; M1/M2/M3/M4 normal=135/140/0/130 scratch=135/110/40/120 dent=150/140/0/130 stain=150/110/40/120; claw_closed=60 claw_open=37"
puts "ARM5_LOADED CPU0_RUNNING PWM_DISABLED; observation not taught. Prefix verified, not full parameter audit. No motion requested."
