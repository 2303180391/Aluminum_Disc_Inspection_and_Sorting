set root [file dirname [file dirname [file normalize [info script]]]]
foreach f [glob [file join $root runtime arm5*.tcl]] {
 set fd [open $f];set source_text [read $fd];close $fd
 if {![info complete $source_text]} {error "Incomplete Tcl: $f"}
}
foreach f [glob [file join $root runtime load_arm5*.tcl]] {
 set fd [open $f];set src [read $fd];close $fd
 if {![info complete $src]} {error "Incomplete loader: $f"}
}
set access 0
proc configparams {name args} {
 global access
 if {[llength $args]} {set access [lindex $args 0]}
 return $access
}
array set memory {0x43C1006C 0x41524D35}
proc mrd {flag addr count} {
 global mem
 set out {}
 for {set i 0} {$i<$count} {incr i} {
  set a [expr {$addr+4*$i}]
  lappend out [expr {[info exists mem($a)]?$mem($a):0}]
 }
 return $out
}
proc mwr {addr value} {
 global mem
 set mem([expr {$addr+0}]) $value
 if {$addr==0x00E00980} {
  set mem(14682532) $value
  set mem(14682536) 2
  set mem([expr {0x00E00980}]) 0
 }
}
set mem([expr {0x43C1006C}]) 0x41524D35
set mem([expr {0x00E00900}]) 0x41354C31
source [file join $root runtime arm5_common.tcl]
::arm5::command 0x103
if {$access!=0} {error "Access setting leaked"}
set mem([expr {0x00E00980}]) 99
if {![catch {::arm5::command 0x102}]} {error "Overwrote pending request"}
if {$access!=0} {error "Access setting leaked on error"}
puts "ARM5_TCL_MAILBOX_PASS"
set a5path [file join $root runtime build_rebuilt yolo_metal_defect_3class.elf]
set a5hash [::arm5::sha256 $a5path]
source [file join $root runtime build_rebuilt symbol_addresses.tcl]
if {$a5hash ne $build_elf_sha256} {error "Loader hash command mismatch"}
puts "ARM5_LOADER_HASH_PASS"
