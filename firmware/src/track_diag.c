#include "tracks.h"
#include "mb_driver.h"
#include "evidence.h"
#include "yolo_diag.h"
#include "xtime_l.h"
#include "sigmoid_exp_table.h"
#include "xil_printf.h"
#include "arm_sort.h"
static MtState state;
static MfBox candidates[MF_CANDIDATES];
#ifdef TRACK_DIAG_HOST_TEST
static volatile u32 track_test_mail[10248];
static volatile u32 *const mail=track_test_mail;
#define TRACK_BARRIER() ((void)0)
#else
static volatile u32 *const mail=(volatile u32 *)0x00E08000U;
#define TRACK_BARRIER() __asm__ volatile("dsb sy" ::: "memory")
#endif
static const MfTables tables[2]={
 {l15_sigmoid_table,{l15_w0_table,l15_w1_table,l15_w2_table},{l15_h0_table,l15_h1_table,l15_h2_table}},
 {l22_sigmoid_table,{l22_w0_table,l22_w1_table,l22_w2_table},{l22_h0_table,l22_h1_table,l22_h2_table}}};
static const char *const class_names[MF_CLASSES]={"scratch","dent","stain"};
void track_record(void){
 unsigned h,k,r,c,n,raw=0,rejected=0,edge_rejected=0,kept,i,seq=mail[4]+1;XTime start,end,step_start;MtTime ms;volatile u32 *p;int status;
 XTime_GetTime(&start);ms=start/(COUNTS_PER_SECOND/1000U);
 for(h=0;h<2;h++){n=h?26:13;
  for(k=0;k<3;k++)for(r=0;r<n;r++)for(c=0;c<n;c++){
   MfBox b;
   if(mf_decode((const u8 *)(h?0x03400000U:0x03300000U),n*n*24,h?22:15,&tables[h],k,r,c,&b) && b.score>=MF_WEAK_THRESHOLD){
    if(mt_edge_clipped(&b))edge_rejected++;
    else if(mt_plausible_acquisition(&b))candidates[raw++]=b;
    else rejected++;
   }
  }
 }
 evidence_raw(candidates,raw);
 kept=mt_dedup(candidates,raw);
 /* NVF1: use the same accepted fresh candidates for positive and normal votes.
  * Boxes already rejected by the unchanged edge/geometry guards are not
  * defect evidence. Kept weak candidates still abstain; never turn a timeout
  * or a retained display box into a normal result. */
 arm_sort_frame(ms,yolo_diag->frame,candidates,kept,kept,1U);
 XTime_GetTime(&step_start);status=mt_step(&state,candidates,kept,ms);XTime_GetTime(&end);
 p=mail+8+((seq-1)%128)*80;p[0]=0;p[79]=0;
 TRACK_BARRIER();
 for(i=1;i<79;i++)p[i]=0;
 p[1]=yolo_diag->frame;p[2]=(u32)ms;p[3]=(u32)(ms>>32);p[4]=raw;p[5]=kept;
 p[6]=(u32)((end-start)*1000000ULL/COUNTS_PER_SECOND);p[7]=(u32)status;p[8]=state.removed;
 for(i=0;i<state.removed;i++){p[10+i*2]=state.removed_id[i];p[11+i*2]=state.removed_reason[i];}
 p[18]=mail[5]; /* Host phase marker: 0=unset, 1=three, 2=middle removed, 3=returned. */
 /* Previously reserved words, retaining the version-2 80-word record layout. */
 p[19]=motion_used_mask;p[20]=motion_ambiguous;
 p[21]=(u32)((end-step_start)*1000000ULL/COUNTS_PER_SECOND);
 p[22]=0x4D4F5431U; /* MOT1: motion candidate, not the baseline firmware. */
 p[23]=0U; /* Two bits per internal slot: 0=scratch, 1=dent, 2=stain. */
 p[72]=rejected;
 p[73]=handoff_merged;
 p[74]=edge_rejected;
 p[75]=weak_motion_used_mask;
 p[76]=scene_motion_detected;
 p[77]=motion_fast_cleared;
 p[78]=weak_tentative_acquired;
 for(i=0;i<4;i++)if(state.t[i].active){MtTrack *t=&state.t[i];volatile u32 *o=p+24+i*12;
  p[9]++;o[0]=t->id;o[1]=t->reason;o[2]=t->misses;o[3]=(u32)(ms-t->strong);o[4]=(u32)(ms-t->evidence);
  o[5]=(u32)t->box.x1;o[6]=(u32)t->box.y1;o[7]=(u32)t->box.x2;o[8]=(u32)t->box.y2;o[9]=(u32)(t->box.score*1000000.0f+.5f);
  o[10]=t->weak_hits;o[11]=t->weak_until>ms?(u32)(t->weak_until-ms):0;
  p[23]|=(t->box.class_id&3U)<<(i*2U);
  if((yolo_diag->frame%30U)==0U)xil_printf("DEFECT frame=%d id=%d class=%s score_ppm=%d box=%d,%d,%d,%d\r\n",
   yolo_diag->frame,t->id,class_names[t->box.class_id],o[9],t->box.x1,t->box.y1,t->box.x2,t->box.y2);
 }
 TRACK_BARRIER();p[79]=seq;p[0]=seq;
 TRACK_BARRIER();mail[4]=seq;
 evidence_record(candidates,kept,yolo_diag->frame,p[18],seq);
 if(status!=0)diag_fail(14U);
 mb_display(&state);
}
void track_init(void){unsigned i;for(i=0;i<10248;i++)mail[i]=0;mail[0]=0x4D545231U;mail[1]=2;mail[2]=128;mail[3]=80;mail[6]=0x4E564631U;evidence_init();}
