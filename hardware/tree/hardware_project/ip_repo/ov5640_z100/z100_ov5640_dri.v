//****************************************Copyright (c)***********************************//
//ԭ�Ӹ����߽�ѧƽ̨��www.yuanzige.com
//����֧�֣�http://www.openedv.com/forum.php
//�Ա����̣�https://zhengdianyuanzi.tmall.com
//��ע΢�Ź���ƽ̨΢�źţ�"����ԭ��"����ѻ�ȡZYNQ & FPGA & STM32 & LINUX���ϡ�
//��Ȩ���У�����ؾ���
//Copyright(C) ����ԭ�� 2023-2033
//All rights reserved
//----------------------------------------------------------------------------------------
// File name:           ov5640_dri
// Last modified Date:  2020/05/04 9:19:08
// Last Version:        V1.0
// Descriptions:        OV5640����ͷ����
//                      
//----------------------------------------------------------------------------------------
// Created by:          ����ԭ��
// Created date:        2019/05/04 9:19:08
// Version:             V1.0
// Descriptions:        The original version
//
//----------------------------------------------------------------------------------------
//****************************************************************************************//

module z100_ov5640_dri (
    input           clk             ,  //ʱ��
    input           rst_n           ,  //��λ�ź�,�͵�ƽ��Ч
    //����ͷ�ӿ� 
    input           cam_pclk        ,  //cmos ��������ʱ��
    input           cam_vsync       ,  //cmos ��ͬ���ź�
    input           cam_href        ,  //cmos ��ͬ���ź�
    input    [7:0]  cam_data        ,  //cmos ����  
    output          cam_rst_n       ,  //cmos ��λ�źţ��͵�ƽ��Ч
    output          cam_pwdn        ,  //cmos ��Դ����ģʽѡ���ź�
    output          cam_scl         ,  //cmos SCCB_SCL��
    inout           cam_sda         ,  //cmos SCCB_SDA��
    
    //����ͷ�ֱ������ýӿ�
    input    [12:0] cmos_h_pixel    ,  //ˮƽ����ֱ���
    input    [12:0] cmos_v_pixel    ,  //��ֱ����ֱ���
    input    [12:0] total_h_pixel   ,  //ˮƽ�����ش�С
    input    [12:0] total_v_pixel   ,  //��ֱ�����ش�С
    input           capture_start   ,  //ͼ��ɼ���ʼ�ź�
    output          cam_init_done   ,  //����ͷ��ʼ�����
    output  [15:0]  cam_sensor_id   ,
    output          cam_i2c_error   ,
    
    //�û��ӿ�
    output          cmos_frame_vsync,  //֡��Ч�ź�    
    output          cmos_frame_href ,  //����Ч�ź�
    output          cmos_frame_valid,  //������Чʹ���ź�
    output  [15:0]  cmos_frame_data    //��Ч����  
);

//parameter define
parameter SLAVE_ADDR = 7'h3c          ; //OV5640��������ַ7'h3c
parameter BIT_CTRL   = 1'b1           ; //OV5640���ֽڵ�ַΪ16λ  0:8λ 1:16λ
parameter CLK_FREQ   = 27'd50_000_000 ; //i2c_driģ�������ʱ��Ƶ�� 
parameter I2C_FREQ   = 18'd250_000    ; //I2C��SCLʱ��Ƶ��,������400KHz

//wire difine
wire        i2c_exec       ;  //I2C����ִ���ź�
wire [23:0] i2c_data       ;  //I2CҪ���õĵ�ַ������(��8λ��ַ,��8λ����)          
wire        i2c_done       ;  //I2C�Ĵ�����������ź�
wire        i2c_dri_clk    ;  //I2C����ʱ��
wire [ 7:0] i2c_data_r     ;  //I2C����������
wire        i2c_rh_wl      ;  //I2C��д�����ź�
wire        i2c_ack        ;  //ACK status is not used by the OV5640 setup sequencer

reg  [7:0]  debug_i2c_done_count;
reg  [15:0] debug_sensor_id;
reg         debug_i2c_error;
reg  [26:0] sensor_power_count;

