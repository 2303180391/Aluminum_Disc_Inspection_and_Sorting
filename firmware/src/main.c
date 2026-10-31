#include "xil_printf.h"
#include "xplatform_info.h"
#include "xil_exception.h"
#include "xscugic.h"
#include "yolo_load_param.h"
#include "yolo_accel_ctrl.h"
#include "dma_ctrl.h"
#include "xtime_l.h"
#include "yolo_diag.h"
#include "multiface_diag.h"
#include "mb_driver.h"
#include "arm_remote.h"
#include "perf_profile.h"
#include "disc_locator.h"
#include "arm5_io.h"

#define INTC_DEVICE_ID		XPAR_SCUGIC_0_DEVICE_ID
#define INTC_DEVICE_INT_ID	XPS_FPGA0_INT_ID
#define VDMA0_BASEADDR   XPAR_AXI_VDMA_0_BASEADDR
#define VDMA0_FRAME0     0x04000000
#define STRIDE          1280
#define HSIZE           1280
#define VSIZE           720

#define VDMA1_BASEADDR   XPAR_AXI_VDMA_1_BASEADDR
#define VDMA1_FRAME0    0x01400000U
#define YOLO_STRIDE     416
#define YOLO_HSIZE      416
#define YOLO_VSIZE      416

XScuGic InterruptController; 	     /* Instance of the Interrupt Controller */
static XScuGic_Config *GicConfig;    /* The configuration parameters of the
                                       controller */

void DeviceDriverHandler(void *CallbackRef);
int Intr_init();

int main()
{
	diag_init();
	mf_diag_init();
	// VDMA0写
	Xil_Out32(VDMA0_BASEADDR+0x30,   0x1);
	Xil_Out32(VDMA0_BASEADDR+0xAC,   VDMA0_FRAME0);
	Xil_Out32(VDMA0_BASEADDR+0xA8,   STRIDE*3);
	Xil_Out32(VDMA0_BASEADDR+0xA4,   HSIZE*3);
	Xil_Out32(VDMA0_BASEADDR+0xA0,   VSIZE);
	// VDMA0读
	Xil_Out32(VDMA0_BASEADDR,        0x1);
	Xil_Out32(VDMA0_BASEADDR+0x5c,   VDMA0_FRAME0);
	Xil_Out32(VDMA0_BASEADDR+0x58,   STRIDE*3);
	Xil_Out32(VDMA0_BASEADDR+0x54,   HSIZE*3);
	Xil_Out32(VDMA0_BASEADDR+0x50,   VSIZE);
	// VDMA1写
	/* Continuous capture; threshold=1, FrameCntEn=0. Never DMA into YOLO input. */
	Xil_Out32(VDMA1_BASEADDR+0x30,   0x00010001U);
	Xil_Out32(VDMA1_BASEADDR+0xAC,   VDMA1_FRAME0);
	Xil_Out32(VDMA1_BASEADDR+0xA8,   YOLO_STRIDE*8);
	Xil_Out32(VDMA1_BASEADDR+0xA4,   YOLO_HSIZE*8);
	Xil_Out32(VDMA1_BASEADDR+0xA0,   YOLO_VSIZE);
///////////////////////////////////////////////////////////
	if(SD_Init() != XST_SUCCESS) {
		xil_printf("ERROR: SD card mount failed\r\n");
		diag_fail(E_SD);
	}
	DMA_Init();
	if(Intr_init() != XST_SUCCESS) {
		xil_printf("ERROR: interrupt initialization failed\r\n");
		diag_fail(E_IRQ_INIT);
	}
	diag_stage(D_LOAD);
	if(load_param() != XST_SUCCESS)
		diag_fail(E_SD);
	DMA_PrepareImmutableParameters();
	diag_wait_capture();
	mb_display_init();

    u64 begin_time;
    u64 end_time;
    u32 time_us;
    u32 frame_count = 0;
    XTime frame_start, captured, detected, finished;
    volatile u32 *perf=(volatile u32 *)(DIAG_BASE+0x500U);

	xil_printf("YOLO accelerator started\r\n");
	disc_board_init();
	arm5_init();
	arm_uart_init();
	while(1) {
		arm5_poll();
		disc_board_poll();
		arm_uart_poll();
		XTime_GetTime(&frame_start);
		diag_frame_begin();
		XTime_GetTime(&captured);
		/* diag_frame_begin copied a frozen capture into 0x01000000 and flushed it. */
		XTime_GetTime(&begin_time);
		yolo_forward();
		XTime_GetTime(&end_time);
		time_us = (u32)(((end_time - begin_time) * 1000000ULL) /
				COUNTS_PER_SECOND);
		Xil_DCacheInvalidateRange(0x03300000U,4056U);
		Xil_DCacheInvalidateRange(0x03400000U,16224U);
		/* Four-box display is committed by track_record after frame integrity. */
		XTime_GetTime(&detected);
		diag_frame_end();
		mf_diag_record(); /* Decode, associate, then commit all four display slots. */
		arm_uart_poll();
		XTime_GetTime(&finished);
		frame_count++;
		perf[1]++; /* odd: snapshot being updated */
		perf[2]=(u32)((captured-frame_start)*1000000ULL/COUNTS_PER_SECOND);
		perf[3]=time_us;
		perf[4]=(u32)((detected-end_time)*1000000ULL/COUNTS_PER_SECOND);
		perf[5]=(u32)((finished-frame_start)*1000000ULL/COUNTS_PER_SECOND);
		perf[6]=frame_count;
		perf[7]=(frame_count==1U || frame_count%30U==0U);
		__asm__ volatile("dsb sy" ::: "memory");
		perf[1]++; /* even: complete snapshot */
		profile_frame_end(perf[2],perf[3],perf[4],perf[5]);
		if((frame_count % 30U) == 0U)
			xil_printf("YOLO inference: %d us\r\n", time_us);
	}
	return 0;

}



void DeviceDriverHandler(void *CallbackRef)
{
	(void)CallbackRef;
	/*
	 * Indicate the interrupt has been processed using a shared variable
	 */
	diag_irq();
}

int Intr_init()
{
	int Status;

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use.
	 */
	GicConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == GicConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(&InterruptController, GicConfig,
					GicConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}


	Xil_ExceptionInit();
	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the ARM processor.
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler) XScuGic_InterruptHandler,
			&InterruptController);

	/*
	 * Enable interrupts in the ARM
	 */
	Xil_ExceptionEnable();

	/*
	 * Connect a device driver handler that will be called when an
	 * interrupt for the device occurs, the device driver handler performs
	 * the specific interrupt processing for the device
	 */
	Status = XScuGic_Connect(&InterruptController, INTC_DEVICE_INT_ID,
			   (Xil_ExceptionHandler)DeviceDriverHandler,
			   (void *)&InterruptController);

	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Controlled comparison: latch rising edge of the bounded FINISH pulse.
	 * All other initialization and computation remain unchanged. */
	XScuGic_SetPriorityTriggerType(&InterruptController, INTC_DEVICE_INT_ID, 0x0, 0x3);

	/*
	 * Enable the interrupt for the device and then cause (simulate) an
	 * interrupt so the handlers will be called
	 */
	XScuGic_Enable(&InterruptController, INTC_DEVICE_INT_ID);

	return 0;
}
