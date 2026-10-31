#include "yolo_diag.h"
#include "yolo_accel_ctrl.h"
#include "xil_mmu.h"
#include "xil_exception.h"
#include "xtime_l.h"
#include <string.h>
#include "score_log.h"
#include "candidate_log.h"
#include "crc_fast.h"
#include "dma_ctrl.h"
#include "perf_profile.h"
#include "video_startup.h"

volatile YoloDiag * const yolo_diag = (volatile YoloDiag *)DIAG_BASE;
typedef char check_mailbox_size[(sizeof(YoloDiag)==256)?1:-1];
#define DMA_BASE XPAR_AXIDMA_0_BASEADDR
#define VDMA_BASE XPAR_AXI_VDMA_1_BASEADDR
#define INPUT_BASE 0x01000000U
#define INPUT_SIZE (416U*416U*8U)
#define ERR_DMA 0x770U
#define ERR_VDMA 0x8FF0U
#define CAPTURE_A 0x01400000U
#define CAPTURE_B 0x01600000U
/* Separate uncached diagnostic extension, not a hardware register. */
typedef struct {
    u32 magic, phase, active, frozen, events, sr, cr, copied;
    u32 source_before, source_after, input, completed, switches;
} CaptureDiag;
static volatile CaptureDiag * const capture=(volatile CaptureDiag *)(DIAG_BASE+0x400U);
static u32 capture_active=CAPTURE_A;
static unsigned crc_table[4][256];
static u32 breakdown[10];
static u32 elapsed_us(XTime start) {XTime now;XTime_GetTime(&now);return (u32)((now-start)*1000000ULL/COUNTS_PER_SECOND);}
static int full_check(void) { return profile_replay() || yolo_diag->frame==1U || yolo_diag->frame%30U==0U; }
/* live_capture runs before frame is incremented.  Use the upcoming frame
 * number so frame 1 and every 30th frame receive the complete four-CRC audit. */
static int capture_full_check(void) {
    u32 next=yolo_diag->frame+1U;
    return next==1U || next%30U==0U;
}
static u32 pending_direction;
static XTime task_started;
static u32 read_cpsr(void) { u32 v; __asm__ volatile("mrs %0, cpsr" : "=r"(v)); return v; }
/* Read-only GIC snapshot. Never read IAR: doing so acknowledges an IRQ. */
static void irq_snapshot(void) {
    volatile u32 *p=(volatile u32 *)(DIAG_BASE+0x100U);
    static const u32 addresses[9]={0xF8F00100U,0xF8F00104U,
        0xF8F01000U,0xF8F01104U,0xF8F01204U,0xF8F01304U,
        0xF8F0183CU,0xF8F01C0CU,0xF8F0143CU};
    unsigned i;
    for(i=0;i<9;i++) p[i]=Xil_In32(addresses[i]);
}
static const u32 layer_sizes[15] = {
    692224,346112,173056,86528,173056,43264,86528,173056,
    43264,86528,4056,21632,86528,173056,16224
};

