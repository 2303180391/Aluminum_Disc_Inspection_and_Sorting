#include "score_log.h"
#include "yolo_diag.h"
#include "xtime_l.h"
#include "candidate_log.h"
#include "association_policy.h"
#define LOG_BASE (DIAG_BASE+0x600U)
#define CAPACITY 32U
#define WORDS 12U
static volatile u32 *const header=(volatile u32 *)LOG_BASE;
static volatile u32 *const entries=(volatile u32 *)(LOG_BASE+0x20U);
static int valid_box;
static XTime last_strong;
static int saved_box[4];
static AssociationPolicy association;
static volatile u32 *const assoc_diag=(volatile u32 *)(DIAG_BASE+0x2100U);
static int min_int(int a,int b) { return a<b?a:b; }
static int max_int(int a,int b) { return a>b?a:b; }
static int abs_int(int a) { return a<0?-a:a; }
static volatile u32 *const smooth_diag=(volatile u32 *)(DIAG_BASE+0xD00U);
u32 score_reference(int box[4],u32 *age_ms) {
    XTime now;
    unsigned i;
    XTime_GetTime(&now);
    for(i=0;i<4;i++) box[i]=saved_box[i];
    *age_ms=(u32)((now-last_strong)*1000ULL/COUNTS_PER_SECOND);
    return (valid_box?2U:0U) | ((saved_box[2]>saved_box[0] && saved_box[3]>saved_box[1])?4U:0U);
}
void score_log_init(void) {
    unsigned i;
    for(i=0;i<8U+CAPACITY*WORDS;i++) header[i]=0U;
    header[0]=0x53434F31U; header[1]=1U; header[2]=CAPACITY;
    valid_box=0;
    association.active=0;association.misses=0;
    for(i=0;i<12U;i++) assoc_diag[i]=0U;
    assoc_diag[0]=0x41535331U;
    for(i=0;i<12U;i++) smooth_diag[i]=0U;
    smooth_diag[0]=0x534D5431U;
}
int score_hold_low(float s15,float s22) {
    XTime now;
    float best=(s22>s15)?s22:s15;
    XTime_GetTime(&now);
    int box[4];
    unsigned long long ms=now*1000ULL/COUNTS_PER_SECOND;
    unsigned action=ap_weak(&association,ms,best,box,candidate_geometry(box));
    int hold=(action==A_MATCH || action==A_BRIDGE);
    valid_box=association.active;
    score_log(s15,s22,action,hold?saved_box[0]:0,hold?saved_box[1]:0,hold?saved_box[2]:0,hold?saved_box[3]:0);
    return hold;
}
void score_smooth_box(int *x1,int *y1,int *x2,int *y2) {
    int w=*x2-*x1,h=*y2-*y1;
    int pw=saved_box[2]-saved_box[0],ph=saved_box[3]-saved_box[1];
    int cx2=*x1+*x2,cy2=*y1+*y2;
    int iw=min_int(*x2,saved_box[2])-max_int(*x1,saved_box[0]);
    int ih=min_int(*y2,saved_box[3])-max_int(*y1,saved_box[1]);
    XTime now;
    u32 applied=0U;
    XTime_GetTime(&now);
    smooth_diag[1]=yolo_diag->frame;
    smooth_diag[2]=(u32)*x1; smooth_diag[3]=(u32)*y1;
    smooth_diag[4]=(u32)*x2; smooth_diag[5]=(u32)*y2;
    /* Display-size filter only. Current center is NOT averaged. Association
     * is approximate, not identity tracking: overlap >=20%, limited motion
     * and size ratio, plus a recent valid detection are required. */
    if(valid_box && now-last_strong<=(XTime)COUNTS_PER_SECOND*1200U/1000U &&
       w>0 && h>0 && pw>0 && ph>0 && iw>0 && ih>0 &&
       iw*ih*5>=w*h+pw*ph-iw*ih &&
       abs_int(cx2-saved_box[0]-saved_box[2])<=max_int(w,pw) &&
       abs_int(cy2-saved_box[1]-saved_box[3])<=max_int(h,ph) &&
       w<=pw*3 && pw<=w*3 && h<=ph*3 && ph<=h*3) {
        w=(w+pw+1)/2; h=(h+ph+1)/2;
        *x1=(cx2-w)/2; *y1=(cy2-h)/2;
        *x2=*x1+w; *y2=*y1+h;
        *x1=max_int(0,*x1); *y1=max_int(0,*y1);
        *x2=min_int(1247,*x2); *y2=min_int(623,*y2);
        applied=1U;
    }
    smooth_diag[6]=(u32)*x1; smooth_diag[7]=(u32)*y1;
    smooth_diag[8]=(u32)*x2; smooth_diag[9]=(u32)*y2;
    smooth_diag[10]=applied;
    __asm__ volatile("dsb sy" ::: "memory");
    smooth_diag[11]=yolo_diag->frame;
}
void score_log(float s15,float s22,u32 reason,int x1,int y1,int x2,int y2) {
    u32 sequence=header[3]+1U;
    volatile u32 *r=entries+((sequence-1U)%CAPACITY)*WORDS;
    XTime ticks;
    XTime_GetTime(&ticks);
    if(reason==3U) {
        valid_box=1; last_strong=ticks;
        saved_box[0]=x1; saved_box[1]=y1; saved_box[2]=x2; saved_box[3]=y2;
        ap_acquire(&association,ticks*1000ULL/COUNTS_PER_SECOND,saved_box);
    } else if(reason==1U || reason==2U || reason==7U || reason==8U || reason==9U) {valid_box=0;association.active=0;}
    assoc_diag[11]=0;
    assoc_diag[1]=yolo_diag->frame;assoc_diag[2]=reason;assoc_diag[3]=association.active;
    assoc_diag[4]=association.misses;assoc_diag[5]=association.last_iou;
    assoc_diag[6]=(u32)(ticks*1000ULL/COUNTS_PER_SECOND-association.strong_ms);
    assoc_diag[7]=(u32)(ticks*1000ULL/COUNTS_PER_SECOND-association.evidence_ms);
    assoc_diag[8]=3000;assoc_diag[9]=1200;assoc_diag[10]=700;
    __asm__ volatile("dsb sy" ::: "memory");assoc_diag[11]=yolo_diag->frame;
    r[11]=0U; /* invalidate slot before overwriting */
    __asm__ volatile("dsb sy" ::: "memory");
    r[0]=sequence; r[1]=yolo_diag->frame;
    r[2]=(u32)(s15*1000000.0f); r[3]=(u32)(s22*1000000.0f);
    r[4]=reason;
    r[5]=(u32)x1; r[6]=(u32)y1; r[7]=(u32)x2; r[8]=(u32)y2;
    r[9]=(s22>s15)?22U:15U;
    r[10]=(u32)(ticks*1000ULL/COUNTS_PER_SECOND);
    __asm__ volatile("dsb sy" ::: "memory");
    r[11]=sequence;
    __asm__ volatile("dsb sy" ::: "memory");
    header[3]=sequence;
}
