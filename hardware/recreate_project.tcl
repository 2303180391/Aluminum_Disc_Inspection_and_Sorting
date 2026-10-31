# Vivado 2023.1. Creates a fresh project; never opens an old .xpr or .dcp.
set source_root [file dirname [file dirname [file normalize [info script]]]]
set output_root [file join $source_root build_out hardware]
if {[llength $argv]>0} {set output_root [file normalize [lindex $argv 0]]}
set full_build [expr {[lsearch -exact $argv --build]>=0}]
set generate_outputs [expr {$full_build || [lsearch -exact $argv --generate]>=0}]
puts "SOURCE_BUILD_MODE argv=$argv full=$full_build generate=$generate_outputs"
set project_dir [file join $output_root project]
if {[file exists [file join $project_dir aluminum_sorter.xpr]]} {error "Build directory already has a project; choose a fresh OutDir"}
file mkdir $output_root
source [file join $source_root hardware source_files.tcl]
create_project aluminum_sorter $project_dir -part xc7z035ffg900-2
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property default_lib xil_defaultlib [current_project]
add_files -norecurse $rtl_files
set_property include_dirs [list [file join $source_root hardware tree hardware_project src]] [get_filesets sources_1]
set_property file_type Verilog [get_files *multibox_overlay.sv]
set_property file_type SystemVerilog [get_files *arm5_bridge.sv]
set generated_ip_files [list]
foreach src $ip_files {
 set local_dir [file join $output_root inputs ip [file rootname [file tail $src]]]
 file mkdir $local_dir
 set local [file join $local_dir [file tail $src]]
 file copy $src $local
 read_ip $local
 lappend generated_ip_files $local
}
update_compile_order -fileset sources_1
source [file join $source_root hardware create_bd.tcl]
set local_bd [get_property NAME [get_files design_1.bd]]
if {$local_bd eq ""} {error "Block Design reconstruction failed"}
foreach ip [get_ips -quiet] {
 if {[string match *:module_ref:* [get_property IPDEF $ip]]} {update_module_reference $ip}
}
validate_bd_design
save_bd_design
make_wrapper -files [get_files $local_bd] -top -import
set_property top design_1_wrapper [get_filesets sources_1]
add_files -fileset constrs_1 -norecurse $constraints_files
update_compile_order -fileset sources_1
set audit [open [file join $output_root source_audit.txt] w]
foreach f [get_files -all] {
 set n [file normalize $f]
 if {[string first [string tolower $source_root] [string tolower $n]]!=0 && [string first [string tolower $output_root] [string tolower $n]]!=0} {error "Foreign design dependency: $n"}
 puts $audit $n
}
close $audit
puts "SOURCE_ONLY_PROJECT_CREATED"
if {$generate_outputs} {
 generate_target all [get_files $generated_ip_files]
 generate_target all [get_files $local_bd]
 puts "SOURCE_ONLY_IP_GENERATION_PASS"
}
if {!$full_build} {close_project;exit}
create_ip_run [get_files $generated_ip_files]
create_ip_run [get_files $local_bd]
set ip_runs [get_runs -filter {IS_SYNTHESIS == 1 && NAME != synth_1}]
if {[llength $ip_runs]} {launch_runs $ip_runs -jobs 4}
foreach r $ip_runs {
 wait_on_run $r
 if {![string match *Complete* [get_property STATUS $r]]} {error "IP synthesis failed: $r"}
}
launch_runs synth_1 -jobs 4
wait_on_run synth_1
if {![string match *Complete* [get_property STATUS [get_runs synth_1]]]} {error "Synthesis failed"}
set_property strategy Performance_NetDelay_high [get_runs impl_1]
set_property STEPS.PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
set_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
launch_runs impl_1 -to_step route_design -jobs 4
wait_on_run impl_1
if {![string match *Complete* [get_property STATUS [get_runs impl_1]]]} {error "Implementation failed"}
open_run impl_1
foreach directive {AlternateReplication AggressiveExplore} {
 set setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
 set hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
 if {$setup>=0 && $hold>=0} {break}
 phys_opt_design -directive $directive
 route_design -directive Explore
}
report_timing_summary -file [file join $output_root timing.rpt]
report_bus_skew -file [file join $output_root bus_skew.rpt]
report_drc -file [file join $output_root drc.rpt]
report_utilization -file [file join $output_root utilization.rpt]
set setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
if {$setup<0 || $hold<0} {error "Timing gate failed: setup=$setup hold=$hold"}
foreach {port pin} {arm_pwm[0] AD13 arm_pwm[1] AD14 arm_pwm[2] AC16 arm_pwm[3] AC17 arm_pwm[4] AB16} {
 if {[get_property PACKAGE_PIN [get_ports $port]] ne $pin} {error "PWM pin mismatch: $port"}
}
write_bitstream -force [file join $output_root design_1_wrapper_arm5.bit]
write_hw_platform -fixed -include_bit -force -file [file join $output_root design_1_wrapper_arm5.xsa]
puts "SOURCE_ONLY_HARDWARE_BUILD_PASS setup=$setup hold=$hold"
close_project
