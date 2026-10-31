source [file join [file dirname [file normalize [info script]]] arm5_common.tcl]
proc arm5_joint {joint angle} {
 if {![string is integer -strict $joint] || $joint<1 || $joint>5} {error "Joint must be 1..5"}
 if {![string is integer -strict $angle] || $angle<0 || $angle>180 || ($joint==5 && ($angle<37 || $angle>90))} {error "Angle out of range"}
 ::arm5::command 74 $joint $angle
}
puts "Defined arm5_joint joint angle. No motion requested. Joint 5 is gripper; 37=open,60=closed. Wait for phase=0 before the next command."
