`timescale 1ns/1ps
module tb_arm5_controller;
    reg clk=0; always #5 clk=~clk;
    reg rst_n=0,ready=0,command_valid=0,command_error=0;
    reg [7:0] command=0;
    reg [2:0] argc=0;
    reg signed [15:0] p0=0,p1=0,p2=0,p3=0,p4=0,p5=0;
    wire [7:0] a0,a1,a2,a3,a4;
    wire reply_valid,busy; wire [3:0] reply_code;
    wire [31:0] calibration_reply_pose;
    wire [3:0] calibration_reply_id;
    wire calibration_reply_plane;
    wire observation_valid,calibration_valid;
    wire [3:0] current_stage;
    arm5_controller #(.CLK_HZ(1000),.STEP_MS(1),.PAUSE_MS(3)) dut(.*,
        .clk(clk),.rst_n(rst_n),.ready(ready),.command_valid(command_valid),
        .command_error(command_error),.command(command),.argc(argc),
        .p0(p0),.p1(p1),.p2(p2),.p3(p3),.p4(p4),.p5(p5),
        .angle0(a0),.angle1(a1),.angle2(a2),.angle3(a3),.angle4(a4),
        .reply_valid(reply_valid),.reply_code(reply_code),.busy(busy),
        .calibration_reply_pose(calibration_reply_pose),
        .calibration_reply_id(calibration_reply_id),
        .calibration_reply_plane(calibration_reply_plane));
    task issue(input [7:0] c,input [2:0] n,input integer x,input integer y);
        begin
            @(negedge clk); command=c;argc=n;p0=x;p1=y;command_valid=1;
            @(negedge clk);command_valid=0;
        end
    endtask
    task pick_class(input integer category);
        begin
            @(negedge clk);command="P";argc=3;p0=50;p1=50;p2=category;command_valid=1;
            @(negedge clk);command_valid=0;
        end
    endtask
    task load_pose(input integer plane,input integer id,
                   input integer m1,input integer m2,input integer m3,input integer m4);
        begin
            @(negedge clk);command="D";argc=6;p0=plane;p1=id;
            p2=m1;p3=m2;p4=m3;p5=m4;command_valid=1;
            @(negedge clk);command_valid=0;
        end
    endtask
    task response(input [3:0] expected);
        integer timeout;
        begin
            timeout=0;
            while(!reply_valid && timeout<200000) begin @(negedge clk);timeout=timeout+1;end
            if(!reply_valid || reply_code!==expected)
                $fatal(1,"response expected %0d got valid=%b code=%0d stage=%0d",expected,reply_valid,reply_code,dut.stage);
            // Consume the one-cycle reply before a caller waits for the next
            // response (G uses OK followed later by ARRIVED).
            @(negedge clk);
            while(reply_valid) @(negedge clk);
        end
    endtask
    task wait_done;
        integer timeout;
        begin
            timeout=0;
            while((busy || !reply_valid || reply_code!=4) && timeout<200000) begin @(negedge clk);timeout=timeout+1;end
            if(timeout>=200000 || a4<37 || a4>90)
                $fatal(1,"motion timeout or claw limit error stage=%0d",dut.stage);
        end
    endtask
    integer i,j,k,stop_stage,g2,g3,g4,gx,gy;
    reg [39:0] stopped_pose;
    reg [31:0] expected_sort;
    reg watch_sort=0,seen_approach=0,seen_descend=0,seen_grip=0;
    reg seen_raise=0,seen_lift=0,seen_release=0,seen_return=0;
    reg seen_base=0;
    reg [2:0] approach_steps=0,descend_steps=0;
    reg [3:0] raise_steps=0;
    reg [31:0] expected_step;
    reg [39:0] before_motion,hold_mask;
    integer settled_cycles=0,previous_stage=-1,previous_index=-1;
    always @(posedge clk) begin
        if(!watch_sort) begin
            settled_cycles=0;previous_stage=-1;previous_index=-1;
        end else begin
            if(previous_stage!=dut.stage || previous_index!=dut.motion_index) begin
                if((previous_stage==12 || previous_stage==3 || previous_stage==4 ||
                    previous_stage==5 || previous_stage==6) && settled_cycles<3)
                    $fatal(1,"motion advanced without the full settling interval");
                settled_cycles=0;
            end
            if(dut.at_target) settled_cycles=settled_cycles+1;
            else settled_cycles=0;
            previous_stage=dut.stage;previous_index=dut.motion_index;
        end
    end
    // Verify actual output changes on each clock, including intermediate angles.
    // M1 moves first. Approach and descent move only M4, M3, M2 in order.
    always @(posedge clk) if(watch_sort) begin
        before_motion={a4,a3,a2,a1,a0};hold_mask=0;
        case(dut.stage)
            12:hold_mask=40'hffffffff00; // only M1
            3:case(dut.motion_index)
                0:hold_mask=40'hff00ffffff; // only M4
                1:hold_mask=40'hffff00ffff; // only M3
                2:hold_mask=40'hffffff00ff; // only M2
            endcase
            4:case(dut.motion_index)
                0:hold_mask=40'hff00ffffff; // only M4
                1:hold_mask=40'hffff00ffff; // only M3
                2:hold_mask=40'hffffff00ff; // only M2
            endcase
            5:hold_mask=40'h00ffffffff; // only M5
            6:case(dut.motion_index)
                0:hold_mask=40'hffffff00ff; // only M2
                1:hold_mask=40'hffff00ffff; // only M3
                2:hold_mask=40'hff00ffffff; // only M4
                3:hold_mask=40'hffffffffff; // M5 remains closed
            endcase
        endcase
        #1;
        if(((before_motion^{a4,a3,a2,a1,a0}) & hold_mask)!=0)
            $fatal(1,"a joint moved out of the requested sequence");
    end
    // Sample at clock edges before nonblocking updates. Each flag records
    // reaching a phase target, not merely entering a state one step later.
    always @(posedge clk) if(watch_sort) begin
        if(dut.stage==12 && dut.at_target) begin
            if(a0!=40 || a4!=37) $fatal(1,"M1 did not position first");
            seen_base=1;
        end
        if(dut.stage==3 && dut.at_target) begin
            case(dut.motion_index)
                0:expected_step={8'd80,8'd50,8'd150,8'd40};
                1:expected_step={8'd80,8'd70,8'd150,8'd40};
                2:expected_step={8'd80,8'd70,8'd60,8'd40};
            endcase
            if(!seen_base || {a3,a2,a1,a0}!==expected_step || a4!=37)
                $fatal(1,"approach target is not above the pickup point");
            if(dut.motion_index>0 && !approach_steps[dut.motion_index-1])
                $fatal(1,"approach joint order skipped a step");
            approach_steps[dut.motion_index]=1;
            if(dut.motion_index==2) seen_approach=1;
        end
        if(dut.stage==4 && dut.at_target) begin
            case(dut.motion_index)
                0:expected_step={8'd70,8'd70,8'd60,8'd40};
                1:expected_step={8'd70,8'd60,8'd60,8'd40};
                2:expected_step={8'd70,8'd60,8'd50,8'd40};
            endcase
            if(!seen_approach || {a3,a2,a1,a0}!==expected_step || a4!=37)
                $fatal(1,"descent target is not the taught pickup pose");
            if(dut.motion_index>0 && !descend_steps[dut.motion_index-1])
                $fatal(1,"descent joint order skipped a step");
            descend_steps[dut.motion_index]=1;
            if(dut.motion_index==2) seen_descend=1;
        end
        if(dut.stage==5 && dut.at_target) begin
            if(!seen_descend || {a3,a2,a1,a0}!={8'd70,8'd60,8'd50,8'd40} || a4!=60)
                $fatal(1,"gripper did not close at the pickup pose");
            seen_grip=1;
        end
        if(dut.stage==6 && dut.at_target) begin
            case(dut.motion_index)
                0:expected_step={8'd70,8'd60,8'd60,8'd40};
                1:expected_step={8'd70,8'd70,8'd60,8'd40};
                default:expected_step={8'd80,8'd70,8'd60,8'd40};
            endcase
            if(!seen_grip || {a3,a2,a1,a0}!==expected_step || a4!=60)
                $fatal(1,"arm did not return above the piece after gripping");
            if(dut.motion_index>0 && !raise_steps[dut.motion_index-1])
                $fatal(1,"lift joint order skipped a step");
            raise_steps[dut.motion_index]=1;
            if(dut.motion_index==3) seen_raise=1;
        end
        if(dut.stage==9) begin
            if(!seen_raise || a4!=60) $fatal(1,"classification began before the lift completed");
            if(dut.at_target) begin
                if({a3,a2,a1,a0}!==(expected_sort)) $fatal(1,"wrong class destination");
                seen_lift=1;
            end
        end
        if(dut.stage==10 && dut.at_target) begin
            if(!seen_lift || {a3,a2,a1,a0}!==expected_sort || a4!=37)
                $fatal(1,"release before arrival at correct class pose");
            seen_release=1;
        end
        if(dut.stage==11 && dut.at_target) begin
            if(!seen_release || {a4,a3,a2,a1,a0}!=={8'd37,8'd65,8'd50,8'd150,8'd90})
                $fatal(1,"return did not finish after release");
            seen_return=1;
        end
    end
    integer poses[0:7][0:3];
    initial begin
        poses[0][0]=10;poses[0][1]=20;poses[0][2]=30;poses[0][3]=40;
        poses[1][0]=30;poses[1][1]=40;poses[1][2]=50;poses[1][3]=60;
        poses[2][0]=50;poses[2][1]=60;poses[2][2]=70;poses[2][3]=80;
        poses[3][0]=70;poses[3][1]=80;poses[3][2]=90;poses[3][3]=100;
        poses[4][0]=10;poses[4][1]=30;poses[4][2]=40;poses[4][3]=50;
        poses[5][0]=30;poses[5][1]=50;poses[5][2]=60;poses[5][3]=70;
        poses[6][0]=50;poses[6][1]=70;poses[6][2]=80;poses[6][3]=90;
        poses[7][0]=70;poses[7][1]=90;poses[7][2]=100;poses[7][3]=110;
        repeat(3) @(negedge clk);rst_n=1;ready=1;repeat(3) @(negedge clk);
        if({a4,a3,a2,a1,a0}!=={8'd37,8'd65,8'd50,8'd150,8'd90})
            $fatal(1,"wrong initial pose");
        issue("W",2,100,100);response(2);
        if(observation_valid) $fatal(1,"observation must not be guessed on reset");
        issue("B",0,0,0);response(9);
        issue("O",0,0,0);response(2);
        if(!observation_valid) $fatal(1,"observation not recorded");
        issue("J",2,1,95);response(2);wait_done;
        issue("B",0,0,0);response(2);wait_done;
        if({a4,a3,a2,a1,a0}!=={8'd37,8'd65,8'd50,8'd150,8'd90})
            $fatal(1,"observation return mismatch");
        // Teach each corner pose through manual joint commands then T/U.
        for(i=0;i<8;i=i+1) begin
            for(j=0;j<4;j=j+1) begin
                issue("J",2,j+1,poses[i][j]);response(2);wait_done;
            end
            if(i<4) begin issue("T",1,i,0);response(2);end
            else begin
                if(i==4) begin
                    issue("J",2,1,11);response(2);wait_done;
                    issue("U",1,0,0);response(9); // base must match T
                    issue("J",2,1,10);response(2);wait_done;
                end
                issue("U",1,i-4,0);response(2);
            end
            issue("J",2,5,37);response(2);wait_done;
        end
        issue("E",1,1,0);response(2);
        issue("J",2,5,36);response(7);
        issue("A",2,50,50);response(2);wait_done;
        if(a0!=40 || a1!=50 || a2!=60 || a3!=70 || a4!=37)
            $fatal(1,"center approach/grasp interpolation wrong: %0d %0d %0d %0d claw=%0d",a0,a1,a2,a3,a4);
        issue("P",2,50,50);response(2);wait_done;
        if(a0!=40 || a1!=60 || a2!=70 || a3!=80 || a4!=60)
            $fatal(1,"pickup did not finish raised and closed: %0d %0d %0d %0d claw=%0d",a0,a1,a2,a3,a4);
        // Two-stage camera workflow: G reaches the raised pose and waits;
        // K supplies the later classification and completes the pick/sort.
        issue("J",2,5,37);response(2);wait_done;
        issue("G",2,50,50);response(2);response(13);
        if(dut.stage!=13 || {a3,a2,a1,a0}!=={8'd80,8'd70,8'd60,8'd40} || a4!=37)
            $fatal(1,"G did not stop at the raised open pose");
        issue("K",1,2,0);response(2);wait_done;
        if({a4,a3,a2,a1,a0}!=={8'd37,8'd65,8'd50,8'd150,8'd90})
            $fatal(1,"K did not complete classification and return home");
        issue("P",2,101,50);response(12);
        issue("J",2,5,37);response(2);wait_done;
        pick_class(-1);response(7);
        pick_class(4);response(7);
        issue("A",3,50,50);response(6);
        for(k=0;k<4;k=k+1) begin
            case(k)
                0:expected_sort={8'd130,8'd0,8'd140,8'd135};
                1:expected_sort={8'd120,8'd40,8'd110,8'd135};
                2:expected_sort={8'd130,8'd0,8'd140,8'd150};
                3:expected_sort={8'd120,8'd40,8'd110,8'd150};
            endcase
            seen_approach=0;seen_descend=0;seen_grip=0;seen_raise=0;
            seen_base=0;approach_steps=0;descend_steps=0;raise_steps=0;
            seen_lift=0;seen_release=0;seen_return=0;watch_sort=1;
            pick_class(k);response(2);
            issue("J",2,1,0);response(3); // Do not overwrite an active sort.
            wait_done;watch_sort=0;
            if(!seen_base || approach_steps!=7 || descend_steps!=7 || raise_steps!=15 ||
                !seen_approach || !seen_descend || !seen_grip || !seen_raise ||
                !seen_lift || !seen_release || !seen_return ||
                {a4,a3,a2,a1,a0}!=={8'd37,8'd65,8'd50,8'd150,8'd90})
                $fatal(1,"class %0d did not complete release and return",k);
        end
        // The production camera workflow must reach each same destination via G/K.
        for(k=0;k<4;k=k+1) begin
            case(k)
                0:expected_sort={8'd130,8'd0,8'd140,8'd135};
                1:expected_sort={8'd120,8'd40,8'd110,8'd135};
                2:expected_sort={8'd130,8'd0,8'd140,8'd150};
                3:expected_sort={8'd120,8'd40,8'd110,8'd150};
            endcase
            seen_approach=0;seen_descend=0;seen_grip=0;seen_raise=0;
            seen_base=0;approach_steps=0;descend_steps=0;raise_steps=0;
            seen_lift=0;seen_release=0;seen_return=0;watch_sort=1;
            issue("G",2,50,50);response(2);response(13);
            if(dut.stage!=13 || !busy || a4!=37)
                $fatal(1,"class %0d G did not wait raised/open with busy",k);
            issue("K",1,k,0);response(2);
            issue("J",2,1,0);response(3);
            wait_done;watch_sort=0;
            if(!seen_base || approach_steps!=7 || descend_steps!=7 || raise_steps!=15 ||
                !seen_approach || !seen_descend || !seen_grip || !seen_raise ||
                !seen_lift || !seen_release || !seen_return ||
                {a4,a3,a2,a1,a0}!=={8'd37,8'd65,8'd50,8'd150,8'd90})
                $fatal(1,"class %0d G/K did not sort, release and return",k);
            $display("ANGLE_CLASS_PASS class=%0d M1=%0d M2=%0d M3=%0d M4=%0d",
                k,expected_sort[7:0],expected_sort[15:8],expected_sort[23:16],expected_sort[31:24]);
        end
        // STOP must hold its instantaneous pose in all new automatic phases.
        for(stop_stage=3;stop_stage<=12;stop_stage=stop_stage+1) begin
            if(stop_stage!=7 && stop_stage!=8) begin
                pick_class(0);response(2);
                wait(dut.stage==stop_stage);
                issue("X",0,0,0);response(5);
                stopped_pose={a4,a3,a2,a1,a0};
                repeat(300) @(negedge clk);
                if(busy || {a4,a3,a2,a1,a0}!==stopped_pose)
                    $fatal(1,"STOP failed in phase %0d",stop_stage);
                issue("J",2,5,37);response(2);wait_done;
            end
        end
        issue("H",0,0,0);response(2);wait_done;
        if({a4,a3,a2,a1,a0}!=={8'd37,8'd65,8'd50,8'd150,8'd90})
            $fatal(1,"H used old initial pose");
        // A nonlinear center distinguishes the new 3x3 mode from the old mode.
        issue("W",2,100,100);response(2);
        issue("E",1,2,0);response(9); // incomplete grid
        for(i=0;i<9;i=i+1) begin
            g2=(i<4)?50:60+(i-4)*10;
            g3=g2+10;g4=g2+20;
            issue("J",2,1,90);response(2);wait_done;
            issue("J",2,2,g2);response(2);wait_done;
            issue("J",2,3,g3);response(2);wait_done;
            issue("J",2,4,g4);response(2);wait_done;
            issue("T",1,i,0);response(2);
            issue("J",2,2,g2-10);response(2);wait_done;
            issue("J",2,3,g3-10);response(2);wait_done;
            issue("J",2,4,g4-10);response(2);wait_done;
            issue("U",1,i,0);response(2);
        end
        issue("E",1,2,0);response(2);
        for(i=0;i<9;i=i+1) begin
            case(i)
                0:begin gx=0;gy=0;end
                1:begin gx=100;gy=0;end
                2:begin gx=0;gy=100;end
                3:begin gx=100;gy=100;end
                4:begin gx=50;gy=0;end
                5:begin gx=0;gy=50;end
                6:begin gx=100;gy=50;end
                7:begin gx=50;gy=100;end
                8:begin gx=50;gy=50;end
            endcase
            g2=(i<4)?50:60+(i-4)*10;
            g3=g2+10;g4=g2+20;
            issue("G",2,gx,gy);response(2);response(13);
            if(dut.grasp_pose!=={g4[7:0],g3[7:0],g2[7:0],8'd90} ||
                dut.lift_pose[31:24]!=g4-10 ||
                dut.lift_pose[23:16]!=g3-10 ||
                dut.lift_pose[15:8]!=g2-10 || dut.lift_pose[7:0]!=90)
                $fatal(1,"grid missed taught node %0d",i);
            issue("X",0,0,0);response(5);
        end
        issue("G",2,25,25);response(2);response(13);
        if(dut.grasp_pose!=={8'd90,8'd80,8'd70,8'd90}) $fatal(1,"grid quadrant 0");
        issue("X",0,0,0);response(5);
        issue("G",2,75,25);response(2);response(13);
        if(dut.grasp_pose!=={8'd93,8'd83,8'd73,8'd90}) $fatal(1,"grid quadrant 1");
        issue("X",0,0,0);response(5);
        issue("G",2,25,75);response(2);response(13);
        if(dut.grasp_pose!=={8'd98,8'd88,8'd78,8'd90}) $fatal(1,"grid quadrant 2");
        issue("X",0,0,0);response(5);
        issue("G",2,75,75);response(2);response(13);
        if(dut.grasp_pose!=={8'd100,8'd90,8'd80,8'd90}) $fatal(1,"grid quadrant 3");
        issue("X",0,0,0);response(5);
        issue("E",1,1,0);response(2);
        issue("G",2,50,50);response(2);response(13);
        if(dut.grasp_pose!=={8'd70,8'd60,8'd50,8'd90})
            $fatal(1,"legacy four-corner mode changed");
        issue("X",0,0,0);response(5);
        $display("PASS: four-class motion, nine-point interpolation, four-corner compatibility");
        $finish;
    end
    initial begin #100000000; $fatal(1,"global timeout");end
endmodule
