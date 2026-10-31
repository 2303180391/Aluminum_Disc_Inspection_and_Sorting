#include "yolo_accel_ctrl.h"
#include "yolo_diag.h"
#include "score_log.h"
#include "candidate_log.h"
/* Instrument command submissions without changing model/coordinate math. */
#define Xil_Out32(addr, value) diag_write_reg((addr), (value))

#define	IMG_W	1248
#define	IMG_H	624
#define DETECT_THRESHOLD 0.50f
#define PL_WAIT_TIMEOUT  0x20000000U

/* Independent CSQ1 experiment. Layer inputs fit in a 32768x64-bit BRAM
 * cache for full-map layers <=52. Large early layers/upsample stay legacy.
 * One descriptor executes all input groups for ONE output group. */
#define CACHE_LOAD 0x01000001U
#define CACHE_CONVOLVE 0x02000004U
#define CACHE_REG3 (Lite_Reg0+12U)
#define CACHE_SIGNATURE 0x43535131U
static int cache_layer(int i) {
    return i!=12 && layers_config[i][4]<=52 &&
        (u32)layers_config[i][1]*(u32)layers_config[i][4]*(u32)layers_config[i][4]<=262144U;
}
static void cache_forward_layer(void) {
    u32 group;
    u32 tile_groups=(ch_in==384)?192U:256U;
    Xil_Out32(CACHE_REG3,((u32)(ch_in_batch_cnt_end-1U)<<16) |
                              (u32)feature_size*(u32)feature_size);
    Xil_Out32(Lite_Reg2, SET_SHIFT_ZP(shift,zp_out,zp_in));
    Xil_Out32(Lite_Reg0,CACHE_LOAD);
    DMA_Tx(tx_base_addr,(u32)ch_in*(u32)feature_size*(u32)feature_size);
    wait_pl_finish();
    Xil_Out32(Lite_Reg1,SET_MULT_BUFFER_RDADDR(mult,0U,0U));
    Xil_Out32(Lite_Reg0,WRITE_BIAS);DMA_Tx(bias_tx_addr,bias_len);wait_pl_finish();
    Xil_Out32(Lite_Reg0,WRITE_LEAKY_RELU);DMA_Tx(act_tx_addr,256U);wait_pl_finish();
    for(group=0;group<ch_out_batch_cnt_end;group++) {
        u32 weight_group=group*(u32)ch_in_batch_cnt_end;
        ch_out_batch_cnt=(u8)group;
        ch_in_batch_cnt=0U;
        if(weight_group%tile_groups==0U) {
            Xil_Out32(Lite_Reg0,WRITE_WEIGHT | (conv_type?CONV_TYPE1:0U));
            DMA_Tx(weight_tx_addr+weight_batch_cnt*weight_len,weight_len);
            wait_pl_finish();weight_batch_cnt++;
        }
        bias_buffer_rd_addr=(u8)group;
        weight_buffer_rd_addr=(u8)(weight_group%tile_groups);
        Xil_Out32(Lite_Reg1,SET_MULT_BUFFER_RDADDR(mult,bias_buffer_rd_addr,weight_buffer_rd_addr));
        update_cmd();
        Xil_Out32(Lite_Reg0,reg_cmd|CACHE_CONVOLVE);wait_pl_finish();
        ch_in_batch_cnt=ch_in_batch_cnt_end-1U;
        update_cmd();update_rx_info();
        Xil_Out32(Lite_Reg0,reg_cmd|READ_START);DMA_Rx(rx_addr,rx_len);wait_pl_finish();
        Xil_DCacheInvalidateRange(rx_addr,rx_len);
    }
}

