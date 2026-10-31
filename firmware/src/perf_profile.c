#include "perf_profile.h"
#ifndef PROFILE_HOST_TEST
#include "yolo_diag.h"
#include "yolo_accel_ctrl.h"
#endif
#include "crc_fast.h"
#ifndef PROFILE_HOST_TEST
#include <string.h>
#endif

/* All high-frequency counters are cached CPU-owned data, not MMIO. Tick sums
 * are converted once per frame; there is no division inside the task hooks. */
typedef struct {
    XTime begin,total,irq,dma_tail,flush,invalidate,task_span,kind_span[7];
    u32 tasks,tx_calls,tx_bytes,rx_calls,rx_bytes,kind_count[7],crc,audited;
} Layer;
static Layer layers[15];
static u32 current_layer,sequence;
static XTime previous_begin,period;
#if PROFILE_TASK_DETAIL
static u32 kind;
static XTime task_begin;
#endif
static unsigned table[4][256];
#ifdef PROFILE_HOST_TEST
static volatile u32 *const header=profile_test_mail;
#else
static volatile u32 *const header=(volatile u32 *)PROFILE_BASE;
#endif
static XTime ticks(void){XTime t;XTime_GetTime(&t);return t;}
static u32 us(XTime t){return (u32)(t*1000000ULL/COUNTS_PER_SECOND);}
static void barrier(void){
#ifndef PROFILE_HOST_TEST
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}
int profile_replay(void){return header[6]==1U;}
u32 profile_expected_crc(void){return header[7];}
void profile_init(void){
    unsigned i;
    for(i=0;i<PROFILE_HEADER_WORDS+PROFILE_RING*PROFILE_WORDS;i++)header[i]=0;
    header[1]=1;header[2]=PROFILE_RING;header[3]=PROFILE_WORDS;
    header[8]=COUNTS_PER_SECOND;header[9]=15;header[10]=PROFILE_LAYER_WORDS;
    header[11]=PROFILE_TASK_DETAIL?0x44455431U:0x434F4152U; /* DET1/COAR */
    header[14]=0x43535131U;header[15]=0x20261018U; /* CSQ1 */
    crc_fast_init(table);current_layer=15;sequence=0;previous_begin=0;
    barrier();header[0]=PROFILE_MAGIC;
}
void profile_frame_begin(void){
    XTime now=ticks();
    period=previous_begin?now-previous_begin:0;previous_begin=now;
    memset(layers,0,sizeof layers);current_layer=15;
}
void profile_layer_begin(u32 n){if(n<15U){current_layer=n;layers[n].begin=ticks();}}
void profile_layer_end(u32 n,u32 crc,u32 audited){
    if(n<15U){layers[n].total=ticks()-layers[n].begin;layers[n].crc=crc;layers[n].audited=audited;}
    current_layer=15;
}
#if PROFILE_TASK_DETAIL
void profile_task_begin(u32 command){
    if(current_layer>=15U)return;
    if((command&0x3FU)==WRITE_BIAS)kind=0;
    else if((command&0x3FU)==WRITE_LEAKY_RELU)kind=1;
    else if((command&0x3FU)==WRITE_WEIGHT)kind=2;
    else if(command&READ_START)kind=5;
    else if(command&CONV_START)kind=4;
    else if(command&UPSAMPLE_START)kind=6;
    else kind=3;
    task_begin=ticks();layers[current_layer].tasks++;layers[current_layer].kind_count[kind]++;
}
void profile_task_end(void){
    XTime span;
    if(current_layer>=15U)return;
    span=ticks()-task_begin;layers[current_layer].task_span+=span;layers[current_layer].kind_span[kind]+=span;
}
void profile_dma(u32 direction,u32 length){
    Layer *p;
    if(current_layer>=15U)return;
    p=&layers[current_layer];
    if(direction==1U){p->tx_calls++;p->tx_bytes+=length;}else{p->rx_calls++;p->rx_bytes+=length;}
}
void profile_cache(u32 direction,XTime elapsed){
    if(current_layer>=15U)return;
    if(direction==1U)layers[current_layer].flush+=elapsed;else layers[current_layer].invalidate+=elapsed;
}
void profile_wait(u32 dma_tail,XTime elapsed){
    if(current_layer>=15U)return;
    if(dma_tail)layers[current_layer].dma_tail+=elapsed;else layers[current_layer].irq+=elapsed;
}
#endif
void profile_frame_end(u32 capture_us,u32 inference_us,u32 head_cache_us,u32 body_us){
    unsigned i,k;u32 seq=++sequence;
    XTime started=ticks();
    volatile u32 *p=header+PROFILE_HEADER_WORDS+((seq-1U)%PROFILE_RING)*PROFILE_WORDS;
    p[0]=p[PROFILE_WORDS-1U]=0;barrier();
    for(i=1;i<PROFILE_WORDS-1U;i++)p[i]=0;
    p[1]=yolo_diag->frame;p[2]=(yolo_diag->frame==1U || yolo_diag->frame%30U==0U || profile_replay());
    p[3]=capture_us;p[4]=inference_us;p[5]=head_cache_us;p[6]=body_us;
    p[7]=us(period);p[8]=yolo_diag->input_crc;p[11]=15;p[12]=(u32)profile_replay();
    if(p[2]){
        /* Head cache was invalidated by main/diag_layer_done before this read. */
        p[9]=crc_fast((const volatile u8 *)L15_ADDR,4056U,table);
        p[10]=crc_fast((const volatile u8 *)L22_ADDR,16224U,table);
    }
    for(i=0;i<15;i++){
        Layer *l=&layers[i];volatile u32 *q=p+32U+i*PROFILE_LAYER_WORDS;
        q[0]=us(l->total);q[1]=l->tasks;q[2]=l->tx_calls;q[3]=l->tx_bytes;q[4]=l->rx_calls;q[5]=l->rx_bytes;
        q[6]=us(l->irq);q[7]=us(l->dma_tail);q[8]=us(l->flush);q[9]=us(l->invalidate);q[10]=us(l->task_span);
        for(k=0;k<7;k++){q[12+k*2]=l->kind_count[k];q[13+k*2]=us(l->kind_span[k]);}
        q[26]=l->crc;q[27]=l->audited;
    }
    p[13]=us(ticks()-started); /* extra publication cost, included in NEXT start-to-start period */
    barrier();p[PROFILE_WORDS-1U]=seq;p[0]=seq;barrier();header[4]=seq;
}
