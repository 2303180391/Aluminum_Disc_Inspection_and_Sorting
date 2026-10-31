`timescale 1ns/1ps
// CSQ1 experimental shell. Arithmetic and FIFO semantics remain in the core.
// reg0[24]+WRITE: load an entire raw feature tensor (8-channel-major).
// reg0[25]+CONV: replay every input group and convolve, one external IRQ.
// reg3[15:0]=words/group; reg3[23:16]=groups-1. No partial beats permitted.
module feature_cache_seq #(
    parameter ADDR_BITS=15,
    parameter FINISH_CYCLES=201
)(
    input wire clk, resetn,
    input wire [31:0] reg0, reg1, reg2, reg3,
    input wire [63:0] in_data,
    input wire [7:0] in_keep,
    input wire in_valid, in_last,
    output wire in_ready,
    output reg [31:0] core_reg0,
    output wire [31:0] core_reg1, core_reg2, core_reg3,
    output wire [63:0] core_data,
    output wire [7:0] core_keep,
    output wire core_valid, core_last,
    input wire core_ready, core_finish, core_idle,
    output wire task_finish
);
    localparam IDLE=0, LOAD=1, FSTART=2, FEED=3, FWAIT=4,
               FIDLE=5, CSTART=6, CWAIT=7, CIDLE=8, DONE=9, FAULT=10, ARM=11,
               CHECK_CONFIG=12, CHECK_PRODUCT=13;
    (* fsm_encoding="one_hot" *) reg [3:0] state;
    (* ram_style="block" *) reg [63:0] cache [0:(1<<ADDR_BITS)-1];
    reg [ADDR_BITS:0] written;
    reg [ADDR_BITS-1:0] read_addr;
    reg [63:0] read_data;
    reg read_valid, read_last;
    reg [15:0] issued, words;
    reg [7:0] last_group, group_index;
    reg [23:0] loaded_descriptor;
    reg cache_valid;
    reg [31:0] cfg, cfg1, cfg2;
    reg [8:0] finish_count;
    // Registered product: AXI descriptor -> words/groups -> product -> checks.
    // No multiplier/comparator/FSM path is allowed in one 200-MHz cycle.
    reg [23:0] requested_words;
    wire special=(reg0[24] && reg0[0]) || (reg0[25] && reg0[2]);
    wire active=(state!=IDLE);
    wire fetch=(state==FEED && (!read_valid || core_ready) && issued<words);
    wire [1:0] batch=(group_index==last_group)?2'd2:
                           ((group_index==0)?2'd0:2'd1);
    // Suppress special command strobes before the core can observe them.
    always @* begin
        if(active) begin
            core_reg0=(cfg & 32'h00ffe7f0) | ({30'd0,batch}<<11);
            if(state==FSTART) core_reg0[0]=1'b1;
            if(state==CSTART) core_reg0[2]=1'b1;
        end else core_reg0=special ? (reg0 & 32'h00fffff0) : (reg0 & 32'h00ffffff);
    end
    assign core_reg1=active ? {cfg1[31:8],(cfg1[7:0]+group_index)} : reg1;
    assign core_reg2=active ? cfg2 : reg2;
    // Shorten only INTERNAL completion holds; preserve the legacy 201-cycle IRQ.
    assign core_reg3=active ? 32'h80000000 : 32'd0;
    assign in_ready=(state==LOAD) ? 1'b1 : ((!active && !special)?core_ready:1'b0);
    assign core_data=active ? read_data : in_data;
    assign core_keep=active ? 8'hff : in_keep;
    assign core_valid=active ? (state==FEED && read_valid) : (!special && in_valid);
    assign core_last=active ? read_last : in_last;
    assign task_finish=(state==DONE) || (!active && !special && core_finish);
    // A single synchronous read port with an elastic output register. Data,
    // VALID and LAST are held together across arbitrary ready backpressure.
    always @(posedge clk) begin
        if(state==LOAD && in_valid && written<(1<<ADDR_BITS))
            cache[written[ADDR_BITS-1:0]]<=in_data;
        if(fetch) read_data<=cache[read_addr];
    end
    always @(posedge clk or negedge resetn) begin
        if(!resetn) begin
            state<=IDLE;written<=0;read_addr<=0;read_valid<=0;read_last<=0;
            issued<=0;words<=0;last_group<=0;group_index<=0;
            loaded_descriptor<=0;cache_valid<=0;cfg<=0;cfg1<=0;cfg2<=0;finish_count<=0;requested_words<=0;
        end else begin
            case(state)
            IDLE: begin
                read_valid<=0;finish_count<=0;
                if(special) begin
                    cfg<=reg0;cfg1<=reg1;cfg2<=reg2;
                    words<=reg3[15:0];last_group<=reg3[23:16];group_index<=0;
                    if(reg0[24]) cache_valid<=0;
                    written<=0;read_addr<=0;state<=CHECK_CONFIG;
                end
            end
            CHECK_CONFIG: begin
                requested_words<=words*({1'b0,last_group}+9'd1);
                if(words==0 || words>4096 || last_group>=128) state<=FAULT;
                else state<=CHECK_PRODUCT;
            end
            CHECK_PRODUCT: begin
                if(requested_words>(1<<ADDR_BITS)) state<=FAULT;
                else if(cfg[24] && cfg[0] && !cfg[25]) begin
                    loaded_descriptor<={last_group,words};state<=ARM;
                end else if(cfg[25] && cfg[2] && !cfg[24] && cache_valid &&
                            loaded_descriptor=={last_group,words} &&
                            ({1'b0,cfg1[7:0]}+{1'b0,last_group})<=255) state<=ARM;
                else state<=FAULT;
            end
            ARM: if(core_idle && !core_finish) state<=cfg[24]?LOAD:FSTART;
            LOAD: if(in_valid) begin
                written<=written+1'b1;
                if(in_keep!=8'hff || written>=requested_words || written>=(1<<ADDR_BITS)) begin
                    cache_valid<=0;state<=FAULT;
                end else if(in_last) begin
                    if(written+1'b1==requested_words) begin cache_valid<=1;state<=DONE;end
                    else begin cache_valid<=0;state<=FAULT;end
                end else if(written+1'b1==requested_words) begin cache_valid<=0;state<=FAULT;end
            end
            FSTART: begin state<=FEED;issued<=0;read_valid<=0;end
            FEED: begin
                if(!read_valid || core_ready) begin
                    read_valid<=fetch;
                    if(fetch) begin
                        issued<=issued+1'b1;read_addr<=read_addr+1'b1;
                        read_last<=(issued==words-1'b1);
                    end
                end
                if(read_valid && core_ready && read_last) state<=FWAIT;
            end
            FWAIT: if(core_finish) state<=FIDLE;
            FIDLE: if(core_idle && !core_finish) state<=CSTART;
            CSTART: state<=CWAIT;
            CWAIT: if(core_finish) state<=CIDLE;
            CIDLE: if(core_idle && !core_finish) begin
                if(group_index==last_group) state<=DONE;
                else begin group_index<=group_index+1'b1;state<=FSTART;end
            end
            DONE: if(finish_count==FINISH_CYCLES-1) begin state<=IDLE;finish_count<=0;end
                  else finish_count<=finish_count+1'b1;
            FAULT: begin cache_valid<=0;read_valid<=0;end // no IRQ; software timeout stops safely
            default: state<=FAULT;
            endcase
        end
    end
endmodule