u8  conv_type = 0;
u16 ch_in = 0;
u16 ch_out = 0;
u8  is_pool = 0;
u16 feature_size = 0;
u16 mult = 0;
u8  shift = 0;
u8  zp_in = 0;
u8  zp_out = 0;
u8  pool_stride = 0;
u8  col_type = 0;
u8  ch_in_batch_cnt = 0;
u8  ch_in_batch_cnt_end = 0;
u8  ch_out_batch_cnt = 0;
u8  ch_out_batch_cnt_end = 0;
u8  tx_cnt = 0;
u8  tx_cnt_end = 0;
u8  batch_cnt = 0;
u8  batch_cnt_end = 0;
u8  pl_buffer_row_num = 0;
u8  tx_row_num = 0;
u8  tx_last_row_num = 0;
u8  rx_left_row_num = 0;
u8  bias_buffer_rd_addr = 0;
u8  weight_buffer_rd_addr = 0;
u32 weight_tx_addr = 0;
u32 bias_tx_addr = 0;
u32 act_tx_addr = 0;
u32 weight_batch_cnt = 0;
u32 tx_base_addr = 0;
u32 rx_base_addr = 0;
u32 reg_cmd = 0;
u32 tx_addr = 0;
u32 tx_len = 0;
u32 rx_addr = 0;
u32 rx_len = 0;
u32 bias_len = 0;
u32 weight_len = 0;
char state = S_IDLE;
volatile char InterruptProcessed = FALSE;

int layers_config[15][9] = {
    /* type, ch_in, ch_out, pool, size, mult, shift, zp_in, zp_out */
    {0, 8, 16, 1, 416, 23547, 7, 0, 115},
    {0, 16, 32, 1, 208, 22352, 7, 21, 151},
    {0, 32, 64, 1, 104, 25493, 7, 32, 117},
    {0, 64, 128, 1, 52, 20024, 7, 21, 132},
    {0, 128, 256, 0, 26, 16716, 8, 26, 128},
    {0, 128, 256, 1, 26, 16716, 8, 26, 128},
    {0, 256, 512, 1, 13, 23750, 10, 41, 143},
    {0, 512, 1024, 0, 13, 22040, 11, 29, 147},
    {1, 1024, 256, 0, 13, 29565, 9, 45, 91},
    {0, 256, 512, 0, 13, 18410, 8, 19, 111},
    {1, 512, 24, 0, 13, 24639, 7, 29, 184},
    {1, 256, 128, 0, 13, 20814, 7, 19, 110},
    {0, 128, 128, 0, 26, 0, 0, 0, 0},
    {0, 384, 256, 0, 26, 26300, 9, 32, 127},
    {1, 256, 24, 0, 26, 17592, 6, 24, 148}
};

u32 layers_tx_addr[15] = {
	0x1000000, 0x3000000, 0x3100000, 0x3000000, 0x3100000, 0x3100000, 0x3000000, 0x3100000, 0x3000000, L13_ADDR, 0x3000000, L13_ADDR, 0x3000000, 0x3100000, 0x3000000
};

u32 layers_rx_addr[15] = {
	0x3000000, 0x3100000, 0x3000000, 0x3100000, 0x3115200, 0x3000000, 0x3100000, 0x3000000, L13_ADDR, 0x3000000, L15_ADDR, 0x3000000, 0x3100000, 0x3000000, L22_ADDR
};

void update_tx_info()
{
	if(feature_size <= 52)
		tx_len = (feature_size*feature_size) << 3;
	else if(tx_cnt == 0) {
		tx_len = (pl_buffer_row_num * feature_size)<<3;
	}
	else if(tx_cnt == tx_cnt_end - 1)
		tx_len = (tx_last_row_num * feature_size)<<3;

	tx_addr = tx_base_addr + ((tx_row_num * feature_size)<<3)*tx_cnt + ((feature_size*feature_size)<<3)*ch_in_batch_cnt;
}

void update_rx_info()
{
	rx_addr = rx_addr + rx_len;

	if(tx_cnt == 0 && ch_out_batch_cnt == 0)
		rx_addr = rx_base_addr;

	if(feature_size <= 52) {
		if(is_pool == 0) {
			rx_len = (feature_size*feature_size) << 3;
			rx_left_row_num = 0;
		}
		else if(pool_stride == 0) {
			rx_len = ((feature_size>>1)* (feature_size>>1)) << 3;
			rx_left_row_num = 0;
		}
		else {
			rx_len = (feature_size*feature_size) << 3;
			rx_left_row_num = 0;
		}

	}
	else if(tx_cnt == 0) {
		rx_len = (((pl_buffer_row_num-1)>>1)<<3)*(feature_size>>1);
		rx_left_row_num = (pl_buffer_row_num-1)%2;
	}
	else if(tx_cnt == tx_cnt_end-1)	{
		rx_len = (((tx_last_row_num-1+rx_left_row_num)>>1)<<3)*(feature_size>>1);
	}
	else {
		rx_len = (((pl_buffer_row_num-2+rx_left_row_num)>>1)<<3)*(feature_size>>1);
		rx_left_row_num = (pl_buffer_row_num-2+rx_left_row_num)%2;
	}

}

