`timescale 1ns/1ps
// Five-joint, camera-free teaching controller.
// M1..M4 are positioning joints (0..180); M5 is gripper (37..90).
// Teaching records COMMAND angles, not measured shaft positions.
// Grasp plane T and measured +30 mm plane U support four- or nine-point teaching.
// Piecewise bilinear JOINT interpolation does not enforce a Cartesian straight line,
// constant height, reachability, or collision avoidance. Validate physically.
module arm5_controller #(
    parameter integer CLK_HZ = 27_000_000,
    parameter integer STEP_MS = 20,
    parameter integer PAUSE_MS = 500
) (
    input wire clk,rst_n,ready,
    input wire command_valid,command_error,
    input wire [7:0] command,
    input wire [2:0] argc,
    input wire signed [15:0] p0,p1,p2,p3,p4,p5,
    output reg [7:0] angle0,angle1,angle2,angle3,angle4,
    output reg reply_valid,
    output reg [3:0] reply_code,
    output reg [31:0] calibration_reply_pose,
    output reg [3:0] calibration_reply_id,
    output reg calibration_reply_plane,
    output wire busy,
    output reg observation_valid,
    output wire calibration_valid,
    output wire [3:0] current_stage
);
    localparam integer STEP_TICKS=(CLK_HZ/1000)*STEP_MS;
    localparam integer PAUSE_TICKS=(CLK_HZ/1000)*PAUSE_MS;
    localparam integer SW=(STEP_TICKS<2)?1:$clog2(STEP_TICKS);
    localparam integer PW=(PAUSE_TICKS<2)?1:$clog2(PAUSE_TICKS);
    localparam [3:0] IDLE=0,CALC=1,GAP=2,APPROACH=3,
        DESCEND=4,GRIP=5,RAISE=6,MANUAL=7,HOME=8,
        SORT_MOVE=9,SORT_RELEASE=10,RETURN_HOME=11,POSITION_BASE=12,
        WAIT_CLASS=13;
    // Packed positioning pose is {M4,M3,M2,M1}; M5 is controlled separately.
    // Automatic pick: approach the taught U pose, descend to T, grip, return U,
    // then (when requested) sort and release. U is taught about 30 mm above T.
    localparam [31:0] HOME_POSE={8'd65,8'd50,8'd150,8'd90};
    localparam [7:0] CLAW_OPEN=8'd37, CLAW_CLOSED=8'd60;
    reg [3:0] stage;
    reg [31:0] observation_pose;
    assign current_stage=stage;
    // APPROACH/DESCEND: 0=M4, 1=M3, 2=M2. RAISE: 0=M2, 1=M3,
    // 2=M4, 3=M5 (hold closed). Every step settles before the next one.
    reg [1:0] motion_index;
    reg [SW-1:0] step_count;
    reg [PW-1:0] pause_count;
    reg ready_previous,enabled,position_only,observe_only,size_configured;
    reg sort_requested;
    reg [31:0] sort_pose;
    reg [9:0] size_x,size_y,requested_x,requested_y;
    // T nodes 0..8, U nodes 9..17, each {M4,M3,M2,M1}.
    // 0..3 are legacy corners; 4..7 edge midpoints; 8 is the center.
    reg [31:0] taught [0:17];
    reg [17:0] taught_valid;
    reg [1:0] interp_mode; // 1=four corners, 2=piecewise 3x3 grid
    reg [31:0] grasp_pose,lift_pose,manual_pose;
    reg [7:0] manual_claw;
    reg [1:0] pair_index;
    reg [31:0] corners_a,corners_b;
    wire calc_done;
    wire [7:0] result_a,result_b;
    wire legacy_valid=(&taught_valid[3:0]) && (&taught_valid[12:9]);
    wire grid_valid=(&taught_valid[8:0]) && (&taught_valid[17:9]);
    assign calibration_valid=enabled && ((interp_mode==2) ? grid_valid : legacy_valid);
    wire [4:0] taught_u_index={1'b0,p0[3:0]}+5'd9;
    wire [9:0] half_x=size_x>>1, half_y=size_y>>1;
    wire right_half=(requested_x>=half_x);
    wire upper_half=(requested_y>=half_y);
    wire [9:0] interp_width=(interp_mode==2) ?
        (right_half ? size_x-half_x : half_x) : size_x;
    wire [9:0] interp_height=(interp_mode==2) ?
        (upper_half ? size_y-half_y : half_y) : size_y;
    wire [9:0] interp_x=(interp_mode==2 && right_half) ?
        requested_x-half_x : requested_x;
    wire [9:0] interp_y=(interp_mode==2 && upper_half) ?
        requested_y-half_y : requested_y;
    reg [3:0] corner0,corner1,corner2,corner3;
    reg [4:0] point_offset;
    reg [31:0] pose0,pose1,pose2,pose3;
    // Reuse one sequential integer interpolator, two joints at a time.
    // Pair 0/1 = grasp M1,M2 / M3,M4; pair 2/3 = lift equivalents.
    always @* begin
        corner0=0;corner1=1;corner2=2;corner3=3;
        if(interp_mode==2) begin
            case({upper_half,right_half})
                2'b00:begin corner0=0;corner1=4;corner2=5;corner3=8;end
                2'b01:begin corner0=4;corner1=1;corner2=8;corner3=6;end
                2'b10:begin corner0=5;corner1=8;corner2=2;corner3=7;end
                2'b11:begin corner0=8;corner1=6;corner2=7;corner3=3;end
            endcase
        end
        point_offset=pair_index[1] ? 5'd9 : 5'd0;
        pose0=taught[corner0+point_offset];
        pose1=taught[corner1+point_offset];
        pose2=taught[corner2+point_offset];
        pose3=taught[corner3+point_offset];
        case(pair_index)
            0: begin
                corners_a={pose3[7:0],pose2[7:0],pose1[7:0],pose0[7:0]};
                corners_b={pose3[15:8],pose2[15:8],pose1[15:8],pose0[15:8]};
            end
            1: begin
                corners_a={pose3[23:16],pose2[23:16],pose1[23:16],pose0[23:16]};
                corners_b={pose3[31:24],pose2[31:24],pose1[31:24],pose0[31:24]};
            end
            2: begin
                corners_a={pose3[7:0],pose2[7:0],pose1[7:0],pose0[7:0]};
                corners_b={pose3[15:8],pose2[15:8],pose1[15:8],pose0[15:8]};
            end
            default: begin
                corners_a={pose3[23:16],pose2[23:16],pose1[23:16],pose0[23:16]};
                corners_b={pose3[31:24],pose2[31:24],pose1[31:24],pose0[31:24]};
            end
        endcase
    end
    arm_xy_interpolator interpolator (
        .clk(clk),.rst_n(rst_n),.enable(stage==CALC),
        .width(interp_width),.height(interp_height),.dx(interp_x),.dy(interp_y),
        .corners0(corners_a),.corners2(corners_b),
        .done(calc_done),.result0(result_a),.result2(result_b));

    wire [31:0] current_pose={angle3,angle2,angle1,angle0};
    reg [31:0] target_pose;
    reg [7:0] target_claw;
    wire at_target=(current_pose==target_pose && angle4==target_claw);
    assign busy=(stage!=IDLE);
    task reply;
        input [3:0] code;
        begin reply_valid<=1;reply_code<=code;end
    endtask
    always @* begin
        target_pose=current_pose;target_claw=angle4;
        case(stage)
            MANUAL: begin target_pose=manual_pose;target_claw=manual_claw;end
            HOME: target_pose=HOME_POSE; // Never automatically release a load.
            POSITION_BASE: begin target_pose[7:0]=lift_pose[7:0];target_claw=CLAW_OPEN;end
            APPROACH: begin
                target_claw=CLAW_OPEN;
                case(motion_index)
                    0:target_pose[31:24]=lift_pose[31:24]; // M4 first
                    1:target_pose[23:16]=lift_pose[23:16]; // then M3
                    default:target_pose[15:8]=lift_pose[15:8]; // then M2
                endcase
            end
            DESCEND: begin
                target_claw=CLAW_OPEN;
                case(motion_index)
                    0:target_pose[31:24]=grasp_pose[31:24]; // M4 first
                    1:target_pose[23:16]=grasp_pose[23:16]; // then M3
                    default:target_pose[15:8]=grasp_pose[15:8]; // then M2
                endcase
            end
            GRIP: begin target_pose=grasp_pose;target_claw=CLAW_CLOSED;end
            RAISE: begin
                target_claw=CLAW_CLOSED; // M5 must remain closed throughout lifting.
                case(motion_index)
                    0:target_pose[15:8]=lift_pose[15:8]; // M2 first
                    1:target_pose[23:16]=lift_pose[23:16]; // then M3
                    2:target_pose[31:24]=lift_pose[31:24]; // then M4
                    default: ; // Final M5 phase confirms closed; never release.
                endcase
            end
            // Carry the part CLOSED; release only after the whole pose settles.
            SORT_MOVE: begin target_pose=sort_pose;target_claw=CLAW_CLOSED;end
            SORT_RELEASE: begin target_pose=sort_pose;target_claw=CLAW_OPEN;end
            RETURN_HOME: begin target_pose=HOME_POSE;target_claw=CLAW_OPEN;end
            default: ;
        endcase
    end
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            angle0<=HOME_POSE[7:0];angle1<=HOME_POSE[15:8];
            angle2<=HOME_POSE[23:16];angle3<=HOME_POSE[31:24];angle4<=CLAW_OPEN;
            stage<=IDLE;motion_index<=0;step_count<=0;pause_count<=0;
            observation_valid<=0;observation_pose<=HOME_POSE;
            ready_previous<=0;enabled<=0;position_only<=0;observe_only<=0;size_configured<=0;
            sort_requested<=0;sort_pose<=HOME_POSE;
            size_x<=100;size_y<=100;requested_x<=0;requested_y<=0;
            taught_valid<=0;interp_mode<=0;pair_index<=0;grasp_pose<=HOME_POSE;
            lift_pose<=HOME_POSE;manual_pose<=HOME_POSE;manual_claw<=CLAW_OPEN;
            reply_valid<=0;reply_code<=0;
            calibration_reply_pose<=0;calibration_reply_id<=0;
            calibration_reply_plane<=0;
        end else begin
            reply_valid<=0;ready_previous<=ready;
            // STOP has priority and preserves all five instantaneous command angles.
            if(command_error) reply(6);
            else if(command_valid) begin
                if(command=="X" && argc==0) begin
                    stage<=IDLE;motion_index<=0;step_count<=0;pause_count<=0;
                    position_only<=0;observe_only<=0;sort_requested<=0;reply(5);
                end else if(command=="S" && argc==0) begin
                    if(!ready) reply(11);
                    else if(busy) reply(3);
                    else reply(1);
                end else if(!ready) reply(11);
                else if(busy && !(command=="K" && argc==1 && stage==WAIT_CLASS)) reply(3);
                else case(command)
                    "O": if(argc!=0) reply(6);
                        else if(angle4!=CLAW_OPEN) reply(9);
                        else begin observation_pose<=current_pose;observation_valid<=1;reply(2);end
                    "B": if(argc!=0) reply(6);
                        else if(!observation_valid || angle4!=CLAW_OPEN) reply(9);
                        else begin manual_pose<=observation_pose;manual_claw<=CLAW_OPEN;
                            stage<=MANUAL;step_count<=0;pause_count<=0;reply(2);end
                    "J": if(argc!=2) reply(6);
                        else if(p0<1 || p0>5 || p1<0 || p1>180 ||
                            (p0==5 && (p1<CLAW_OPEN || p1>90))) reply(7);
                        else begin
                            manual_pose<=current_pose;manual_claw<=angle4;
                            case(p0)
                                1:manual_pose[7:0]<=p1[7:0];
                                2:manual_pose[15:8]<=p1[7:0];
                                3:manual_pose[23:16]<=p1[7:0];
                                4:manual_pose[31:24]<=p1[7:0];
                                5:manual_claw<=p1[7:0];
                            endcase
                            stage<=MANUAL;step_count<=0;pause_count<=0;reply(2);
                        end
                    // W,X-length,Y-length: X follows user's L, Y follows W.
                    "W": if(argc!=2) reply(6);
                        else if(p0<1 || p0>500 || p1<1 || p1>500) reply(7);
                        else begin
                            size_x<=p0[9:0];size_y<=p1[9:0];
                            taught_valid<=0;enabled<=0;interp_mode<=0;
                            size_configured<=1;reply(2);
                        end
                    "T","U": if(argc!=1) reply(6);
                        else if(p0<0 || p0>8) reply(7);
                        else if(!size_configured) reply(9);
                        else if(p0>3 && (size_x<2 || size_y<2)) reply(9);
                        else if(angle4!=CLAW_OPEN) reply(9);
                        else if(command=="U" && !taught_valid[p0[3:0]]) reply(9);
                        // Vertical pickup/lift sequences keep the base fixed.
                        else if(command=="U" && angle0!=taught[p0[3:0]][7:0]) reply(9);
                        else begin
                            enabled<=0;
                            if(command=="T") begin
                                taught[p0[3:0]]<=current_pose;
                                taught_valid[p0[3:0]]<=1;
                                // Re-teaching a grasp invalidates its old lift pose.
                                taught_valid[taught_u_index]<=0;
                            end else begin
                                taught[taught_u_index]<=current_pose;
                                taught_valid[taught_u_index]<=1;
                            end
                            reply(2);
                        end
                    // D,plane,id,M1,M2,M3,M4: load a taught pose without motion.
                    // plane=0 for grasp T, 1 for approach U. Rewriting T
                    // invalidates its old U; re-enable interpolation with E.
                    "D": if(argc!=6) reply(6);
                        else if(p0<0 || p0>1 || p1<0 || p1>8 ||
                            p2<0 || p2>180 || p3<0 || p3>180 ||
                            p4<0 || p4>180 || p5<0 || p5>180) reply(7);
                        else if(!size_configured || (p1>3 && (size_x<2 || size_y<2))) reply(9);
                        else if(p0==1 && !taught_valid[p1[3:0]]) reply(9);
                        else if(p0==1 && p2[7:0]!=taught[p1[3:0]][7:0]) reply(9);
                        else begin
                            enabled<=0;
                            if(p0==0) begin
                                taught[p1[3:0]]<={p5[7:0],p4[7:0],p3[7:0],p2[7:0]};
                                taught_valid[p1[3:0]]<=1;
                                taught_valid[{1'b0,p1[3:0]}+5'd9]<=0;
                            end else begin
                                taught[{1'b0,p1[3:0]}+5'd9]<=
                                    {p5[7:0],p4[7:0],p3[7:0],p2[7:0]};
                                taught_valid[{1'b0,p1[3:0]}+5'd9]<=1;
                            end
                            reply(2);
                        end
                    // Q,plane,id: read back a taught pose as CAL,T/U,id,M1..M4.
                    "Q": if(argc!=2) reply(6);
                        else if(p0<0 || p0>1 || p1<0 || p1>8) reply(7);
                        else if(!taught_valid[{1'b0,p1[3:0]}+
                            (p0==1 ? 5'd9 : 5'd0)]) reply(8);
                        else begin
                            calibration_reply_pose<=taught[{1'b0,p1[3:0]}+
                                (p0==1 ? 5'd9 : 5'd0)];
                            calibration_reply_plane<=p0[0];
                            calibration_reply_id<=p1[3:0];
                            reply(14);
                        end
                    "E": if(argc!=1) reply(6);
                        else if(p0<0 || p0>2) reply(7);
                        else if(p0==1 && (!size_configured || !legacy_valid)) reply(9);
                        else if(p0==2 && (!size_configured || size_x<2 || size_y<2 || !grid_valid)) reply(9);
                        else begin enabled<=(p0!=0);interp_mode<=p0[1:0];reply(2);end
                    // A,X,Y: unloaded trial. P,X,Y: pick and hold.
                    // P,X,Y,class: pick, lift, sort, release, then return home.
                    // class 0=normal, 1=scratch, 2=dent, 3=stain.
                    // User-specified 2026-10-29 destinations. Packed M4,M3,M2,M1.
                    "G": if(argc!=2) reply(6);
                        else if(p0<0 || p1<0 || p0>$signed({6'd0,size_x}) ||
                            p1>$signed({6'd0,size_y})) reply(12);
                        else if(!size_configured || !enabled || !legacy_valid ||
                            (interp_mode==2 && !grid_valid) || angle4!=CLAW_OPEN) reply(9);
                        else begin
                            requested_x<=p0[9:0];requested_y<=p1[9:0];
                            position_only<=0;observe_only<=1;motion_index<=0;
                            sort_requested<=0;pair_index<=0;
                            stage<=CALC;step_count<=0;pause_count<=0;reply(2);
                        end
                    "K": if(argc!=1) reply(6);
                        else if(p0<0 || p0>3) reply(7);
                        else if(stage!=WAIT_CLASS || angle4!=CLAW_OPEN) reply(9);
                        else begin
                            observe_only<=0;sort_requested<=1;motion_index<=0;
                            case(p0)
                                0:sort_pose<={8'd130,8'd0,8'd140,8'd135};
                                1:sort_pose<={8'd120,8'd40,8'd110,8'd135};
                                2:sort_pose<={8'd130,8'd0,8'd140,8'd150};
                                3:sort_pose<={8'd120,8'd40,8'd110,8'd150};
                                default:sort_pose<=HOME_POSE;
                            endcase
                            stage<=DESCEND;step_count<=0;pause_count<=0;reply(2);
                        end
                    "A","P": if((command=="A" && argc!=2) ||
                        (command=="P" && argc!=2 && argc!=3)) reply(6);
                        else if(command=="P" && argc==3 && (p2<0 || p2>3)) reply(7);
                        else if(p0<0 || p1<0 || p0>$signed({6'd0,size_x}) ||
                            p1>$signed({6'd0,size_y})) reply(12);
                        else if(!size_configured || !enabled || !legacy_valid ||
                            (interp_mode==2 && !grid_valid) || angle4!=CLAW_OPEN) reply(9);
                        else begin
                            requested_x<=p0[9:0];requested_y<=p1[9:0];
                            position_only<=(command=="A");observe_only<=0;pair_index<=0;
                            motion_index<=0;
                            sort_requested<=(command=="P" && argc==3);
                            case(p2)
                                0:sort_pose<={8'd130,8'd0,8'd140,8'd135};
                                1:sort_pose<={8'd120,8'd40,8'd110,8'd135};
                                2:sort_pose<={8'd130,8'd0,8'd140,8'd150};
                                3:sort_pose<={8'd120,8'd40,8'd110,8'd150};
                                default:sort_pose<=HOME_POSE;
                            endcase
                            stage<=CALC;step_count<=0;pause_count<=0;reply(2);
                        end
                    "H": if(argc!=0) reply(6);
                        else begin stage<=HOME;step_count<=0;pause_count<=0;reply(2);end
                    default:reply(10);
                endcase
            end else if(!ready) begin
                stage<=IDLE;motion_index<=0;step_count<=0;pause_count<=0;
                position_only<=0;observe_only<=0;sort_requested<=0;
            end else if(!ready_previous) reply(1);
            else if(stage==CALC) begin
                if(calc_done) begin
                    case(pair_index)
                        0:grasp_pose[15:0]<={result_b,result_a};
                        1:grasp_pose[31:16]<={result_b,result_a};
                        2:lift_pose[15:0]<={result_b,result_a};
                        3:lift_pose[31:16]<={result_b,result_a};
                    endcase
                    if(pair_index==3) stage<=POSITION_BASE;
                    else begin pair_index<=pair_index+1'b1;stage<=GAP;end
                end
            end else if(stage==GAP) stage<=CALC; // Reset interpolator done between pairs.
            else if(stage==IDLE) begin step_count<=0;pause_count<=0;end
            else if(stage==WAIT_CLASS) begin
                // Hold the raised open pose indefinitely until K,class arrives.
                step_count<=0;pause_count<=0;
            end
            else begin
                // Each joint steps at most 1 degree/20 ms, then dwells 0.5 s.
                // Completion reflects commanded angles only, with no servo feedback.
                if(step_count==STEP_TICKS-1) begin
                    step_count<=0;
                    if(angle0<target_pose[7:0]) angle0<=angle0+1'b1;
                    else if(angle0>target_pose[7:0]) angle0<=angle0-1'b1;
                    if(angle1<target_pose[15:8]) angle1<=angle1+1'b1;
                    else if(angle1>target_pose[15:8]) angle1<=angle1-1'b1;
                    if(angle2<target_pose[23:16]) angle2<=angle2+1'b1;
                    else if(angle2>target_pose[23:16]) angle2<=angle2-1'b1;
                    if(angle3<target_pose[31:24]) angle3<=angle3+1'b1;
                    else if(angle3>target_pose[31:24]) angle3<=angle3-1'b1;
                    if(angle4<target_claw) angle4<=angle4+1'b1;
                    else if(angle4>target_claw) angle4<=angle4-1'b1;
                end else step_count<=step_count+1'b1;
                if(!at_target) pause_count<=0;
                else if(pause_count==PAUSE_TICKS-1) begin
                    pause_count<=0;step_count<=0;
                    case(stage)
                        POSITION_BASE:begin stage<=APPROACH;motion_index<=0;end
                        APPROACH:begin
                            if(motion_index!=2) motion_index<=motion_index+1'b1;
                            else begin
                                motion_index<=0;
                                if(observe_only) begin stage<=WAIT_CLASS;reply(13);end
                                else stage<=DESCEND;
                            end
                        end
                        DESCEND:if(motion_index!=2) motion_index<=motion_index+1'b1;
                            else begin
                                motion_index<=0;
                                if(position_only) begin stage<=IDLE;reply(4);end
                                else stage<=GRIP;
                            end
                        GRIP:begin stage<=RAISE;motion_index<=0;end
                        RAISE:if(motion_index!=3) motion_index<=motion_index+1'b1;
                            else begin
                                motion_index<=0;
                                if(sort_requested) stage<=SORT_MOVE;
                                else begin stage<=IDLE;reply(4);end
                            end
                        SORT_MOVE:stage<=SORT_RELEASE;
                        SORT_RELEASE:stage<=RETURN_HOME;
                        default:begin stage<=IDLE;reply(4);end
                    endcase
                end else pause_count<=pause_count+1'b1;
            end
        end
    end
endmodule
