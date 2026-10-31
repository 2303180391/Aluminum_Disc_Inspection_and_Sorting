set root [file dirname [file dirname [file normalize [info script]]]]
set access 0
set downloaded 0
set resumed 0
set stopped 0
set pwm 0
set bad_model 0
set bad_reference 0
set bad_occlusion 0
set bad_red 0
set bad_edge 0
set bad_normal_vote 0
set pl_ready 1
set ap_failure 0
set reset_fail 0
set system_resets 0
set programmed 0
set cleared 0
set selected ""
set capture_error 0
set vdma_reads 0
proc configparams {name args} {
 global access
 if {[llength $args]} {set access [lindex $args 0]}
 return $access
}
proc connect {} {}
proc targets {args} {set ::selected $args}
proc rst {args} {
 if {$args eq "-system"} {
  if {![string match *APU* $::selected]} {error "System reset must select APU"}
  if {$::reset_fail} {error "APB AP transaction error, DAP status 0xF0000021"}
  incr ::system_resets
  set ::ap_failure 0
  set ::pl_ready 0
 }
}
proc fpga {path} {incr ::programmed;set ::pl_ready 1;set ::pwm 0}
proc dow {path} {incr ::downloaded}
proc con {} {incr ::resumed}
proc mwr {address value} {
 if {$address ni {0x00E00000 0x00E00900 0x00E00B00} || $value!=0} {error "Unexpected mailbox write"}
 if {!$::pl_ready || !$::downloaded} {error "Premature mailbox write"}
 incr ::cleared
}
proc mrd {flag address count} {
 global pwm bad_model build_symbol
 if {$::ap_failure} {error "APB AP transaction error, DAP status 0xF0000021"}
 if {$address>=0x43C00000 && $address<0x43C20000 && !$::pl_ready} {error "Read of unconfigured PL"}
 if {$address==0x43C1006C} {return {0x41524D35}}
 if {$address==0x43C10068} {return [list $pwm]}
 if {$address==0x43C1004C} {return {0x4D423402}}
 if {$address==0x43C0000C} {return {0x43535131}}
 if {$address==0x00E00000} {
  set v [lrepeat 64 0]
  foreach {i value} {0 0x59444731 1 2 2 4 5 2 30 4} {lset v $i $value}
  if {$::capture_error} {lset v 2 255;lset v 3 7;lset v 5 0}
  return [lrange $v 0 [expr {$count-1}]]
 }
 if {$address==0x00E00400} {return {0x43415031 1 0x01400000 0 1 0x00010000 0x00010001 0 0 0 0 0 0}}
 if {$address==0x00E08000} {return {0x4D545231 2 0 0 0 0 0 0}}
 if {$address==0x00E08018} {return [list [expr {$::bad_normal_vote?0:0x4E564631}]]}
 if {$address==0x00E00B00} {return [lrepeat 16 0]}
 if {$address==0x00E800CC} {return [list [expr {$::bad_reference?0:1}] 0xF5BDF86D]}
 if {$address==0x00E800DC} {return [list [expr {$::bad_occlusion?0:0x4F434331}]]}
 if {$address==0x00E800E0} {return [list [expr {$::bad_red?0:0x52444631}]]}
 if {$address==0x00E800E4} {return [list [expr {$::bad_edge?0:0x45444732}]]}
 if {$address==0x43000030 || $address==0x43010030} {incr ::vdma_reads;return {0x00010001 0x00010000}}
 if {$address==0x430000A0} {incr ::vdma_reads;return {720 3840 3840 0x04000000}}
 if {$address==0x430100A0} {incr ::vdma_reads;return {416 3328 3328 0x01400000}}
 if {$address==0x00E00900} {return [lrange {0x41354C31 1 0 0 0 0 0 0 0 0 1093834330 37 1 0 0 0} 0 [expr {$count-1}]]}
 if {$address==$build_symbol(layers_config)} {
  if {$bad_model} {return {0 8 16 1 416 1 7 0 115}}
  return {0 8 16 1 416 23547 7 0 115}
 }
 if {$address==0x02000000} {
  return {0xFFFFFD9F 0xFFFFFFE5 0x0000019F 0xFFFFFFE7 0xFFFFFF1A 0xFFFFFF21 0x00000069 0x00000010 0x00000212 0x00000645 0xFFFFFFE5 0x000007FB 0xFFFFFD7A 0xFFFFFE0C 0xFFFFFD82 0xFFFFFF18}
 }
 error "Unexpected memory access: $address"
}
rename source real_source
proc source {path} {
 switch [file tail $path] {
  symbol_addresses.tcl {uplevel 1 {set build_symbol(layers_config) 0x00123000}}
  release_manifest.tcl {uplevel 1 {set arm5_release_hashes {}}}
  replay_stop.tcl {proc ::replay_stop {} {incr ::stopped}}
  ps7_init.tcl {proc ::ps7_init {} {};proc ::ps7_post_config {} {}}
  default {uplevel 1 [list real_source $path]}
 }
}
source [file join $root runtime load_arm5.tcl]
if {$downloaded!=1 || $resumed!=1 || $pwm || $access!=0} {error "Loader happy path failed"}
set pwm 1
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *LOAD_REFUSED* $msg]} {error "Active PWM not protected"}
if {$downloaded!=1 || $access!=0} {error "Refusal mutated loader state"}
set pwm 0;set bad_model 1
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *MODEL_CONFIG_MISMATCH* $msg]} {error "Wrong model accepted"}
if {$access!=0} {error "Access not restored"}
set bad_model 0;set bad_reference 1
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *ORIGIN_REFERENCE_MISMATCH* $msg]} {error "Old marker-only origin mode accepted"}
if {$access!=0} {error "Origin reference refusal leaked access setting"}
set bad_reference 0;set bad_occlusion 1
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *OCCLUSION_POLICY_MISMATCH* $msg]} {error "Old non-occlusion software accepted"}
if {$access!=0} {error "Occlusion refusal leaked access setting"}
set bad_occlusion 0;set bad_red 1
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *RED_FRAME_POLICY_MISMATCH* $msg]} {error "Old black-frame software accepted"}
if {$access!=0} {error "Red-frame refusal leaked access setting"}
set bad_red 0;set bad_edge 1
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *EDGE_POLICY_MISMATCH* $msg]} {error "Old non-edge software accepted"}
if {$access!=0} {error "Edge-policy refusal leaked access setting"}
set bad_edge 0;set bad_normal_vote 1
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *NORMAL_VOTE_FIX_MISMATCH* $msg]} {error "Old normal-vote software accepted"}
if {$access!=0} {error "Normal-vote policy refusal leaked access setting"}
set bad_normal_vote 0;set ap_failure 1
set before_download $downloaded;set before_program $programmed
if {![catch {source [file join $root runtime load_arm5.tcl]} msg] || ![string match *PREFLIGHT_UNREADABLE* $msg]} {error "Unreadable preflight was ignored"}
if {$downloaded!=$before_download || $programmed!=$before_program || $system_resets || $access!=0} {error "Unreadable normal load performed mutation"}
set pl_ready 0
source [file join $root runtime load_arm5_cold.tcl]
if {$system_resets!=1 || $programmed!=$before_program+1 || $downloaded!=$before_download+1 || $pwm || $access!=0} {error "Cold recovery failed"}
if {[info exists ::arm5_cold_start]} {error "Cold bypass flag leaked"}
set reset_fail 1
set before_download $downloaded;set before_program $programmed
if {![catch {source [file join $root runtime load_arm5_cold.tcl]} msg] || ![string match *F0000021* $msg]} {error "Cold reset failure was ignored"}
if {$::arm5_load_stage ne "SYSTEM_RESET" || $downloaded!=$before_download || $programmed!=$before_program || [info exists ::arm5_cold_start]} {error "Failed reset continued or flag leaked"}
if {$cleared<4} {error "Fresh boot did not clear retained mailboxes"}
set reset_fail 0;set capture_error 1;set vdma_reads 0
if {![catch {source [file join $root runtime load_arm5_cold.tcl]} msg] || ![string match *diagnostic=7* $msg]} {error "Capture failure lost original error"}
if {$vdma_reads!=4 || $access!=0 || $::arm5_load_stage ne "WAIT_FIRMWARE" || [info exists ::arm5_cold_start]} {error "Capture failure diagnostic incomplete or changed access state"}
puts "ARM5_CAPTURE_FAILURE_DUMP_PASS; both VDMA snapshots, original error retained, no automatic restart"
set capture_error 0
set before_download $downloaded;set before_reset $system_resets
source [file join $root runtime load_arm5_wired_poweroff.tcl]
if {$downloaded!=$before_download+1 || $system_resets!=$before_reset+1 || $pwm || $access || [info exists ::arm5_cold_start]} {error "Wired power-off entry failed"}
set reset_fail 1;set before_download $downloaded
if {![catch {source [file join $root runtime load_arm5_wired_poweroff.tcl]} msg] || ![string match *F0000021* $msg]} {error "Wired entry ignored failed reset"}
if {$downloaded!=$before_download || [info exists ::arm5_cold_start]} {error "Wired entry continued after failed reset"}
puts "ARM5_WIRED_POWEROFF_MOCK_PASS; same verified load, PWM disabled, reset failure stops"
puts "ARM5_COLD_RECOVERY_MOCK_PASS; no old PL reads, fail-fast preflight, reset failure stops load"
puts "ARM5_LOADER_MOCK_PASS; no XSCT hardware connection was used"
