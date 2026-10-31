#ifndef YOLO_ACCEL_CTRL_H
#define YOLO_ACCEL_CTRL_H

#include "xil_io.h"
#include "xil_printf.h"
#include "xparameters.h"	/* SDK generated parameters */
#include "dma_ctrl.h"
#include "yolo_load_param.h"
#include "sigmoid_exp_table.h"

#define	S_IDLE			0
#define S_FEATURE_CONV	1
#define	S_DMA_RX		2
#define	S_FINISH		3

#define	Lite_Reg0			XPAR_AXI4_LITE_V1_0_0_BASEADDR
#define	Lite_Reg1			XPAR_AXI4_LITE_V1_0_0_BASEADDR+0x4
#define	Lite_Reg2			XPAR_AXI4_LITE_V1_0_0_BASEADDR+0x8

#define	BOX_X1_ADDR			XPAR_AXI4_LITE_BOX_V1_0_0_BASEADDR
#define	BOX_Y1_ADDR			XPAR_AXI4_LITE_BOX_V1_0_0_BASEADDR+0x4
#define	BOX_X2_ADDR			XPAR_AXI4_LITE_BOX_V1_0_0_BASEADDR+0x8
#define	BOX_Y2_ADDR			XPAR_AXI4_LITE_BOX_V1_0_0_BASEADDR+0xC
//////////////////////////////////////////////////////////////////
// Define Command
#define	WRITE_BIAS			0x21
#define	WRITE_LEAKY_RELU	0x31
#define	WRITE_WEIGHT		0x11
#define	WRITE_FEATURE		0x01

#define	READ_START			0x02
#define	CONV_START			0x04
#define	UPSAMPLE_START		0x08

#define	CONV_TYPE1			0x40
#define	CONV_TYPE3			0x00
#define	IS_PADDING			0x80
#define	IS_POOL				0x100
#define	POOL_STRIDE1		0x2000

#define	SITE_FIRST			0x000
#define	SITE_MIDDLE			0x200
#define	SITE_LAST			0x400
#define SITE_ALL			0x600

#define	BATCH_FIRST			0x0000
#define	BATCH_MIDDLE		0x0800
#define	BATCH_LAST			0x1000

#define SET_COL_TYPE(x)     (x<<14)

#define	SET_ROW_NUM(x)		((x-1)<<17)

//////////////////////////////////////////////////////////////////

#define	PL_BUFFER_LEN		4096

#define SET_MULT_BUFFER_RDADDR(x,y,z)		((x<<16)|(y<<8)|(z))
#define SET_SHIFT_ZP(x,y,z)					((x<<24)|(y<<8)|(z))

#define	L13_ADDR			0x3200000
#define	L15_ADDR			0x3300000
#define	L22_ADDR			0x3400000

//////////////////////////////////////////////////////////////////
extern int layers_config[15][9];

extern u8  conv_type;
extern u16 ch_in;
extern u16 ch_out;
extern u8  is_pool;
extern u16 feature_size;
extern u16 mult;
extern u8  shift;
extern u8  zp_in;
extern u8  zp_out;

extern u8  pool_stride;
extern u8  col_type;
extern u8  ch_in_batch_cnt;
extern u8  ch_in_batch_cnt_end;
extern u8  ch_out_batch_cnt;
extern u8  ch_out_batch_cnt_end;
extern u8  tx_cnt;
extern u8  tx_cnt_end;
extern u8  batch_cnt;
extern u8  batch_cnt_end;
extern u8  pl_buffer_row_num;
extern u8  tx_row_num;
extern u8  tx_last_row_num;
extern u8  rx_left_row_num;

extern u8  bias_buffer_rd_addr;
extern u8  weight_buffer_rd_addr;
extern u32 weight_tx_addr;
extern u32 bias_tx_addr;
extern u32 act_tx_addr;
extern u32 weight_batch_cnt;
extern u32 layers_tx_addr[15];
extern u32 layers_rx_addr[15];
extern u32 tx_base_addr;
extern u32 rx_base_addr;

extern u32 reg_cmd;
extern u32 tx_addr;
extern u32 tx_len;
extern u32 rx_addr;
extern u32 rx_len;
extern u32 bias_len;
extern u32 weight_len;

extern char state;
extern volatile char InterruptProcessed;

void update_tx_info();
void update_rx_info();
void update_cmd();
void update_cnt_info();
void yolo_forward();
void yolo_forward_init(int layer_config[], int i);
int wait_pl_finish();
void upsample_task();
void yolo_detect();

#endif
