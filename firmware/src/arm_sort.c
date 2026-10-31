#include "arm_sort.h"
#include "sort_policy.h"
#include "arm_remote.h"
#include "arm5_io.h"
#include "disc_locator.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xtime_l.h"
#include "arm5_calibration.h"
#define CMD (ARM5_BASE+0x64)
#define STATUS (ARM5_BASE+0x68)
/* Host mailbox: 32=request token,33=ASCII command,34=argc,35..40=args,
 * 41=completed token,42=reply. Write request token last. */
#ifdef ARM5_HOST_TEST
static volatile unsigned mail[64];
#define ARM5_BARRIER() ((void)0)
#else
static volatile unsigned *const mail=(volatile unsigned *)0x00E00900U;
#define ARM5_BARRIER() __asm__ volatile("dsb sy" ::: "memory")
#endif
static SortPolicy policy;
enum { A_IDLE,A_HOME,A_OBSERVE_MOVE,A_LOCATE_SETTLE,A_APPROACH,A_CLASSIFY,A_SORT,A_ERROR };
volatile unsigned arm_sort_state,arm_sort_result,arm_sort_samples;
static unsigned phase,reason,calibrated,sequence,x_mm,y_mm;
static unsigned xy_valid,locate_only,request_kind_pending,requested_mode;
static unsigned reported_state=~0U,reported_reason=~0U,reported_result=~0U;
static uint64_t since;
extern volatile DiscResult disc_result;
static uint64_t millis(void){XTime t;XTime_GetTime(&t);return t/(COUNTS_PER_SECOND/1000U);}
static void phase_set(unsigned next){phase=next;since=millis();}
static void fail(unsigned why){
 reason=why;xy_valid=0;request_kind_pending=0;(void)arm5_issue('X',0,0);phase_set(A_ERROR);
 Xil_Out32(CMD,(Xil_In32(STATUS)&1U)?7U:0U);
 xil_printf("ARM5 ERROR reason=%d\r\n",why);
}
static unsigned calibration_load(void){
 int a[6]={100,100,0,0,0,0};unsigned i,j;
 if(arm5_issue('W',2,a)!=2)return 0;
 for(i=0;i<18;i++){
  a[0]=arm5_calibration[i][0];a[1]=arm5_calibration[i][1];
  for(j=0;j<4;j++)a[j+2]=arm5_calibration[i][j+2];
  if(arm5_issue('D',6,a)!=2)return 0;
  if(arm5_issue('Q',2,a)!=14)return 0;
  if(arm5_read(10)!=((unsigned)a[2]|((unsigned)a[3]<<8)|((unsigned)a[4]<<16)|((unsigned)a[5]<<24)))return 0;
 }
 a[0]=2;return arm5_issue('E',1,a)==2 && (arm5_read(12)&2U);
}
void arm5_init(void){
 unsigned i;for(i=0;i<64;i++)mail[i]=0;
 mail[0]=0x41354C31U;mail[1]=1;
 Xil_Out32(CMD,0U);
 calibrated=calibration_load();
 if(!calibrated){reason=10;phase=A_ERROR;}
 xil_printf("ARM5_READY calibration=%d pwm=0 observation_taught=0\r\n",calibrated);
}
void arm_remote_snapshot(ArmRemoteSnapshot *s){
 unsigned i;
 s->state=(phase==A_ERROR)?SORT_ERROR:(phase==A_IDLE?SORT_IDLE:
          phase==A_CLASSIFY?policy.state:phase==A_SORT?SORT_RUNNING:SORT_SETTLE);
 s->reason=reason;s->result=arm_sort_result;s->samples=policy.samples;
 s->frame=policy.last_frame;s->window=policy.window_count;
 for(i=0;i<4;i++)s->votes[i]=policy.votes[i];
 s->uncertain=policy.uncertain;s->hw=Xil_In32(STATUS);
 s->phase=phase;s->flags=arm5_read(12)&3U;s->x_mm=x_mm;s->y_mm=y_mm;
 s->xy_valid=xy_valid;s->measure=sequence;s->locate_only=locate_only;
 s->calibrated=calibrated;s->pose=arm5_read(8);s->claw=arm5_read(9);
}
const char *arm_remote_command(unsigned c){
 unsigned s=Xil_In32(STATUS);
 if(Xil_In32(ARM5_BASE+0x6c)!=ARM5_ID)return "BAD_HARDWARE";
 if(c==REMOTE_DISABLE){
  disc_locator_reset_calibration();
  Xil_Out32(CMD,0);policy=(SortPolicy){0};phase_set(A_IDLE);reason=0;
  xy_valid=0;request_kind_pending=0;locate_only=0;
  return "OK";
 }
 if(c==REMOTE_RETRY){
  if(phase!=A_ERROR)return "NOT_ERROR";
  if(s&16U)return "BUSY";
  if(!calibrated)return "BAD_CALIBRATION";
  policy=(SortPolicy){0};reason=0;phase_set(A_IDLE);
  Xil_Out32(CMD,(s&1U)?5U:0U);return "OK";
 }
 if(phase!=A_IDLE || (s&0x16U))return "BUSY";
 if(c==REMOTE_ENABLE){
  if(s&1U)return "ALREADY_ENABLED";
  if(!calibrated)return "BAD_CALIBRATION";
  Xil_Out32(CMD,3U);
  if(arm5_issue('H',0,0)!=2){fail(11);return "HOME_FAILED";}
  phase_set(A_HOME);return "OK";
 }
 if(c==REMOTE_TEACH){
  if(!(s&1U) || !(s&8U))return "NOT_READY";
  if(!calibrated)return "BAD_CALIBRATION";
  if(arm5_read(9)!=37U)return "CLAW_NOT_OPEN";
  if(arm5_issue('O',0,0)!=2)return "OBSERVE_TEACH_FAILED";
  disc_locator_reset_calibration();
  xy_valid=0;return "OK";
 }
 if(c==REMOTE_START || c==REMOTE_LOCATE){
  if(!(s&1U) || !(s&8U))return "NOT_READY";
  if(!(arm5_read(12)&1U))return "OBSERVE_NOT_TAUGHT";
  if(!calibrated)return "BAD_CALIBRATION";
  requested_mode=c==REMOTE_LOCATE;request_kind_pending=1;
  Xil_Out32(CMD,0x41U);return "OK";
 }
 return "UNKNOWN_CMD";
}
static void host_command(void){
 unsigned token=mail[32],cmd=mail[33],n=mail[34],i,r=3;int a[6];
 if(!token)return;
 mail[32]=0;
 for(i=0;i<6;i++)a[i]=(int)mail[35+i];
 if(cmd>=0x101U && cmd<=0x106U && n==0){
  const char *answer=arm_remote_command(cmd-0x100U);
  r=(answer[0]=='O' && answer[1]=='K' && answer[2]==0)?2U:11U;
 }else if(phase==A_IDLE && !(Xil_In32(STATUS)&0x16U)){
  if(n<=6 && (cmd=='J' || cmd=='O' || cmd=='B' || cmd=='H' || cmd=='Q' || cmd=='S')){
   r=arm5_issue(cmd,n,a);
   if(r==2 && cmd=='O')disc_locator_reset_calibration();
   if(r==2 && (cmd=='J' || cmd=='B' || cmd=='H')){
    Xil_Out32(CMD,3U);phase_set(A_HOME);
   }
  }else r=10;
 }
 mail[42]=r;ARM5_BARRIER();mail[41]=token;
}
void arm5_poll(void){
 unsigned s,r;uint64_t now=millis();int a[2];
 host_command();s=Xil_In32(STATUS);r=arm5_read(7)&15U;
 if(!(s&1U) && phase!=A_IDLE && phase!=A_ERROR){
  phase_set(A_IDLE);policy=(SortPolicy){0};xy_valid=0;request_kind_pending=0;
 }
 if(phase==A_IDLE && (s&4U)){
  /* Physical key always starts a full cycle, never reuses the last locate mode. */
  locate_only=request_kind_pending?requested_mode:0;
  request_kind_pending=0;xy_valid=0;
  Xil_Out32(CMD,7U);reason=0;policy=(SortPolicy){0};
  if(!calibrated || !(arm5_read(12)&1U))fail(12);
  else if(arm5_issue('B',0,0)!=2)fail(13);
  else phase_set(A_OBSERVE_MOVE);
 }else if(phase==A_HOME && !(s&16U) && r==4){Xil_Out32(CMD,5U);phase_set(A_IDLE);}
 else if(phase==A_OBSERVE_MOVE && !(s&16U) && r==4)phase_set(A_LOCATE_SETTLE);
 else if(phase==A_LOCATE_SETTLE && now-since>=2000){
  if(!disc_board_measure_for_arm(++sequence)){
   unsigned current=Xil_In32(0x00E800FCU)==sequence && Xil_In32(0x00E8001CU)==sequence;
   xil_printf("ARM5_LOC_FAIL sequence=%d result_current=%d transport=%d geometry=%d frame_valid=%d origin_valid=%d disc_valid=%d\r\n",
              sequence,current,Xil_In32(0x00E80014U),disc_result.status,
              disc_result.frame_valid,disc_result.origin_valid,disc_result.valid);
   fail(14);
  }
  else if(disc_result.x_um<0 || disc_result.y_um<0 || disc_result.x_um>100000 || disc_result.y_um>100000)fail(15);
  else {
   x_mm=(disc_result.x_um+500)/1000;y_mm=(disc_result.y_um+500)/1000;
   xy_valid=1;
   a[0]=(int)x_mm;a[1]=(int)y_mm;
   xil_printf("ARM5_LOC X_mm=%d Y_mm=%d sequence=%d\r\n",x_mm,y_mm,sequence);
   if(locate_only){
    Xil_Out32(CMD,5U);phase_set(A_IDLE);
    xil_printf("ARM5_LOCATE_ONLY_DONE X_mm=%d Y_mm=%d\r\n",x_mm,y_mm);
   }else if(arm5_issue('G',2,a)!=2)fail(16);else phase_set(A_APPROACH);
  }
 }else if(phase==A_APPROACH && r==13){
  policy=(SortPolicy){0};policy.state=SORT_SETTLE;policy.since=now;
  phase_set(A_CLASSIFY);
 }else if(phase==A_SORT && !(s&16U) && r==4){
  Xil_Out32(CMD,5U);phase_set(A_IDLE);
  xil_printf("ARM5_DONE class=%d X_mm=%d Y_mm=%d\r\n",arm_sort_result,x_mm,y_mm);
 }
 if(phase!=A_IDLE && phase!=A_ERROR && millis()-since>180000)fail(17);
 arm_sort_state=(phase==A_ERROR)?SORT_ERROR:phase==A_IDLE?SORT_IDLE:phase==A_CLASSIFY?policy.state:phase==A_SORT?SORT_RUNNING:SORT_SETTLE;
 arm_sort_samples=policy.samples;
 if(reported_state!=arm_sort_state || reported_reason!=reason || reported_result!=arm_sort_result){
  xil_printf("ARM_SORT state=%d result=%d samples=%d frame=%d reason=%d mixed=0\r\n",
             arm_sort_state,arm_sort_result,arm_sort_samples,policy.last_frame,reason);
  reported_state=arm_sort_state;reported_reason=reason;reported_result=arm_sort_result;
 }
 mail[2]=phase;mail[3]=reason;mail[4]=x_mm;mail[5]=y_mm;mail[6]=policy.last_frame;
 mail[7]=Xil_In32(STATUS);mail[8]=arm_sort_result;mail[9]=arm5_read(12);
 mail[10]=arm5_read(8);mail[11]=arm5_read(9);mail[12]=calibrated;mail[13]=sequence;
 mail[14]=xy_valid;mail[15]=locate_only;
}
void arm_sort_frame(uint64_t ms,unsigned frame,const MfBox *boxes,unsigned count,unsigned any,unsigned valid){
 unsigned i,mask=0;int a[1];
 if(phase!=A_CLASSIFY)return;
 for(i=0;i<count;i++)if(mf_is_strong(&boxes[i]))mask|=1U<<boxes[i].class_id;
 /* Controller is holding WAIT_CLASS; its busy flag is expected here. */
 if(sort_update(&policy,ms,frame,0,valid,mask,any,0,0)){
  arm_sort_result=policy.result;a[0]=(int)policy.result;
  if(arm5_issue('K',1,a)!=2)fail(18);
  else {
   /* Classification and motion start can share the same inference frame. */
   xil_printf("ARM_SORT state=3 result=%d samples=%d frame=%d reason=0 mixed=0\r\n",
              arm_sort_result,policy.samples,policy.last_frame);
   phase_set(A_SORT);
  }
 }else if(policy.state==SORT_ERROR)fail(20+policy.reason);
}
