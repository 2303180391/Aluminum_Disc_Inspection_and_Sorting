# ARM5 software mailbox; never resumes CPU or enables PWM implicitly.
namespace eval ::arm5 {
 variable base 0x00E00900
 proc sha256 {path} {
  set ps {& {param($p) $f=[IO.File]::OpenRead($p); $h=[Security.Cryptography.SHA256]::Create(); try { [BitConverter]::ToString($h.ComputeHash($f)).Replace('-','') } finally { $h.Dispose(); $f.Dispose() }}}
  return [string toupper [string trim [exec powershell.exe -NoProfile -NonInteractive -Command $ps $path]]]
 }
 proc read {addr {count 1}} {
  set out {}
  foreach v [mrd -value $addr $count] {
   if {[string match -nocase 0x* $v] || ([regexp {^[0-9A-Fa-f]{8}$} $v] && [regexp {[A-Fa-f]} $v])} {
    scan $v %x number
   } else {set number [expr {wide($v)}]}
   lappend out [expr {$number&0xffffffff}]
  }
  if {[llength $out]!=$count} {error "ARM5 short memory read"}
  return $out
 }
 proc check {} {
  variable base
  if {[lindex [read 0x43C1006C] 0]!=0x41524D35 ||
      [lindex [read $base] 0]!=0x41354C31} {error "ARM5 firmware/hardware mismatch"}
 }
 proc status {} {
  variable base
  set saved [configparams force-mem-accesses]
  set rc [catch {
   configparams force-mem-accesses 1
   check
   set v [read $base 16]
   puts "ARM5 phase=[lindex $v 2] reason=[lindex $v 3] X_mm=[lindex $v 4] Y_mm=[lindex $v 5] class=[lindex $v 8] flags=[lindex $v 9] calibrated=[lindex $v 12]"
   set a [lindex $v 10]
   puts "ANGLES M1=[expr {$a&255}] M2=[expr {($a>>8)&255}] M3=[expr {($a>>16)&255}] M4=[expr {($a>>24)&255}] M5=[lindex $v 11]"
   set hw [lindex $v 7]
   puts "ARM5_HW enabled=[expr {$hw&1}] locked=[expr {($hw>>1)&1}] request=[expr {($hw>>2)&1}] ready=[expr {($hw>>3)&1}] busy=[expr {($hw>>4)&1}] done=[expr {($hw>>5)&1}] xy_valid=[lindex $v 14] locate_only=[lindex $v 15]"
   puts "CPU/PWM unchanged. phase: 0 idle,1 manual/home,2 observation move,3 locate settle,4 approach,5 classify,6 sort,7 error."
  } msg opts]
  configparams force-mem-accesses $saved
  if {$rc} {return -options $opts $msg}
 }
 proc command {cmd args} {
  variable base
  if {[llength $args]>6} {error "Maximum six arguments"}
  set saved [configparams force-mem-accesses]
  set rc [catch {
   configparams force-mem-accesses 1
   check
   if {[lindex [read [expr {$base+128}]] 0]!=0} {error "Pending command; do not overwrite"}
   set token [expr {([lindex [read [expr {$base+164}]] 0]+1)&0x7fffffff}]
   if {$token==0} {set token 1}
   mwr [expr {$base+132}] $cmd
   mwr [expr {$base+136}] [llength $args]
   set i 0
   foreach a $args {
    if {![string is integer -strict $a] || $a<-32768 || $a>32767} {error "Invalid integer argument"}
    mwr [expr {$base+140+4*$i}] [expr {$a&0xffffffff}]
    incr i
   }
   mwr [expr {$base+128}] $token
   set deadline [expr {[clock milliseconds]+10000}]
   while {[lindex [read [expr {$base+164}]] 0]!=$token} {
    if {[clock milliseconds]>$deadline} {error "ARM5 acknowledgement timeout; request may still be pending. Do not repeat; inspect CPU/status."}
    after 100
   }
   set reply [lindex [read [expr {$base+168}]] 0]
   puts "ARM5_COMMAND reply=$reply (2=accepted,14=calibration data,11=not ready/rejected). Completion is separate."
   if {$reply!=2 && $reply!=14} {error "ARM5 command rejected: $reply"}
  } msg opts]
  configparams force-mem-accesses $saved
  if {$rc} {return -options $opts $msg}
 }
}

