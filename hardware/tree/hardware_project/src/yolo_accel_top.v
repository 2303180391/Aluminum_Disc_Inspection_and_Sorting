module  yolo_accel_top(
        // system signals
        (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 sclk CLK" *)
        (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME sclk, FREQ_HZ 200000000, ASSOCIATED_BUSIF s_axis_mm2s:s_axis_s2mm, ASSOCIATED_RESET s_rst_n" *)
        input                   sclk                    ,       
        (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 s_rst_n RST" *)
        (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_rst_n, POLARITY ACTIVE_LOW" *)
        input                   s_rst_n                 ,       
        // Axi4-Lite Reg
        input           [31:0]  slave_lite_reg0         ,
        input           [31:0]  slave_lite_reg1         ,
        input           [31:0]  slave_lite_reg2         ,
        input           [31:0]  slave_lite_reg3         ,
        // Axi4-Stream Rx
        input           [63:0]  s_axis_mm2s_tdata       ,       
        input           [ 7:0]  s_axis_mm2s_tkeep       ,       
        input                   s_axis_mm2s_tvalid      ,       
        output  wire            s_axis_mm2s_tready      ,       
        input                   s_axis_mm2s_tlast       ,       
        // Axi4-Stream TX
        output  wire    [63:0]  s_axis_s2mm_tdata       ,       
        output  wire    [ 7:0]  s_axis_s2mm_tkeep       ,       
        output  wire            s_axis_s2mm_tvalid      ,       
        input                   s_axis_s2mm_tready      ,       
        output  wire            s_axis_s2mm_tlast       ,       
        // Generate Intr
        output  wire            task_finish                    
);
wire [31:0] cr0,cr1,cr2,cr3;
wire [63:0] cd;
wire [7:0] ck;
wire cv,cl,cready,cfinish,cidle;
feature_cache_seq cache_seq(
 .clk(sclk),.resetn(s_rst_n),
 .reg0(slave_lite_reg0),.reg1(slave_lite_reg1),.reg2(slave_lite_reg2),.reg3(slave_lite_reg3),
 .in_data(s_axis_mm2s_tdata),.in_keep(s_axis_mm2s_tkeep),
 .in_valid(s_axis_mm2s_tvalid),.in_last(s_axis_mm2s_tlast),.in_ready(s_axis_mm2s_tready),
 .core_reg0(cr0),.core_reg1(cr1),.core_reg2(cr2),.core_reg3(cr3),
 .core_data(cd),.core_keep(ck),.core_valid(cv),.core_last(cl),.core_ready(cready),
 .core_finish(cfinish),.core_idle(cidle),.task_finish(task_finish)
);
yolo_accel_core core(
 .sclk(sclk),.s_rst_n(s_rst_n),
 .slave_lite_reg0(cr0),.slave_lite_reg1(cr1),.slave_lite_reg2(cr2),.slave_lite_reg3(cr3),
 .s_axis_mm2s_tdata(cd),.s_axis_mm2s_tkeep(ck),.s_axis_mm2s_tvalid(cv),
 .s_axis_mm2s_tlast(cl),.s_axis_mm2s_tready(cready),
 .s_axis_s2mm_tdata(s_axis_s2mm_tdata),.s_axis_s2mm_tkeep(s_axis_s2mm_tkeep),
 .s_axis_s2mm_tvalid(s_axis_s2mm_tvalid),.s_axis_s2mm_tready(s_axis_s2mm_tready),
 .s_axis_s2mm_tlast(s_axis_s2mm_tlast),.task_finish(cfinish),.core_idle(cidle)
);
endmodule
