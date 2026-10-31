# Read-only failure/progress summary for the three-class metal-defect candidate.
targets -set -filter {name =~ "ARM*#0"}
# Live reads only: never suspend the CPU while the PL arm may be moving.
set md_saved_access [configparams force-mem-accesses]
set md_rc [catch {
    configparams force-mem-accesses 1
    set d [mrd -value 0x00E00000 64]
    if {[lindex $d 0] != 0x59444731 || [lindex $d 1] != 2} {
        error "Metal-defect diagnostic mailbox absent; wrong ELF or program not started"
    }
    set stage_names {ZERO BOOT LOAD READY CAPTURE FRAME TASK IRQ_WAIT DMA_WAIT LAYER COMPLETE}
    set error_names {NONE SD IRQ_INIT DMA_RESET DMA_STATUS DMA_TIMEOUT PL_TIMEOUT CAPTURE INPUT_CHANGED LAYER_CHANGED BAD_TRANSFER PARAM_CHANGED OVERLAY_SIGNATURE OVERLAY_APPLY_TIMEOUT TRACK_COMMIT}
    set stage [lindex $d 2]
    set error [lindex $d 3]
    if {$stage == 255} {set stage_name FAILED} elseif {$stage < [llength $stage_names]} {set stage_name [lindex $stage_names $stage]} else {set stage_name UNKNOWN}
    if {$error < [llength $error_names]} {set error_name [lindex $error_names $error]} else {set error_name UNKNOWN}
    puts "DIAG stage=$stage_name error=$error_name frame=[lindex $d 5] layer=[lindex $d 6] task=[lindex $d 7]"
    puts "DMA TX=0x[format %08X [lindex $d 13]] RX=0x[format %08X [lindex $d 14]] VDMA=0x[format %08X [lindex $d 22]] irq=[lindex $d 15] completed_tasks=[lindex $d 19]"
    puts "INPUT crc=0x[format %08X [lindex $d 20]] reference=0x[format %08X [lindex $d 21]]"
    set cap [mrd -value 0x00E00400 13]
    puts "CAPTURE magic=0x[format %08X [lindex $cap 0]] phase=[lindex $cap 1] completed=[lindex $cap 11] switches=[lindex $cap 12] copied=[lindex $cap 7]"
    puts "CAPTURE_DETAIL events=[lindex $cap 4] CR=0x[format %08X [lindex $cap 6]] SR=0x[format %08X [lindex $cap 5]] active=0x[format %08X [lindex $cap 2]] frozen=0x[format %08X [lindex $cap 3]] failed_from_stage=[lindex $d 30]"
    foreach {vd_name vd_base} {HDMI_720P 0x43000000 YOLO_416 0x43010000} {
        # Only implemented S2MM registers; no reserved-word range reads.
        set vd_cs [mrd -value [expr {$vd_base+0x30}] 2]
        set vd_geom [mrd -value [expr {$vd_base+0xA0}] 4]
        puts "VDMA_$vd_name CR=0x[format %08X [lindex $vd_cs 0]] SR=0x[format %08X [lindex $vd_cs 1]] VSIZE=[lindex $vd_geom 0] HSIZE=[lindex $vd_geom 1] STRIDE=[lindex $vd_geom 2] FRAME=0x[format %08X [lindex $vd_geom 3]]"
    }
    set th [mrd -value 0x00E08000 8]
    puts "TRACK magic=0x[format %08X [lindex $th 0]] records=[lindex $th 4]"
    set vs [mrd -value 0x00E00B00 16]
    if {[lindex $vs 0]==0x56535931} {
        puts "VIDEO_STARTUP phase=[lindex $vs 2] result=[lindex $vs 3] reason=[lindex $vs 15] clean_720p=[lindex $vs 10] clean_416=[lindex $vs 11] clears_720p=[lindex $vs 12] clears_416=[lindex $vs 13] elapsed_ms=[lindex $vs 14]"
        puts "VIDEO_STARTUP_SR initial_720p=0x[format %08X [lindex $vs 4]] initial_416=0x[format %08X [lindex $vs 5]] final_720p=0x[format %08X [lindex $vs 6]] final_416=0x[format %08X [lindex $vs 7]]"
    }
} md_error md_options]
configparams force-mem-accesses $md_saved_access
puts "CPU/PWM unchanged; live fields may span adjacent updates. Do not resume a failed CPU."
if {$md_rc} {return -options $md_options $md_error}
