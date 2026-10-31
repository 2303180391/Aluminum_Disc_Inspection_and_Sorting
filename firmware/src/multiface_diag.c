#include "multiface_diag.h"
#include "multiface.h"
#include "sigmoid_exp_table.h"
#include "yolo_diag.h"
#include "xtime_l.h"
#define MF_BASE (DIAG_BASE+0x4000U)
#define MF_RING 32U
#define MF_WORDS 84U
void track_init(void);
void track_record(void);
/* This page belongs to the uncached diagnostic MB configured in diag_init.
 * Private cached workspace is in .bss, not the 8 KB stack or DMA buffers. */
static volatile u32 *const mf_mail=(volatile u32 *)MF_BASE;
static MfBox workspace[MF_CANDIDATES],strong[4],weak[4];
static const MfTables tables[2]={
 {l15_sigmoid_table,{l15_w0_table,l15_w1_table,l15_w2_table},{l15_h0_table,l15_h1_table,l15_h2_table}},
 {l22_sigmoid_table,{l22_w0_table,l22_w1_table,l22_w2_table},{l22_h0_table,l22_h1_table,l22_h2_table}}
};
void mf_diag_init(void) {
    track_init();
    unsigned i;for(i=0;i<8+MF_RING*MF_WORDS;i++)mf_mail[i]=0;
    mf_mail[0]=0x4D464431U;mf_mail[1]=1;mf_mail[2]=MF_RING;mf_mail[3]=MF_WORDS;
}
static void save_box(volatile u32 *p,const MfBox *b) {
    p[0]=(u32)(b->score*1000000.0f+0.5f);
    p[1]=(u32)b->x1;p[2]=(u32)b->y1;p[3]=(u32)b->x2;p[4]=(u32)b->y2;
    p[5]=b->head;p[6]=b->anchor;p[7]=(b->class_id<<24)|(b->row*32U+b->col);
}
void mf_diag_record(void) {
    unsigned sc=0,wc=0,i,seq=mf_mail[4]+1;
    int sn=0,wn=0,audit=(yolo_diag->frame==1U || yolo_diag->frame%30U==0U);XTime begin,end;volatile u32 *r;
    /* Both heads were invalidated before yolo_detect. That function only reads
     * model outputs; no new forward/capture occurs before this observer ends. */
    XTime_GetTime(&begin);
    if(audit) {
        sn=mf_detect((const u8 *)0x03300000U,4056,(const u8 *)0x03400000U,16224,
                     tables,MF_DIAG_STRONG_THRESHOLD,MF_NMS_PERMILLE,workspace,MF_CANDIDATES,strong,&sc);
        wn=mf_detect((const u8 *)0x03300000U,4056,(const u8 *)0x03400000U,16224,
                     tables,MF_WEAK_THRESHOLD,MF_NMS_PERMILLE,workspace,MF_CANDIDATES,weak,&wc);
    }
    XTime_GetTime(&end);
    r=mf_mail+8+((seq-1)%MF_RING)*MF_WORDS;
    r[0]=0;r[83]=0;
    __asm__ volatile("dsb sy" ::: "memory");
    for(i=1;i<83;i++)r[i]=0;
    r[1]=yolo_diag->frame;r[2]=yolo_diag->input_crc;
    r[3]=(u32)((end-begin)*1000000ULL/COUNTS_PER_SECOND);
    r[4]=sc;r[5]=sn<0?0:(u32)sn;r[6]=wc;r[7]=wn<0?0:(u32)wn;
    r[8]=(u32)sn;r[9]=(u32)wn;
    r[10]=(u32)audit;
    for(i=0;sn>0 && i<(unsigned)sn;i++)save_box(r+16+8*i,&strong[i]);
    for(i=0;wn>0 && i<(unsigned)wn;i++)save_box(r+48+8*i,&weak[i]);
    __asm__ volatile("dsb sy" ::: "memory");
    r[83]=seq;r[0]=seq;
    __asm__ volatile("dsb sy" ::: "memory");
    mf_mail[4]=seq;
    track_record(); /* Full candidates, not the diagnostic top-four list. */
    if(mf_mail[5]==1U) {
        mf_mail[6]=yolo_diag->frame;
        __asm__ volatile("dsb sy" ::: "memory");
        mf_mail[7]=1U;
        while(mf_mail[5]==1U) __asm__ volatile("nop");
        mf_mail[7]=0;
    }
}
