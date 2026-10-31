# Optional IDE import after the new hardware XSA has passed timing.
set root [file dirname [file dirname [file normalize [info script]]]]
set build [file join $root build_out]
if {[llength $argv]} {set build [file normalize [lindex $argv 0]]}
set xsa [file join $build hardware design_1_wrapper_arm5.xsa]
set ws [file join $build vitis_workspace]
if {![file isfile $xsa]} {error "Build hardware first: $xsa"}
if {[file exists [file join $ws arm5_platform]]} {error "Vitis workspace already exists"}
setws $ws
platform create -name arm5_platform -hw $xsa -no-boot-bsp
domain create -name arm5_standalone -os standalone -proc ps7_cortexa9_0
domain active arm5_standalone
bsp setlib -name xilffs
bsp config stdin ps7_uart_0
bsp config stdout ps7_uart_0
platform generate
app create -name metal_defect_arm5 -platform arm5_platform -domain arm5_standalone -template {Empty Application(C)}
importsources -name metal_defect_arm5 -path [file join $root firmware src]
app config -name metal_defect_arm5 build-config Release
app config -name metal_defect_arm5 -set compiler-optimization {Optimize more (-O2)}
app config -name metal_defect_arm5 -add define-compiler-symbols PROFILE_TASK_DETAIL=0
app config -name metal_defect_arm5 -add define-compiler-symbols XPAR_AXI4_LITE_BOX_V1_0_0_BASEADDR=XPAR_MULTIBOX_OVERLAY_0_BASEADDR
app config -name metal_defect_arm5 -add libraries m
app config -name metal_defect_arm5 -add libraries xilffs
app config -name metal_defect_arm5 linker-script [file join $ws metal_defect_arm5 src lscript.ld]
app build -name metal_defect_arm5
if {![file isfile [file join $ws metal_defect_arm5 Release metal_defect_arm5.elf]]} {error "Vitis ELF missing"}
puts "SOURCE_ONLY_VITIS_WORKSPACE_CREATED $ws; use the main firmware build for the runtime symbol package"
exit