static XTime now_ticks(void) { XTime t; XTime_GetTime(&t); return t; }
static int expired(XTime start) { return now_ticks()-start > (XTime)COUNTS_PER_SECOND*2U; }
static void dma_status(void) {
    yolo_diag->tx_status=Xil_In32(DMA_BASE+0x04);
    yolo_diag->rx_status=Xil_In32(DMA_BASE+0x34);
}
static u32 buffer_crc(u32 addr,u32 length) {
    const volatile u8 *p=(const volatile u8 *)(UINTPTR)addr;
    XTime started=now_ticks();
    u32 crc;
    Xil_DCacheInvalidateRange(addr,length);
    crc=crc_fast(p,length,crc_table);
    breakdown[8]+=elapsed_us(started);breakdown[9]+=length;
    return crc;
}
void diag_stage(u32 stage) { yolo_diag->stage=stage; __asm__ volatile("dsb sy" ::: "memory"); }
void diag_init(void) {
    unsigned i;
    crc_fast_init(crc_table);
    /* Dedicated 1-MiB DDR section below all image/model buffers. */
    Xil_SetTlbAttributes(DIAG_BASE,NORM_NONCACHE);
    for(i=0;i<20;i++) Xil_Out32(DIAG_BASE+0x2200U+i*4U,0U);
    Xil_Out32(DIAG_BASE+0x2200U,0x50524632U);
    score_log_init();
    candidate_log_init();
    diag_params_init();
    for(i=0;i<64;i++) ((volatile u32 *)yolo_diag)[i]=0;
    yolo_diag->magic=0x59444731U;
    yolo_diag->version=2;
    for(i=0;i<5;i++) ((volatile u32 *)(DIAG_BASE+0x200U))[i]=0;
    for(i=0;i<9;i++) ((volatile u32 *)(DIAG_BASE+0x100U))[i]=0;
    for(i=0;i<sizeof(CaptureDiag)/sizeof(u32);i++) ((volatile u32 *)capture)[i]=0;
    capture->magic=0x43415031U;
    capture->active=CAPTURE_A;
    for(i=0;i<8;i++) Xil_Out32(DIAG_BASE+0x500U+i*4U,0U);
    Xil_Out32(DIAG_BASE+0x500U,0x50455231U);
    Xil_Out32(DIAG_BASE+0x540U,1000U); /* display width permille */
    Xil_Out32(DIAG_BASE+0x544U,1000U); /* display height permille */
    yolo_diag->layer=0xFFFFFFFFU;
    profile_init();
    diag_stage(D_BOOT);
}
void yolo_diag_failed(void) { for(;;) __asm__ volatile("nop"); }
void yolo_diag_complete(void) { for(;;) __asm__ volatile("nop"); }
void diag_fail(u32 error) {
    /* Preserve the first error; do not issue another accelerator command. */
    yolo_diag->reserved1=read_cpsr(); /* Before diagnostic itself masks IRQ. */
    Xil_ExceptionDisable();
    irq_snapshot();
    dma_status();
    yolo_diag->reserved0=yolo_diag->stage;
    yolo_diag->error=error;
    yolo_diag->vdma_status=Xil_In32(VDMA_BASE+0x34);
    diag_stage(D_FAILED);
    yolo_diag_failed();
}
static unsigned startup_read(void *context,unsigned address) {
    (void)context;return Xil_In32(address);
}
static void startup_write(void *context,unsigned address,unsigned value) {
    (void)context;Xil_Out32(address,value);
    __asm__ volatile("dsb sy" ::: "memory");
}
static unsigned startup_milliseconds(void *context) {
    (void)context;return (unsigned)(now_ticks()/(COUNTS_PER_SECOND/1000U));
}
void diag_wait_capture(void) {
    const VideoStartupIo io={0,startup_read,startup_write,startup_milliseconds};
    volatile VideoStartupReport *const startup=(volatile VideoStartupReport *)(DIAG_BASE+0xB00U);
    int video_ok;
    Xil_Out32(DIAG_BASE+0x380U,0x434F4E31U); /* CON1, continuous candidate */
    yolo_diag->reserved2=read_cpsr();
    diag_stage(D_READY);
    video_ok=video_startup_sync(&io,startup);
    xil_printf("VIDEO_STARTUP result=%d reason=%d clean=%d/%d clears=%d/%d elapsed_ms=%d\r\n",
               startup->result,startup->reason,startup->clean_frames[0],startup->clean_frames[1],
               startup->clears[0],startup->clears[1],startup->elapsed_ms);
    if(!video_ok)diag_fail(E_CAPTURE);
}
static u32 capture_status(void) {
    capture->cr=Xil_In32(VDMA_BASE+0x30U);
    capture->sr=Xil_In32(VDMA_BASE+0x34U);
    yolo_diag->vdma_status=capture->sr;
    if((capture->sr & (ERR_VDMA|1U)) ||
       (capture->cr & 0x00FF0015U)!=0x00010001U) diag_fail(E_CAPTURE);
    return capture->sr;
}
static void capture_fence(void) {
    XTime started=now_ticks();
    capture->events=0;
    (void)capture_status();
    /* Discard old IOC only; never hide a video/DMA error. Counter events are
     * frame starts, not a generic end-of-frame indication. Two separately
     * acknowledged events after VSIZE are the candidate handoff fence tested
     * against this project's actual VDMA IP. Board validation is still required. */
    Xil_Out32(VDMA_BASE+0x34U,0x1000U);
    __asm__ volatile("dsb sy" ::: "memory");
    while(capture->events<2U) {
        if(capture_status() & 0x1000U) {
            Xil_Out32(VDMA_BASE+0x34U,0x1000U);
            __asm__ volatile("dsb sy" ::: "memory");
            capture->events++;
        }
        if(expired(started)) diag_fail(E_CAPTURE);
    }
    breakdown[0]+=elapsed_us(started);
}
static void live_capture(void) {
    XTime stamp;
    int audit=capture_full_check();
    u32 next=(capture_active==CAPTURE_A)?CAPTURE_B:CAPTURE_A;
    diag_stage(D_CAPTURE);
    capture->phase=1; capture->copied=0;
    /* On the first call make sure A has seen complete frames before switching. */
    if(capture->switches==0U) capture_fence();
    capture->frozen=capture_active;
    capture->phase=2;
    (void)capture_status();
    /* One hardware frame store: change slot 0, commit last with VSIZE.
     * Do not stop/reset RS and do not write non-existent slot 1. */
    Xil_Out32(VDMA_BASE+0xACU,next);
    Xil_Out32(VDMA_BASE+0xA8U,416U*8U);
    Xil_Out32(VDMA_BASE+0xA4U,416U*8U);
    Xil_Out32(VDMA_BASE+0xA0U,416U);
    __asm__ volatile("dsb sy" ::: "memory");
    capture->phase=3;
    capture_fence();
    capture_active=next; capture->active=next; capture->switches++;
    capture->phase=4;
    stamp=now_ticks();
    capture->source_before=buffer_crc(capture->frozen,INPUT_SIZE);
    breakdown[1]=elapsed_us(stamp);stamp=now_ticks();
    memcpy((void *)INPUT_BASE,(const void *)(UINTPTR)capture->frozen,INPUT_SIZE);
    Xil_DCacheFlushRange(INPUT_BASE,INPUT_SIZE);
    __asm__ volatile("dsb sy" ::: "memory");
    breakdown[2]=elapsed_us(stamp);stamp=now_ticks();
    if(audit) {
        capture->source_after=buffer_crc(capture->frozen,INPUT_SIZE);
        breakdown[3]=elapsed_us(stamp);
    } else {
        /* Zero explicitly means skipped, never a stale CRC presented as new. */
        capture->source_after=0U;
        breakdown[3]=0U;
    }
    stamp=now_ticks();
    capture->input=buffer_crc(INPUT_BASE,INPUT_SIZE);
    breakdown[4]=elapsed_us(stamp);
    if((audit && capture->source_before!=capture->source_after) ||
       capture->input!=capture->source_before) diag_fail(E_INPUT_CHANGED);
    breakdown[6]=(u32)audit;
    (void)capture_status();
    capture->copied=INPUT_SIZE;
    yolo_diag->input_reference=capture->input;
    capture->phase=5;
}
void diag_frame_begin(void) {
    unsigned i;
    profile_frame_begin();
    for(i=0;i<10;i++) breakdown[i]=0;
    DMA_PerfReset();
    if(profile_replay()) {
        /* Only entered by the separate replay script at a frame boundary.
         * VDMA keeps writing A/B, never INPUT_BASE. Re-read and verify the
         * uploaded exact packed input on every replay; no image transform. */
        capture->input=buffer_crc(INPUT_BASE,INPUT_SIZE);
        if(capture->input!=profile_expected_crc())diag_fail(E_INPUT_CHANGED);
        yolo_diag->input_reference=capture->input;
    } else live_capture();
    yolo_diag->frame++;
    yolo_diag->layer=0xFFFFFFFFU;
    diag_stage(D_FRAME);
    yolo_diag->vdma_status=Xil_In32(VDMA_BASE+0x34);
    (void)capture_status();
    /* live_capture just verified this same destination after copy+flush.
     * No CPU/DMA writer owns INPUT_BASE between there and inference. */
    yolo_diag->input_crc=capture->input;
    if(yolo_diag->input_crc!=yolo_diag->input_reference) diag_fail(E_INPUT_CHANGED);
}
void diag_frame_end(void) {
    XTime stamp=now_ticks();
    u32 a,b,c,d,i;
    volatile u32 *p=(volatile u32 *)(DIAG_BASE+0x2200U);
    if(full_check()) {
        yolo_diag->input_crc=buffer_crc(INPUT_BASE,INPUT_SIZE);
        breakdown[5]=elapsed_us(stamp);
        if(yolo_diag->input_crc!=yolo_diag->input_reference) diag_fail(E_INPUT_CHANGED);
        breakdown[7]=1U;
    } else {
        /* The destination was CRC-verified immediately after copy.  INPUT_BASE
         * has no CPU or DMA writer during inference.  Keep the verified value
         * visible and reserve the second scan for the periodic full audit. */
        yolo_diag->input_crc=yolo_diag->input_reference;
        breakdown[5]=0U;
        breakdown[7]=0U;
    }
    (void)capture_status();
    capture->completed++;
    DMA_PerfRead(&a,&b,&c,&d);
    p[1]++;__asm__ volatile("dsb sy" ::: "memory");
    p[2]=yolo_diag->frame;
    for(i=0;i<10;i++) p[3+i]=breakdown[i];
    p[13]=a;p[14]=b;p[15]=c;p[16]=d;
    __asm__ volatile("dsb sy" ::: "memory");p[1]++;
    diag_stage(D_READY); /* Repeat with a new camera capture, not a replay. */
}
void diag_layer_begin(u32 layer) {
    profile_layer_begin(layer);
    yolo_diag->layer=layer; diag_stage(D_LAYER);
    if(layer==0U && full_check()) diag_params_check(1U);
    if(layer>=15U)diag_fail(E_BAD_TRANSFER);
    DMA_BeginLayer(layer,layers_tx_addr[layer],(u32)layers_config[layer][1],(u32)layers_config[layer][4]);
}
void diag_layer_done(u32 layer) {
    u32 crc;
    dma_status();
    if((yolo_diag->tx_status|yolo_diag->rx_status)&ERR_DMA) diag_fail(E_DMA_STATUS);
    if(!(yolo_diag->rx_status&2U)) diag_fail(E_DMA_TIMEOUT);
    if(!full_check()) {
        /* Zero means not checked this live frame; never report stale CRC as fresh. */
        yolo_diag->current[layer]=yolo_diag->reference[layer]=0;
        yolo_diag->last_crc=yolo_diag->expected_crc=0;
        profile_layer_end(layer,0U,0U);
        DMA_EndLayer();
        return;
    }
    crc=buffer_crc(layers_rx_addr[layer],layer_sizes[layer]);
    if(layer==0U) diag_params_check(2U);
    yolo_diag->current[layer]=crc;
    yolo_diag->last_crc=crc;
    yolo_diag->reference[layer]=crc; /* Live frames may legitimately differ. */
    yolo_diag->expected_crc=yolo_diag->reference[layer];
    profile_layer_end(layer,crc,1U);
    DMA_EndLayer();
    diag_stage(D_LAYER);
}
void diag_write_reg(UINTPTR addr,u32 value) {
    if(addr==Lite_Reg0 && value!=0U) {
        profile_task_begin(value);
        if(pending_direction) diag_fail(E_BAD_TRANSFER);
        if(InterruptProcessed) yolo_diag->stale_irq++;
        InterruptProcessed=FALSE;
        yolo_diag->task++;
        yolo_diag->command=value;
        yolo_diag->direction=0;
        yolo_diag->tx_addr=yolo_diag->tx_len=0;
        yolo_diag->rx_addr=yolo_diag->rx_len=0;
        yolo_diag->feature_size=feature_size;
        yolo_diag->in_batch=ch_in_batch_cnt;
        yolo_diag->out_batch=ch_out_batch_cnt;
        yolo_diag->row_batch=tx_cnt;
        yolo_diag->irq_start=yolo_diag->irq_count;
        task_started=now_ticks();
        diag_stage(D_TASK);
    }
    Xil_Out32(addr,value);
}
void diag_dma_start(u32 direction,u32 addr,u32 len) {
    u32 cr,sr,offset=(direction==1U)?0U:0x30U;
    if(pending_direction || !len || (direction!=1U && direction!=2U)) diag_fail(E_BAD_TRANSFER);
    dma_status();
    cr=Xil_In32(DMA_BASE+offset);
    sr=Xil_In32(DMA_BASE+offset+4);
    /* Idle can be low before the first transfer. RS=1, Halted=0 are required. */
    if((sr&ERR_DMA) || (sr&1U) || !(cr&1U)) diag_fail(E_DMA_STATUS);
    pending_direction=direction;
    profile_dma(direction,len);
    yolo_diag->direction=direction;
    if(direction==1U) { yolo_diag->tx_addr=addr; yolo_diag->tx_len=len; }
    else { yolo_diag->rx_addr=addr; yolo_diag->rx_len=len; }
    /* Clear prior IOC so the completion check belongs to this submission. */
    Xil_Out32(DMA_BASE+offset+4,0x1000U);
}
void diag_irq(void) { yolo_diag->irq_count++; InterruptProcessed=TRUE; }
int diag_wait_task(void) {
    XTime start=now_ticks();
    u32 sr=0;
    yolo_diag->reserved3=read_cpsr();
    diag_stage(D_IRQ_WAIT);
    while(!InterruptProcessed) {
        dma_status();
        if((yolo_diag->tx_status|yolo_diag->rx_status)&ERR_DMA) diag_fail(E_DMA_STATUS);
        if(expired(start)) diag_fail(E_PL_TIMEOUT);
    }
    profile_wait(0U,now_ticks()-start);
    /* Bits 0..3 are task strobes. Bit8 is also the pooling reset gate:
     * clearing the whole register destroys the carried half-row between
     * feature blocks. Preserve configuration exactly as the RTL auto-clear. */
    Xil_Out32(Lite_Reg0,yolo_diag->command & ~0xFU);
    if(pending_direction) {
        diag_stage(D_DMA_WAIT);
        start=now_ticks();
        do {
            dma_status();
            sr=(pending_direction==1U)?yolo_diag->tx_status:yolo_diag->rx_status;
            if(sr&ERR_DMA) diag_fail(E_DMA_STATUS);
            if(expired(start)) diag_fail(E_DMA_TIMEOUT);
        } while((sr&0x1002U)!=0x1002U);
        profile_wait(1U,now_ticks()-start);
    }
    pending_direction=0;
    InterruptProcessed=FALSE;
    yolo_diag->completed_tasks++;
    yolo_diag->transfer_us=(u32)((now_ticks()-task_started)*1000000U/COUNTS_PER_SECOND);
    diag_stage(D_TASK);
    profile_task_end();
    return XST_SUCCESS;
}