// Give the sensor an explicit power/reset sequence after PL configuration.
// The original reference tied these pins permanently active, which does not
// guarantee a reset when only the FPGA bitstream is reprogrammed over JTAG.
localparam [26:0] PWDN_RELEASE_COUNT = CLK_FREQ / 27'd500;   // 2 ms
localparam [26:0] RESET_RELEASE_COUNT = (CLK_FREQ / 27'd1000) * 27'd7;  // 7 ms
localparam [26:0] CFG_RELEASE_COUNT = (CLK_FREQ / 27'd1000) * 27'd27;   // 27 ms
reg sensor_cfg_rst_n;
wire i2c_logic_reset_n;
xpm_cdc_async_rst #(.DEST_SYNC_FF(3), .INIT_SYNC_FF(0),
    .RST_ACTIVE_HIGH(0)) u_i2c_logic_reset_sync (
    .src_arst(sensor_cfg_rst_n), .dest_clk(i2c_dri_clk), .dest_arst(i2c_logic_reset_n)
);


//*****************************************************
//**                    main code                      
//*****************************************************

//��Դ����ģʽѡ�� 0������ģʽ 1����Դ����ģʽ
// Power-down is released first, reset is released 5 ms later, and the SCCB
// controller is held reset for another 20 ms before its own startup delay.
always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
        sensor_power_count <= 27'd0;
    else if (sensor_power_count < CFG_RELEASE_COUNT)
        sensor_power_count <= sensor_power_count + 1'b1;
end

assign  cam_pwdn  = (sensor_power_count < PWDN_RELEASE_COUNT);
assign  cam_rst_n = (sensor_power_count >= RESET_RELEASE_COUNT);
// Register the power-sequence decode before it reaches asynchronous reset
// pins. A multi-bit counter comparator can glitch even when its final value
// is monotonic. Release is delayed by one 50 MHz cycle (20 ns).
always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
        sensor_cfg_rst_n <= 1'b0;
    else if (sensor_power_count >= CFG_RELEASE_COUNT)
        sensor_cfg_rst_n <= 1'b1;
end
assign  cam_sensor_id = debug_sensor_id;
assign  cam_i2c_error = debug_i2c_error;

// Preserve the first two SCCB read results (chip ID high/low) and remember
// any NACK.  The original vendor sequencer otherwise reports init_done even
// when the sensor did not acknowledge its register transactions.
always @(posedge i2c_dri_clk or negedge i2c_logic_reset_n) begin
    if (!i2c_logic_reset_n) begin
        debug_i2c_done_count <= 8'd0;
        debug_sensor_id      <= 16'd0;
        debug_i2c_error      <= 1'b0;
    end else if (i2c_done) begin
        if (i2c_ack)
            debug_i2c_error <= 1'b1;

        if (debug_i2c_done_count == 8'd0)
            debug_sensor_id[15:8] <= i2c_data_r;
        else if (debug_i2c_done_count == 8'd1)
            debug_sensor_id[7:0] <= i2c_data_r;

        if (debug_i2c_done_count != 8'hff)
            debug_i2c_done_count <= debug_i2c_done_count + 1'b1;
    end
end
    
//I2C����ģ��
z100_ov5640_i2c_cfg u_i2c_cfg(
    .clk                (i2c_dri_clk),
    .rst_n              (i2c_logic_reset_n),
            
    .i2c_exec           (i2c_exec),
    .i2c_data           (i2c_data),
    .i2c_rh_wl          (i2c_rh_wl),        //I2C��д�����ź�
    .i2c_done           (i2c_done), 
    .i2c_data_r         (i2c_data_r),   
                
    .cmos_h_pixel       (cmos_h_pixel),     //CMOSˮƽ�������ظ���
    .cmos_v_pixel       (cmos_v_pixel) ,    //CMOS��ֱ�������ظ���
    .total_h_pixel      (total_h_pixel),    //ˮƽ�����ش�С
    .total_v_pixel      (total_v_pixel),    //��ֱ�����ش�С
        
    .init_done          (cam_init_done) 
    );    

//I2C����ģ��
z100_ov5640_i2c_dri #(
    .SLAVE_ADDR         (SLAVE_ADDR),       //��������
    .CLK_FREQ           (CLK_FREQ  ),              
    .I2C_FREQ           (I2C_FREQ  ) 
    )
u_i2c_dr(
    .clk                (clk),
    .rst_n              (sensor_cfg_rst_n),

    .i2c_exec           (i2c_exec  ),   
    .bit_ctrl           (BIT_CTRL  ),   
    .i2c_rh_wl          (i2c_rh_wl),        //�̶�Ϊ0��ֻ�õ���IIC������д����   
    .i2c_addr           (i2c_data[23:8]),   
    .i2c_data_w         (i2c_data[7:0]),   
    .i2c_data_r         (i2c_data_r),   
    .i2c_done           (i2c_done  ),
    .i2c_ack            (i2c_ack   ),
    
    .scl                (cam_scl   ),   
    .sda                (cam_sda   ),   

    .dri_clk            (i2c_dri_clk)       //I2C����ʱ��
    );

//CMOSͼ�����ݲɼ�ģ��
z100_ov5640_capture u_cmos_capture_data(      //ϵͳ��ʼ�����֮���ٿ�ʼ�ɼ����� 
    .rst_n              (capture_start),
    
    .cam_pclk           (cam_pclk),
    .cam_vsync          (cam_vsync),
    .cam_href           (cam_href),
    .cam_data           (cam_data),         
    
    .cmos_frame_vsync   (cmos_frame_vsync),
    .cmos_frame_href    (cmos_frame_href ),
    .cmos_frame_valid   (cmos_frame_valid), //������Чʹ���ź�
    .cmos_frame_data    (cmos_frame_data )  //��Ч���� 
    );

endmodule