void update_cmd()
{
	if(conv_type == 0)
		reg_cmd	= CONV_TYPE3|IS_PADDING;
	else
		reg_cmd = CONV_TYPE1;

	if(is_pool == 1)
		reg_cmd |= IS_POOL;
	if(pool_stride == 1)
		reg_cmd |= POOL_STRIDE1;

	if(feature_size <= 52)
		reg_cmd |= SITE_ALL;
	else if(tx_cnt == 0)
		reg_cmd |= SITE_FIRST;
	else if(tx_cnt == tx_cnt_end - 1)
		reg_cmd |= SITE_LAST;
	else
		reg_cmd |= SITE_MIDDLE;

	if(ch_in_batch_cnt == (ch_in_batch_cnt_end-1))
		reg_cmd |= BATCH_LAST;
	else if(ch_in_batch_cnt == 0)
		reg_cmd |= BATCH_FIRST;
	else
		reg_cmd |= BATCH_MIDDLE;

	reg_cmd = reg_cmd | SET_COL_TYPE(col_type);
	
	if(feature_size <= 52)
		reg_cmd = reg_cmd | SET_ROW_NUM(feature_size);
	else if(tx_cnt == tx_cnt_end-1)
		reg_cmd = reg_cmd | SET_ROW_NUM(tx_last_row_num);
	else
		reg_cmd = reg_cmd | SET_ROW_NUM(pl_buffer_row_num);
}

void update_cnt_info()
{
	if(ch_in_batch_cnt == ch_in_batch_cnt_end-1) {
		ch_in_batch_cnt = 0;
		if(tx_cnt == tx_cnt_end-1) {
			tx_cnt = 0;
			ch_out_batch_cnt++;
		}
		else
			tx_cnt++;
	}
	else
		ch_in_batch_cnt++;
//	if(tx_cnt == tx_cnt_end-1){
//		tx_cnt = 0;
////		if(ch_out_batch_cnt == ch_out_batch_cnt_end-1)
////			ch_out_batch_cnt = 0;
////		else
//		ch_out_batch_cnt++;
//	}
//	else
//		tx_cnt++;

}


int wait_pl_finish()
{
	/* On failure diag_wait_task never returns, even if callers ignore status. */
	return diag_wait_task();
}

void yolo_forward_init(int layer_config[], int i)
{
	diag_layer_begin((u32)i);
	conv_type = layer_config[0];
	ch_in = layer_config[1];
	ch_out = layer_config[2];
	is_pool = layer_config[3];
	feature_size = layer_config[4];
	mult = layer_config[5];
	shift = layer_config[6];
	zp_in = layer_config[7];
	zp_out = layer_config[8];
	if(i == 6)		// 判断当前执行的层是否为Layer10,Layer11
		pool_stride	= 1;
	else
		pool_stride = 0;

	///////////////////////////////////////////////////////////////////
	// 获取计数器相关的值
	tx_cnt = 0;
	ch_out_batch_cnt = 0;
	ch_out_batch_cnt_end = ch_out>>3;

	ch_in_batch_cnt = 0;
	ch_in_batch_cnt_end = ch_in>>3;

	if(feature_size <= 52) {
		pl_buffer_row_num = feature_size;
		tx_row_num = feature_size;
		tx_last_row_num = 0;
	}
	else {
		pl_buffer_row_num = PL_BUFFER_LEN / feature_size; 	// PL端数据BUFFER能存储特征图数据的最大行数量
		tx_row_num = pl_buffer_row_num - 2;					// PS端每次实际发送特征图数据的行数量
		tx_last_row_num = feature_size % tx_row_num;		// PS端最后一次发送特征图数据的行数量
	}

	if(tx_last_row_num == 0)
		tx_cnt_end = feature_size / tx_row_num;
	else
		tx_cnt_end = feature_size / tx_row_num + 1;

	///////////////////////////////////////////////////////////////////
	bias_buffer_rd_addr = 0;
	weight_buffer_rd_addr = 0;
	if(i<12) {
		weight_tx_addr = WeightAddr[i];
		bias_tx_addr = BiasAddr[i];
		act_tx_addr = ActAddr[i];
	}
	else {
		weight_tx_addr = WeightAddr[i-1];
		bias_tx_addr = BiasAddr[i-1];
		act_tx_addr = ActAddr[i-1];
	}
	weight_batch_cnt = 0;
	bias_len = ch_out*4;
	if(conv_type == 0) {
		if(i == 13)
			weight_len = 110592; //384*8*4*9 = 110592;
		else if(ch_in * ch_out <= 16384)
			weight_len = ch_in * ch_out * 9;
		else
			weight_len = 16384 * 9;
	}
	else {
		if(ch_in * ch_out <= 16384)
			weight_len = ch_in * ch_out;
		else
			weight_len = 16384;
	}
	///////////////////////////////////////////////////////////////////
	tx_base_addr = layers_tx_addr[i];
	rx_base_addr = layers_rx_addr[i];
	///////////////////////////////////////////////////////////////////
	if(feature_size == 416)
		col_type = 0;
	else if(feature_size == 208)
		col_type = 1;
	else if(feature_size == 104)
		col_type = 2;
	else if(feature_size == 52)
		col_type = 3;
	else if(feature_size == 26)
		col_type = 4;
	else if(feature_size == 13)
		col_type = 5;
}

