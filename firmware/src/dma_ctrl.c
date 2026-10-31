#ifdef DMA_HOST_TEST
#include "../tests/dma_mock.h"
#define DMA_MEMORY_BARRIER() ((void)0)
#else
#include "dma_ctrl.h"
#include "xil_printf.h"
#include "yolo_diag.h"
#include "yolo_load_param.h"
#include "perf_profile.h"
#define DMA_MEMORY_BARRIER() __asm__ volatile("dsb sy" ::: "memory")
#endif
#include "dma_layer_plan.h"
static DmaLayerPlan layer_plan;
static int immutable_ready;
static u32 skip_calls,skip_bytes,clean_calls,clean_bytes;
void DMA_PerfReset(void) {skip_calls=skip_bytes=clean_calls=clean_bytes=0;dma_layer_reset(&layer_plan);}
void DMA_PerfRead(u32 *a,u32 *b,u32 *c,u32 *d) {*a=skip_calls;*b=skip_bytes;*c=clean_calls;*d=clean_bytes;}
void DMA_PrepareImmutableParameters(void) {
    /* Only after successful SD load. No runtime CPU writes to these regions. */
    Xil_DCacheFlushRange(BiasAddr[0],BiasAddr[14]-BiasAddr[0]);
    Xil_DCacheFlushRange(ActAddr[0],ActAddr[14]-ActAddr[0]);
    Xil_DCacheFlushRange(WeightAddr[0],WeightAddr[14]-WeightAddr[0]);
    DMA_MEMORY_BARRIER();
    immutable_ready=1;
}
static int contained(u32 addr,u32 len,u32 begin,u32 end) {
    return addr>=begin && addr<end && len<=end-addr;
}
void DMA_BeginLayer(u32 layer,u32 source,u32 channels,u32 side) {
    u32 bytes=dma_layer_bytes(layer,channels,side);
    dma_layer_reset(&layer_plan);
    if(!bytes || bytes>0xFFFFFFFFU-source)diag_fail(E_BAD_TRANSFER);
    /* Flush ONCE before the first task, even for a previously clean source.
     * During yolo_forward CPU only reads features; writes are DMA-owned and
     * destination invalidate-before/after RX remains unchanged. No range is
     * carried into a later layer/frame. Unknown TX sources still use flush. */
    Xil_DCacheFlushRange(source,bytes);
    DMA_MEMORY_BARRIER();
    clean_calls++;clean_bytes+=bytes;
    (void)dma_layer_set(&layer_plan,source,bytes);
}
void DMA_EndLayer(void){dma_layer_reset(&layer_plan);}

#define DMA_RESET_TIMEOUT 0x01000000U

void DMA_Init()
{
	immutable_ready=0;
	dma_layer_reset(&layer_plan);
	u32 timeout = DMA_RESET_TIMEOUT;
	u32 base = XPAR_AXIDMA_0_BASEADDR;

	/* A reset request is asynchronous to the AXI-Lite write.  Register
	 * writes made while RESET is still asserted may be ignored, which
	 * leaves MM2S halted and prevents the PL task from ever finishing. */
	Xil_Out32(base + XAXIDMA_TX_OFFSET + XAXIDMA_CR_OFFSET,
			XAXIDMA_CR_RESET_MASK);
	while ((Xil_In32(base + XAXIDMA_TX_OFFSET + XAXIDMA_CR_OFFSET) &
			XAXIDMA_CR_RESET_MASK) != 0U && timeout > 0U)
		timeout--;

	if (timeout == 0U) {
		xil_printf("ERROR: AXI DMA reset timeout\r\n");
		diag_fail(E_DMA_RESET);
	}

	/* IRQ bits are acknowledged in the status registers, not written to
	 * the control registers.  This design uses the accelerator's finish
	 * signal, so DMA interrupts remain disabled. */
	Xil_Out32(base + XAXIDMA_TX_OFFSET + XAXIDMA_SR_OFFSET,
			XAXIDMA_IRQ_ALL_MASK);
	Xil_Out32(base + XAXIDMA_RX_OFFSET + XAXIDMA_SR_OFFSET,
			XAXIDMA_IRQ_ALL_MASK);
	Xil_Out32(base + XAXIDMA_TX_OFFSET + XAXIDMA_CR_OFFSET,
			XAXIDMA_CR_RUNSTOP_MASK);
	Xil_Out32(base + XAXIDMA_RX_OFFSET + XAXIDMA_CR_OFFSET,
			XAXIDMA_CR_RUNSTOP_MASK);
}


void DMA_Tx(u32 TxAddr, u32 Length)
{
	diag_dma_start(1U, TxAddr, Length);
	if(dma_layer_contains(&layer_plan,TxAddr,Length) || (immutable_ready &&
       (contained(TxAddr,Length,BiasAddr[0],BiasAddr[14]) ||
        contained(TxAddr,Length,ActAddr[0],ActAddr[14]) ||
        contained(TxAddr,Length,WeightAddr[0],WeightAddr[14])))) {
        skip_calls++;skip_bytes+=Length;
    } else {
        Xil_DCacheFlushRange(TxAddr, Length);
        clean_calls++;clean_bytes+=Length;
    }
	Xil_Out32(XPAR_AXIDMA_0_BASEADDR+XAXIDMA_TX_OFFSET+XAXIDMA_SRCADDR_OFFSET, TxAddr);
	Xil_Out32(XPAR_AXIDMA_0_BASEADDR+XAXIDMA_TX_OFFSET+XAXIDMA_BUFFLEN_OFFSET, Length);
}

void DMA_Rx(u32 RxAddr, u32 Length)
{
	diag_dma_start(2U, RxAddr, Length);
	/* The DMA owns the destination buffer during S2MM.  Drop any cached
	 * CPU copy before starting the transfer; the caller invalidates again
	 * after completion before reading the new data. */
	Xil_DCacheInvalidateRange(RxAddr, Length);
	Xil_Out32(XPAR_AXIDMA_0_BASEADDR+XAXIDMA_RX_OFFSET+XAXIDMA_DESTADDR_OFFSET, RxAddr);
	Xil_Out32(XPAR_AXIDMA_0_BASEADDR+XAXIDMA_RX_OFFSET+XAXIDMA_BUFFLEN_OFFSET, Length);
}
