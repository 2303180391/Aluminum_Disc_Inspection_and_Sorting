# XSCT can raise a Tcl error when the selected processor is already stopped.
# Only this exact harmless condition is accepted. Never hide Invalid context.
proc replay_stop {} {
    set rc [catch {stop} msg opts]
    if {!$rc} {return}
    if {[string trim $msg] eq "Already stopped"} {
        puts "Selected CPU already stopped; continuing."
        return
    }
    return -options $opts $msg
}