void yolo_forward()
{
	/* Refuse an old bitstream rather than executing an unsupported command. */
	if(Xil_In32(CACHE_REG3)!=CACHE_SIGNATURE) diag_fail(E_BAD_TRANSFER);
	/* Isolate repeated layer0 from state left by later network layers. */
	for(int i=0; i<15; i++) {

		yolo_forward_init(layers_config[i], i);

		if(cache_layer(i)) {
			cache_forward_layer();
		}
		else if(i==12)	// UpSample
			upsample_task();
		else {
			state = S_IDLE;
			while(state != S_FINISH) {
				switch(state) {
					case S_IDLE:
						Xil_Out32(Lite_Reg2, SET_SHIFT_ZP(shift,zp_out, zp_in));
						Xil_Out32(Lite_Reg1, SET_MULT_BUFFER_RDADDR(mult, bias_buffer_rd_addr, weight_buffer_rd_addr));
						Xil_Out32(Lite_Reg0, WRITE_BIAS);			// 发送bias数据
						DMA_Tx(bias_tx_addr, bias_len);
						wait_pl_finish();

						Xil_Out32(Lite_Reg0, WRITE_LEAKY_RELU);		// 发送LeakyRelu数据
						DMA_Tx(act_tx_addr, 256);
						wait_pl_finish();
						if(conv_type == 0)
							Xil_Out32(Lite_Reg0, WRITE_WEIGHT);					// 发送卷积3x3的Weight数据
						else
							Xil_Out32(Lite_Reg0, WRITE_WEIGHT|CONV_TYPE1);		// 发送卷积1x1Weight数据
						DMA_Tx(weight_tx_addr, weight_len);
						wait_pl_finish();

						state = S_FEATURE_CONV;
						break;

					case S_FEATURE_CONV:
						update_tx_info();
						update_cmd();

						Xil_Out32(Lite_Reg0, reg_cmd|WRITE_FEATURE);		// 发送Feature数据
						DMA_Tx(tx_addr, tx_len);
						wait_pl_finish();
						Xil_Out32(Lite_Reg0, reg_cmd|CONV_START);		// 卷积计算
						wait_pl_finish();


						if(ch_in_batch_cnt == ch_in_batch_cnt_end-1)
							state = S_DMA_RX;
						else {
							update_cnt_info();
							state = S_FEATURE_CONV;
							if(i==13)
								weight_buffer_rd_addr=ch_in_batch_cnt+ch_out_batch_cnt*ch_in_batch_cnt_end - (weight_batch_cnt*192);
							else
								weight_buffer_rd_addr=ch_in_batch_cnt+ch_out_batch_cnt*ch_in_batch_cnt_end - (weight_batch_cnt<<8);
							Xil_Out32(Lite_Reg1, SET_MULT_BUFFER_RDADDR(mult, bias_buffer_rd_addr, weight_buffer_rd_addr));
						}
						break;


					case S_DMA_RX:
						update_rx_info();
						Xil_Out32(Lite_Reg0, reg_cmd|READ_START);	// 将PL端的数据传至PS端
						DMA_Rx(rx_addr, rx_len);					// 9行数据+1行填充=10行数据 --->卷积后，8个416的数据量---》经过池化后，变成4*208---> 总共8个通道，即最终数据量4*208*8=6656
						wait_pl_finish();
						Xil_DCacheInvalidateRange(rx_addr, rx_len);

						if(tx_cnt == tx_cnt_end-1 && ch_out_batch_cnt == ch_out_batch_cnt_end-1 && ch_in_batch_cnt == ch_in_batch_cnt_end-1)
							state = S_FINISH;
						else {
							state = S_FEATURE_CONV;
							update_cnt_info();
							if(i==13) {
								if((ch_out_batch_cnt*ch_in_batch_cnt_end)%192==0 && ch_in_batch_cnt == 0 && tx_cnt == 0) {
									weight_tx_addr = weight_tx_addr + weight_len;
									Xil_Out32(Lite_Reg0, WRITE_WEIGHT);
									DMA_Tx(weight_tx_addr, weight_len);
									wait_pl_finish();
									weight_batch_cnt++;
								}
								weight_buffer_rd_addr=ch_in_batch_cnt+ch_out_batch_cnt*ch_in_batch_cnt_end - (weight_batch_cnt*192);
							}
							else {
								if((ch_out_batch_cnt*ch_in_batch_cnt_end)%256==0 && ch_in_batch_cnt == 0 && tx_cnt == 0){
									weight_tx_addr = weight_tx_addr + weight_len;
									if(conv_type == 0)
										Xil_Out32(Lite_Reg0, WRITE_WEIGHT);					// 发送卷积3x3的Weight数据
									else
										Xil_Out32(Lite_Reg0, WRITE_WEIGHT|CONV_TYPE1);		// 发送卷积1x1Weight数据
									DMA_Tx(weight_tx_addr, weight_len);
									wait_pl_finish();
									weight_batch_cnt++;
								}
								weight_buffer_rd_addr=ch_in_batch_cnt+ch_out_batch_cnt*ch_in_batch_cnt_end - (weight_batch_cnt<<8);
							}
							bias_buffer_rd_addr = ch_out_batch_cnt;
							Xil_Out32(Lite_Reg1, SET_MULT_BUFFER_RDADDR(mult, bias_buffer_rd_addr, weight_buffer_rd_addr));
						}
						break;


					case S_FINISH: break;

					default: break;
				}
			}
		}
		diag_layer_done((u32)i);
	}
}

