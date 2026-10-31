module  stream_tx(
        // system siganls
        input                   sclk                    ,       
        input                   s_rst_n                 ,       
        //
        output  wire    [63:0]  s_axis_s2mm_tdata       ,       
        output  wire            s_axis_s2mm_tvalid      ,       
        input                   s_axis_s2mm_tready      ,       
        output  wire            s_axis_s2mm_tlast       ,      
        // 
        output  wire            buffer_rd_en            ,       
        input           [63:0]  buffer_rd_data          ,
        input           [ 5:0]  state                   ,
        input           [12:0]  buffer_data_count       ,
        output  wire            read_finish                    
);

//========================================================================\
// =========== Define Parameter and Internal signals =========== 
//========================================================================/

reg     [12:0]                  tx_cnt                          ;       

//=============================================================================
//**************    Main Code   **************
//=============================================================================
// Only advertise data while the latched FIFO word count has not been
// exhausted.  This keeps TVALID/TLAST stable when the DMA applies
// back-pressure and prevents reading past the end of the FIFO.
assign  s_axis_s2mm_tvalid      =       state[2] && (tx_cnt < buffer_data_count);
assign  buffer_rd_en            =       s_axis_s2mm_tvalid & s_axis_s2mm_tready;
assign  s_axis_s2mm_tdata       =       buffer_rd_data;
assign  s_axis_s2mm_tlast       =       s_axis_s2mm_tvalid &&
                                        (tx_cnt == (buffer_data_count-1'b1));
assign  read_finish             =       s_axis_s2mm_tvalid &
                                        s_axis_s2mm_tready &
                                        s_axis_s2mm_tlast;

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                tx_cnt  <=      'd0;
        else if(s_axis_s2mm_tvalid == 1'b0)
                tx_cnt  <=      'd0;    
        else if(buffer_rd_en == 1'b1)
                tx_cnt  <=      tx_cnt + 1'b1;
end

endmodule
