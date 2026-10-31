# File-only verification. No connect, memory access, CPU or arm command.
set angle5root [file dirname [file normalize [info script]]]
source [file join $angle5root arm5_common.tcl]
source [file join $angle5root release_manifest.tcl]
if {![info exists arm5_sort_angle_profile] || $arm5_sort_angle_profile ne "ANG20261029"} {
 error "ARM5_ANGLE_PROFILE_MISSING: package not finalized"
}
foreach {angle5file angle5expected} $arm5_release_hashes {
 set angle5path [file join $angle5root $angle5file]
 if {![file isfile $angle5path] || [::arm5::sha256 $angle5path] ne $angle5expected} {
  error "ARM5_ANGLE_PACKAGE_FILE_MISMATCH: $angle5file"
 }
}
foreach {angle5name angle5code angle5m1 angle5m2 angle5m3 angle5m4} $arm5_sort_angle_table {
 puts "SORT_TARGET class=$angle5name code=$angle5code M1=$angle5m1 M2=$angle5m2 M3=$angle5m3 M4=$angle5m4"
}
puts "ARM5_SORT_ANGLE_FILES_OK ANG20261029; local file hashes only. This does not identify an already-running board. CPU/PWM unchanged."