void upsample_task()
{
	tx_addr = tx_base_addr;
	tx_len = 0x548;

	rx_addr = rx_base_addr;
	rx_len = 0x1520;
	Xil_Out32(Lite_Reg2, SET_SHIFT_ZP(shift,zp_out, zp_in));
	for(int i=0; i<16; i++) {
		Xil_Out32(Lite_Reg0, WRITE_FEATURE);		// 发送Feature数据
		DMA_Tx(tx_addr, tx_len);
		wait_pl_finish();

		Xil_Out32(Lite_Reg0, UPSAMPLE_START);		// UpSample
		DMA_Rx(rx_addr, rx_len);
		wait_pl_finish();

		tx_addr = tx_addr + tx_len;
		rx_addr = rx_addr + rx_len;
	}
}

void yolo_detect()
{
	////////////////////////////////////////
	// Layer15 解析
	u8 *layer15_addr;
	u8 p_obj, p_class, p_obj_max=0, p_class_max=0;
	u16 layer15_score=0, layer15_score_max=0;
	u8 layer15_kmax=0, layer15_rmax=0, layer15_cmax=0;
	float layer15_score_f;

	for(u8 k=0;k<3;k++) {
		for(u8 r=0;r<13;r++) {
			for(u8 c=0;c<13;c++) {
				if(k==0) {
					layer15_addr = (u8 *)(UINTPTR)(L15_ADDR+4+(c<<3)+r*104);
					p_obj = *layer15_addr;
					p_class = *(layer15_addr+1);
				}
				else if(k==1) {
					layer15_addr = (u8 *)(UINTPTR)(L15_ADDR+2+(c<<3)+r*104+1352);
					p_obj = *layer15_addr;
					p_class = *(layer15_addr+1);
				}
				else {
					layer15_addr = (u8 *)(UINTPTR)(L15_ADDR+(c<<3)+r*104+2704);
					p_obj = *layer15_addr;
					p_class = *(layer15_addr+1);
				}
				layer15_score = p_obj*p_class;
				if(layer15_score >= layer15_score_max) {
					layer15_score_max = layer15_score;
					layer15_kmax = k;
					layer15_rmax = r;
					layer15_cmax = c;
					p_obj_max = p_obj;
					p_class_max = p_class;
				}
			}
		}
	}

	layer15_score_f = l15_sigmoid_table[p_obj_max]*l15_sigmoid_table[p_class_max];
	////////////////////////////////////////
	// Layer22 解析
	u8 *layer22_addr;
	u16 layer22_score=0, layer22_score_max=0;
	u8 layer22_kmax=0, layer22_rmax=0, layer22_cmax=0;
	float layer22_score_f;

	for(u8 k=0;k<3;k++) {
		for(u8 r=0;r<26;r++) {
			for(u8 c=0;c<26;c++) {
				if(k==0) {
					layer22_addr = (u8 *)(UINTPTR)(L22_ADDR+4+(c<<3)+r*208);
					p_obj = *layer22_addr;
					p_class = *(layer22_addr+1);
				}
				else if(k==1) {
					layer22_addr = (u8 *)(UINTPTR)(L22_ADDR+2+(c<<3)+r*208+5408);
					p_obj = *layer22_addr;
					p_class = *(layer22_addr+1);
				}
				else {
					layer22_addr = (u8 *)(UINTPTR)(L22_ADDR+(c<<3)+r*208+10816);
					p_obj = *layer22_addr;
					p_class = *(layer22_addr+1);
				}
				layer22_score = p_obj*p_class;
				if(layer22_score >= layer22_score_max) {
					layer22_score_max = layer22_score;
					layer22_kmax = k;
					layer22_rmax = r;
					layer22_cmax = c;
					p_obj_max = p_obj;
					p_class_max = p_class;
				}
			}
		}
	}
	layer22_score_f = l22_sigmoid_table[p_obj_max]*l22_sigmoid_table[p_class_max];
	candidate_log(layer15_score_f,layer22_score_f,layer15_kmax,layer15_rmax,layer15_cmax,layer22_kmax,layer22_rmax,layer22_cmax);

	if(layer15_score_f < DETECT_THRESHOLD && layer22_score_f < DETECT_THRESHOLD) {
		if(score_hold_low(layer15_score_f,layer22_score_f)) return;
		Xil_Out32(BOX_X1_ADDR, 0);
		Xil_Out32(BOX_Y1_ADDR, 0);
		Xil_Out32(BOX_X2_ADDR, 0);
		Xil_Out32(BOX_Y2_ADDR, 0);
		return;
	}

	float box_x, box_y, box_w, box_h;
	u8 data_temp;

	if(layer22_score_f > layer15_score_f) {
		if(layer22_kmax==0) {
			layer22_addr = (u8 *)(UINTPTR)((layer22_cmax<<3)+layer22_rmax*208+L22_ADDR);
			data_temp = *layer22_addr;
			box_x = (l22_sigmoid_table[data_temp] + layer22_cmax)/26;
			data_temp = *(layer22_addr+1);
			box_y = (l22_sigmoid_table[data_temp] + layer22_rmax)/26;
			data_temp = *(layer22_addr+2);
			box_w = l22_w0_table[data_temp];
			data_temp = *(layer22_addr+3);
			box_h = l22_h0_table[data_temp];
		}
		else if(layer22_kmax==1) {
			layer22_addr = (u8 *)(UINTPTR)(6+(layer22_cmax<<3)+layer22_rmax*208+L22_ADDR);
			data_temp = *layer22_addr;
			box_x = (l22_sigmoid_table[data_temp] + layer22_cmax)/26;
			data_temp = *(layer22_addr+1);
			box_y = (l22_sigmoid_table[data_temp] + layer22_rmax)/26;
			data_temp = *(layer22_addr+5402);
			box_w = l22_w1_table[data_temp];
			data_temp = *(layer22_addr+5403);
			box_h = l22_h1_table[data_temp];
		}
		else {
			layer22_addr = (u8 *)(UINTPTR)(4+(layer22_cmax<<3)+layer22_rmax*208+5408+L22_ADDR);
			data_temp = *layer22_addr;
			box_x = (l22_sigmoid_table[data_temp] + layer22_cmax)/26;
			data_temp = *(layer22_addr+1);
			box_y = (l22_sigmoid_table[data_temp] + layer22_rmax)/26;
			data_temp = *(layer22_addr+2);
			box_w = l22_w2_table[data_temp];
			data_temp = *(layer22_addr+3);
			box_h = l22_h2_table[data_temp];
		}
	}
	else {
		if(layer15_kmax==0) {
			layer15_addr = (u8 *)(UINTPTR)((layer15_cmax<<3)+layer15_rmax*104+L15_ADDR);
			data_temp = *layer15_addr;
			box_x = (l15_sigmoid_table[data_temp] + layer15_cmax)/13;
			data_temp = *(layer15_addr+1);
			box_y = (l15_sigmoid_table[data_temp] + layer15_rmax)/13;
			data_temp = *(layer15_addr+2);
			box_w = l15_w0_table[data_temp];
			data_temp = *(layer15_addr+3);
			box_h = l15_h0_table[data_temp];
		}
		else if(layer15_kmax==1) {
			layer15_addr = (u8 *)(UINTPTR)(6+(layer15_cmax<<3)+layer15_rmax*104+L15_ADDR);
			data_temp = *layer15_addr;
			box_x = (l15_sigmoid_table[data_temp] + layer15_cmax)/13;
			data_temp = *(layer15_addr+1);
			box_y = (l15_sigmoid_table[data_temp] + layer15_rmax)/13;
			data_temp = *(layer15_addr+1346);
			box_w = l15_w1_table[data_temp];
			data_temp = *(layer15_addr+1347);
			box_h = l15_h1_table[data_temp];
		}
		else {
			layer15_addr = (u8 *)(UINTPTR)(4+(layer15_cmax<<3)+layer15_rmax*104+1352+L15_ADDR);
			data_temp = *layer15_addr;
			box_x = (l15_sigmoid_table[data_temp] + layer15_cmax)/13;
			data_temp = *(layer15_addr+1);
			box_y = (l15_sigmoid_table[data_temp] + layer15_rmax)/13;
			data_temp = *(layer15_addr+2);
			box_w = l15_w2_table[data_temp];
			data_temp = *(layer15_addr+3);
			box_h = l15_h2_table[data_temp];
		}
	}

	int box_x1, box_y1, box_x2, box_y2;
	/* Optional display-only margin, neutral by default. Does not change score,
	 * anchors or inference. Hardware draw_box already adds crop offsets 16/48. */
	u32 width_scale=Xil_In32(DIAG_BASE+0x540U);
	u32 height_scale=Xil_In32(DIAG_BASE+0x544U);
	if(width_scale>=1000U && width_scale<=2000U) box_w*=width_scale/1000.0f;
	if(height_scale>=1000U && height_scale<=2000U) box_h*=height_scale/1000.0f;
	box_x1 = (box_x-box_w/2) * IMG_W;
	box_x2 = (box_x+box_w/2) * IMG_W;
	box_y1 = (box_y-box_h/2) * IMG_H;
	box_y2 = (box_y+box_h/2) * IMG_H;

	if(box_x1 < 0) box_x1 = 0;
	if(box_y1 < 0) box_y1 = 0;
	if(box_x2 >= IMG_W) box_x2 = IMG_W - 1;
	if(box_y2 >= IMG_H) box_y2 = IMG_H - 1;
	if(box_x2 <= box_x1 || box_y2 <= box_y1) {
		score_log(layer15_score_f,layer22_score_f,2U,box_x1,box_y1,box_x2,box_y2);
		box_x1 = 0;
		box_y1 = 0;
		box_x2 = 0;
		box_y2 = 0;
	}

	if(box_x2>box_x1 && box_y2>box_y1)
		score_smooth_box(&box_x1,&box_y1,&box_x2,&box_y2);
	Xil_Out32(BOX_X1_ADDR, box_x1);
	Xil_Out32(BOX_Y1_ADDR, box_y1);
	Xil_Out32(BOX_X2_ADDR, box_x2);
	Xil_Out32(BOX_Y2_ADDR, box_y2);
	if(box_x2>box_x1 && box_y2>box_y1)
		score_log(layer15_score_f,layer22_score_f,3U,box_x1,box_y1,box_x2,box_y2);
}









