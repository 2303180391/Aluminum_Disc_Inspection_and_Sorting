# Read-only. Never halt/resume the CPU or issue an arm / video command.
set nvroot [file dirname [file normalize [info script]]]
source [file join $nvroot arm5_common.tcl]
targets -set -filter {name =~ "ARM*#0"}
set nvsaved [configparams force-mem-accesses]
set nvrc [catch {
 configparams force-mem-accesses 1
 ::arm5::check
 set nvhead [::arm5::read 0x00E08000 8]
 if {[lindex $nvhead 0]!=0x4D545231 || [lindex $nvhead 6]!=0x4E564631} {
  error "NORMAL_VOTE_FIX_NOT_LOADED: this script requires the NVF1 ELF"
 }
 ::arm5::status
 set nvok 0
 for {set nvtry 0} {$nvtry<3} {incr nvtry} {
  set nvseq [lindex [::arm5::read 0x00E08010 1] 0]
  if {!$nvseq} {error "NORMAL_VOTE_NO_COMPLETED_FRAME"}
  set nvaddr [expr {0x00E08020+(($nvseq-1)%128)*320}]
  set nvrecord [::arm5::read $nvaddr 80]
  set nvseal [lindex [::arm5::read $nvaddr 1] 0]
  if {[lindex $nvrecord 0]==$nvseq && [lindex $nvrecord 79]==$nvseq && $nvseal==$nvseq} {set nvok 1;break}
 }
 if {!$nvok} {error "NORMAL_VOTE_SNAPSHOT_CHANGED: retry this read-only script"}
 puts "NORMAL_VOTE NVF1 frame=[lindex $nvrecord 1] accepted_before_dedup=[lindex $nvrecord 4] accepted_after_dedup=[lindex $nvrecord 5] ignored_geometry=[lindex $nvrecord 72] ignored_edge=[lindex $nvrecord 74]"
 puts "Current decoded frame only; this does not reconstruct the original failure. CPU/PWM unchanged."
} nvmsg nvopts]
configparams force-mem-accesses $nvsaved
if {$nvrc} {return -options $nvopts $nvmsg}
