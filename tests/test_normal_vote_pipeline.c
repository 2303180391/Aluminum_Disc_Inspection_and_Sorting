#include <stdio.h>
#include <stdlib.h>
#define main original_arm5_flow_main
#include "test_arm5_flow.c"
#undef main

/* Feed decoded proposals through the real geometry, dedup, tracker and ARM5
 * sorting flow. Only accelerator decoding and hardware I/O are fixtures. */
static MfBox fixture[8];
static unsigned fixture_count;
int decode_fixture(const unsigned char *,unsigned,unsigned,const MfTables *,
                   unsigned,unsigned,unsigned,MfBox *);
#define TRACK_DIAG_HOST_TEST
#define mail track_mail
#define mf_decode decode_fixture
#ifdef LEGACY_NORMAL_VOTE
#include "legacy_track_diag_for_test.c"
#else
#include "../firmware/src/track_diag.c"
#endif
#undef mf_decode
#undef mail
static YoloDiag diag_store;
volatile YoloDiag *const yolo_diag=&diag_store;
static unsigned recorded_kept,recorded_raw,checks;
void evidence_init(void){}
void evidence_raw(const MfBox *b,unsigned n){(void)b;recorded_raw=n;}
void evidence_record(const MfBox *b,unsigned n,unsigned f,unsigned p,unsigned s){
 (void)b;(void)f;(void)p;(void)s;recorded_kept=n;
}
void mb_display(const MtState *s){(void)s;}
void diag_fail(u32 error){fprintf(stderr,"Unexpected diag_fail=%u\n",error);abort();}
int decode_fixture(const unsigned char *p,unsigned bytes,unsigned head,const MfTables *t,
                   unsigned k,unsigned row,unsigned col,MfBox *b){
 (void)p;(void)bytes;(void)t;
 if(head!=15 || k || row || col>=fixture_count)return 0;
 *b=fixture[col];return 1;
}
#define VERIFY(c) do{checks++;if(!(c)){fprintf(stderr,"FAIL line=%d: %s\n",__LINE__,#c);return 1;}}while(0)
static MfBox proposal(unsigned c,float score,int x,int y,int w,int h){
 MfBox b={0};b.class_id=c;b.score=score;b.x1=x;b.y1=y;b.x2=x+w;b.y2=y+h;return b;
}
static void begin_case(void){
 phase_set(A_CLASSIFY);policy=(SortPolicy){0};policy.state=SORT_OBSERVE;
 policy.since=clock_ms;hw=27;state=(MtState){0};fixture_count=0;track_init();
}
static void tick(void){clock_ms+=230;diag_store.frame++;track_record();}
int main(void){
 unsigned i,c,before;
 VERIFY(original_arm5_flow_main()==0);

 /* The original failure: high-score border and implausible proposals are
  * rejected upstream but their counts previously vetoed every normal vote. */
 begin_case();before=k_count;
 fixture[0]=proposal(0,1.0f,10,50,50,160);
 fixture[1]=proposal(1,1.0f,400,200,300,200);fixture_count=2;
 for(i=0;i<4;i++){tick();VERIFY(k_count==before);}
 tick();VERIFY(recorded_raw==0 && recorded_kept==0);
 VERIFY(k_count==before+1 && k_class==0 && phase==A_SORT);
 VERIFY(policy.samples==5 && policy.votes[0]==5 && !policy.uncertain);
 VERIFY(track_mail[6]==0x4E564631U);

 /* A real weak proposal is still uncertain, even if nothing is drawn yet. */
 begin_case();before=k_count;
 fixture[0]=proposal(0,.8f,450,200,60,180);fixture_count=1;
 for(i=0;i<42;i++){tick();VERIFY(k_count==before);}
 VERIFY(recorded_raw==1 && recorded_kept==1 && policy.uncertain==5);
 clock_ms+=500;diag_store.frame++;track_record();
 VERIFY(phase==A_ERROR && reason==23 && k_count==before);

 /* All three positive classifications still use the same five-frame window
  * and original class-specific thresholds, including a rejected background. */
 for(c=0;c<3;c++){
  begin_case();before=k_count;
  fixture[0]=proposal(c,c==1?.995f:.96f,450,220,c==1?50:80,c==1?30:80);
  fixture[1]=proposal(0,1.0f,0,100,60,160);fixture_count=2;
  for(i=0;i<4;i++){tick();VERIFY(k_count==before);}
  tick();VERIFY(k_count==before+1 && k_class==c+1 && phase==A_SORT);
 }

 /* Border evidence does not delay a valid class's existing four-of-five vote. */
 begin_case();before=k_count;
 fixture[0]=proposal(0,1.0f,0,100,60,160);fixture_count=1;tick();
 fixture[1]=proposal(1,.995f,450,250,50,30);fixture_count=2;
 for(i=0;i<4;i++)tick();
 VERIFY(k_count==before+1 && k_class==2 && policy.votes[2]==4);

 /* A weak in-window proposal must age out before five normal votes exist. */
 begin_case();before=k_count;
 fixture[0]=proposal(0,.8f,450,200,60,180);fixture_count=1;tick();
 fixture[0]=proposal(0,1.0f,0,100,60,160);
 for(i=0;i<4;i++){tick();VERIFY(k_count==before);}
 tick();VERIFY(k_count==before+1 && k_class==0);

 /* Sustained credible conflicting classes must never default to normal. */
 begin_case();before=k_count;
 fixture[0]=proposal(0,.99f,300,200,60,150);
 fixture[1]=proposal(1,1.0f,700,300,50,30);fixture_count=2;
 for(i=0;i<42;i++){tick();VERIFY(k_count==before);}
 clock_ms+=500;diag_store.frame++;track_record();
 VERIFY(phase==A_ERROR && reason==22 && k_count==before);

 /* Invalid inputs retain the error exit, with no class command. */
 begin_case();before=k_count;
 arm_sort_frame(clock_ms,++diag_store.frame,0,0,0,0);
 VERIFY(phase==A_ERROR && reason==21 && k_count==before);

 /* Classification is reachable only after a successful current localization;
  * an empty scene must not become a normal sorting request. */
 loc_ok=0;phase_set(A_LOCATE_SETTLE);since=clock_ms;clock_ms+=2001;
 before=k_count;arm5_poll();
 VERIFY(phase==A_ERROR && reason==14 && k_count==before && !xy_valid);
 printf("NORMAL_VOTE_PIPELINE_PASS checks=%u cases=10\n",checks);
 return 0;
}
