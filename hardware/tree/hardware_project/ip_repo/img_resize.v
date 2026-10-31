module  img_resize(
        // system signals
        input                   sclk                    ,       
        input                   s_rst_n                 ,       
        // 
        input                   vid_ce_i                ,       
        input                   m_vsync_i               ,       
        input                   m_hsync_i               ,       
        input                   m_href_i                ,       
        input           [23:0]  m_data_i                ,       
        //
        output  wire            vid_ce_o                ,       
        output  wire            m_vsync_o               ,       
        output  wire            m_hsync_o               ,       
        output  reg             m_href_o                ,       
        output  wire    [23:0]  m_data_o                 
);

//========================================================================\
// =========== Define Parameter and Internal signals =========== 
//========================================================================/
localparam      OV5640_720P_W   =       'd1280                  ;


reg     [10:0]                  cnt_h                           ;       
reg     [ 9:0]                  cnt_v                           ;       

reg     [ 1:0]                  cnt_col                         ;       
reg     [ 1:0]                  cnt_row                         ;       

wire                            m_active                        ;       


//=============================================================================
//**************    Main Code   **************
//=============================================================================

assign  m_active        =       vid_ce_i & m_href_i;
assign  m_vsync_o       =       m_vsync_i;
assign  m_hsync_o       =       ~m_href_o;
assign  vid_ce_o        =       (m_href_o == 1'b0) ? 1'b1 : 
                                ((cnt_col == 'd0) ? vid_ce_i : 1'b0);
assign  m_data_o        =       {1'b0, m_data_i[7:1], 1'b0, m_data_i[15:9], 1'b0, m_data_i[23:17]};       // 通道顺序不一样, 每个通道除2是为了量化

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                cnt_h   <=      'd0;
        else if(m_hsync_i == 1'b1)
                cnt_h   <=      'd0;
        else if(m_active == 1'b1)
                cnt_h   <=      cnt_h + 1'b1;
end

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                cnt_v   <=      'd0;
        else if(m_vsync_i == 1'b1)
                cnt_v   <=      'd0;
        else if(m_active == 1'b1 && cnt_h == (OV5640_720P_W-1))
                cnt_v   <=      cnt_v + 1'b1;
end

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                cnt_col <=      'd0;
        else if(m_href_o == 1'b1 && vid_ce_i == 1'b1 && cnt_col >= 'd2)
                cnt_col <=      'd0;
        else if(m_href_o == 1'b1 && vid_ce_i == 1'b1)
                cnt_col <=      cnt_col + 1'b1;
end

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                cnt_row <=      'd0;
        else if(cnt_v >= 'd48 && cnt_v < ('d48+'d624) && m_active == 1'b1 && cnt_h == (OV5640_720P_W-1) && cnt_row >= 'd2)
                cnt_row <=      'd0;
        else if(cnt_v >= 'd48 && cnt_v < ('d48+'d624) && m_active == 1'b1 && cnt_h == (OV5640_720P_W-1))
                cnt_row <=      cnt_row + 1'b1;
end

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                m_href_o        <=      1'b0;
        else if(cnt_v >= 'd48 && cnt_v < ('d48+'d624) && cnt_row <= 'd1) begin
                if(cnt_h == 'd15 && m_active == 1'b1)
                        m_href_o        <=      1'b1;
                else if(cnt_h == ('d15+'d1248) && m_active == 1'b1)
                        m_href_o        <=      1'b0;
        end
        else
                m_href_o        <=      1'b0;
end

////////////////////////////////////////////////////////////////
/*reg     [ 8:0]  dbg_col_cnt;
reg     [ 8:0]  dbg_row_cnt;

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                dbg_col_cnt     <=      'd0;
        else if(m_href_o == 1'b0)
                dbg_col_cnt     <=      'd0;
        else if(m_href_o == 1'b1 && vid_ce_o == 1'b1)
                dbg_col_cnt     <=      dbg_col_cnt + 1'b1;
end

always  @(posedge sclk or negedge s_rst_n) begin
        if(s_rst_n == 1'b0)
                dbg_row_cnt     <=      'd0;
        else if(m_vsync_o == 1'b1)
                dbg_row_cnt     <=      'd0;
        else if(m_href_o == 1'b1 && vid_ce_o == 1'b1 && dbg_col_cnt == 'd415)
                dbg_row_cnt     <=      dbg_row_cnt + 1'b1;
end
*/

endmodule
