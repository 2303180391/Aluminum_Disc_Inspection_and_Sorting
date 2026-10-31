# Exact, read-only verification of every parameter byte loaded from the
# DS14 metal-defect SD package.  Run with ARM5 PWM disabled; CPU is not paused.
# The script does not modify DDR, the ELF, the PL bitstream, or the SD card.

set ds14_audit_root [file dirname [info script]]
set audit_param_root [file join $ds14_audit_root sd_bin_board_v2]
set audit_output_root [file join $ds14_audit_root captures]

set audit_bias_files {
    l0_b.bin l2_b.bin l4_b.bin l6_b.bin l8_b.bin l8_b.bin l10_b.bin
    l12_b.bin l13_b.bin l14_b.bin l15_b.bin l18_b.bin l21_b.bin l22_b.bin
}
set audit_bias_lengths {64 128 256 512 1024 1024 2048 4096 1024 2048 96 512 1024 96}

set audit_act_files {
    l0_r.bin l2_r.bin l4_r.bin l6_r.bin l820_r.bin l8_r.bin l10_r.bin
    l12_r.bin l13_r.bin l14_r.bin l15_r.bin l18_r.bin l21_r.bin l22_r.bin
}
set audit_act_lengths {256 256 256 256 256 256 256 256 256 256 256 256 256 256}

set audit_weight_files {
    l0_w.bin l2_w.bin l4_w.bin l6_w.bin l8_w.bin l8_w.bin l10_w.bin
    l12_w.bin l13_w.bin l14_w.bin l15_w.bin l18_w.bin l21_w.bin l22_w.bin
}
set audit_weight_lengths {
    1152 4608 18432 73728 294912 294912 1179648 4718592
    262144 1179648 12288 32768 884736 6144
}

proc audit_sum {values} {
    set total 0
    foreach value $values {incr total $value}
    return $total
}

proc audit_read_binary {handle count} {
    set data [read $handle $count]
    if {[string length $data] != $count} {
        error "short binary read: wanted $count bytes, got [string length $data]"
    }
    return $data
}

# Returns -1 for an exact match, otherwise the first mismatching byte offset
# within the expected file.
proc audit_compare_segment {dump_path dump_offset expected_path expected_length} {
    set dump_handle [open $dump_path rb]
    set expected_handle [open $expected_path rb]
    fconfigure $dump_handle -translation binary -encoding binary
    fconfigure $expected_handle -translation binary -encoding binary
    seek $dump_handle $dump_offset start

    set checked 0
    set first_mismatch -1
    while {$checked < $expected_length} {
        set remaining [expr {$expected_length - $checked}]
        set chunk_length [expr {$remaining > 65536 ? 65536 : $remaining}]
        set got [audit_read_binary $dump_handle $chunk_length]
        set want [audit_read_binary $expected_handle $chunk_length]
        if {$got ne $want} {
            binary scan $got cu* got_bytes
            binary scan $want cu* want_bytes
            for {set i 0} {$i < $chunk_length} {incr i} {
                if {[lindex $got_bytes $i] != [lindex $want_bytes $i]} {
                    set first_mismatch [expr {$checked + $i}]
                    break
                }
            }
            break
        }
        incr checked $chunk_length
    }
    close $dump_handle
    close $expected_handle
    return $first_mismatch
}

proc audit_check_group {group_name dump_path root files lengths} {
    set offset 0
    set mismatched_files 0
    set i 0
    foreach filename $files length $lengths {
        set expected_path [file join $root $filename]
        if {![file exists $expected_path]} {
            error "$group_name layer $i is missing host file $expected_path"
        }
        set host_length [file size $expected_path]
        if {$host_length != $length} {
            error "$group_name layer $i file-size mismatch: $filename host=$host_length loader=$length"
        }
        set first [audit_compare_segment $dump_path $offset $expected_path $length]
        if {$first < 0} {
            puts "FULL_PARAM $group_name layer=$i file=$filename bytes=$length result=MATCH"
        } else {
            incr mismatched_files
            puts "FULL_PARAM $group_name layer=$i file=$filename bytes=$length result=MISMATCH first_offset=$first ddr=0x[format %08X [expr {$offset + $first}]]"
        }
        incr offset $length
        incr i
    }
    return $mismatched_files
}

source [file join $ds14_audit_root arm5_common.tcl]
targets -set -filter {name =~ "ARM*#0"}
set gate_saved [configparams force-mem-accesses]
set gate_rc [catch {
 configparams force-mem-accesses 1
 ::arm5::check
 if {[lindex [::arm5::read 0x43C10068] 0]&0x17} {error "FULL_AUDIT_REQUIRES_PWM_DISABLED"}
} gate_msg gate_opts]
configparams force-mem-accesses $gate_saved
if {$gate_rc} {return -options $gate_opts $gate_msg}

set audit_saved_access [configparams force-mem-accesses]
set audit_rc [catch {
    configparams force-mem-accesses 1

    set bias_total [audit_sum $audit_bias_lengths]
    set act_total [audit_sum $audit_act_lengths]
    set weight_total [audit_sum $audit_weight_lengths]
    if {$bias_total != 13952 || $act_total != 3584 || $weight_total != 8963712} {
        error "internal parameter-layout mismatch: bias=$bias_total act=$act_total weight=$weight_total"
    }

    set capture_dir [file join $audit_output_root "board_dump_[clock seconds]"]
    file mkdir $capture_dir
    set bias_dump [file join $capture_dir bias_ddr.bin]
    set act_dump [file join $capture_dir act_ddr.bin]
    set weight_dump [file join $capture_dir weight_ddr.bin]

    puts "FULL_PARAM_DUMP_BEGIN total_bytes=[expr {$bias_total + $act_total + $weight_total}]"
    puts "Reading bias bytes=$bias_total"
    # XSCT mrd count is in 32-bit words, while all layout lengths above are bytes.
    mrd -bin -file $bias_dump 0x02000000 [expr {$bias_total / 4}]
    puts "Reading activation-table bytes=$act_total"
    mrd -bin -file $act_dump 0x02004000 [expr {$act_total / 4}]
    puts "Reading weight bytes=$weight_total; this is the slow part"
    mrd -bin -file $weight_dump 0x02005000 [expr {$weight_total / 4}]

    set mismatches 0
    incr mismatches [audit_check_group BIAS $bias_dump $audit_param_root $audit_bias_files $audit_bias_lengths]
    incr mismatches [audit_check_group ACT $act_dump $audit_param_root $audit_act_files $audit_act_lengths]
    incr mismatches [audit_check_group WEIGHT $weight_dump $audit_param_root $audit_weight_files $audit_weight_lengths]

    if {$mismatches == 0} {
        puts "FULL_PARAMETER_VERIFY_OK files=42 bytes=8981248 capture=$capture_dir"
    } else {
        error "FULL_PARAMETER_VERIFY_MISMATCH mismatched_files=$mismatches capture=$capture_dir"
    }
} audit_error audit_options]
configparams force-mem-accesses $audit_saved_access
if {$audit_rc} {return -options $audit_options $audit_error}
