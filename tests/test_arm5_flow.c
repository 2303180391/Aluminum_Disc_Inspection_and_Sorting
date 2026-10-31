#define ARM5_HOST_TEST
#include "../firmware/src/arm_sort.c"
static unsigned hw,reply,flags=2,pose,loc_ok=1,g_count,k_count,b_count,k_class,claw=37,teach_count;
static unsigned long long clock_ms;
volatile DiscResult disc_result;
static unsigned locator_resets;
void disc_locator_reset_calibration(void){locator_resets++;}
void XTime_GetTime(XTime *out){*out=clock_ms;}
void xil_printf(const char *fmt,...){(void)fmt;}
unsigned Xil_In32(unsigned addr){return addr==ARM5_BASE+0x6c?ARM5_ID:hw;}
void Xil_Out32(unsigned addr,unsigned value){
 (void)addr;
 if(!value){hw=0;reply=5;return;}
 hw=(hw&~3U)|(value&3U)|8U;
 if(value&4U)hw&=~4U;
 if(value&64U)hw|=6U;
}
unsigned arm5_read(unsigned i){return i==7?reply:i==12?flags:(i==10 || i==8)?pose:i==9?claw:0;}
unsigned arm5_issue(unsigned c,unsigned n,const int *a){
 (void)n;reply=2;
 if(c=='Q')reply=14;
 if(c=='D')pose=(unsigned)a[2]|((unsigned)a[3]<<8)|((unsigned)a[4]<<16)|((unsigned)a[5]<<24);
 if(c=='G'){g_count++;hw|=16;}
 if(c=='B'){b_count++;hw|=16;}
 if(c=='K'){k_count++;k_class=(unsigned)a[0];hw|=16;}
 if(c=='H')hw|=16;
 if(c=='O'){teach_count++;flags|=1U;}
 if(c=='X'){hw&=~16U;reply=5;}
 return reply;
}
unsigned disc_board_measure_for_arm(unsigned token){
 (void)token;disc_result.x_um=49738;disc_result.y_um=54111;return loc_ok;
}
#define CHECK(c,n) do{if(!(c))return n;}while(0)
static int eq(const char*a,const char*b){while(*a && *a==*b){a++;b++;}return *a==*b;}
static void complete(void){hw&=~16U;reply=4;arm5_poll();}
int main(void){
 unsigned i;ArmRemoteSnapshot snap;
 arm5_init();CHECK(calibrated,1);CHECK(!hw,2);
 CHECK(eq(arm_remote_command(REMOTE_ENABLE),"OK"),3);complete();CHECK(phase==A_IDLE,4);
 CHECK(eq(arm_remote_command(REMOTE_START),"OBSERVE_NOT_TAUGHT"),5);
 CHECK(eq(arm_remote_command(REMOTE_LOCATE),"OBSERVE_NOT_TAUGHT"),28);
 claw=60;CHECK(eq(arm_remote_command(REMOTE_TEACH),"CLAW_NOT_OPEN") && !teach_count,29);
 claw=37;CHECK(eq(arm_remote_command(REMOTE_TEACH),"OK") && teach_count==1 && flags==3,30);
 CHECK(locator_resets==1,43);
 CHECK(eq(arm_remote_command(REMOTE_START),"OK"),6);arm5_poll();
 CHECK(phase==A_OBSERVE_MOVE && b_count==1,7);
 CHECK(eq(arm_remote_command(REMOTE_START),"BUSY"),8);
 complete();clock_ms+=2001;loc_ok=0;arm5_poll();
 CHECK(phase==A_ERROR && g_count==0 && k_count==0,9);
 CHECK(!xy_valid,31);
 CHECK(eq(arm_remote_command(REMOTE_RETRY),"OK"),10);
 loc_ok=1;CHECK(eq(arm_remote_command(REMOTE_START),"OK"),11);arm5_poll();
 complete();clock_ms+=2001;arm5_poll();
 CHECK(phase==A_APPROACH && g_count==1 && x_mm==50 && y_mm==54,12);
 reply=13;arm5_poll();CHECK(phase==A_CLASSIFY,13);
 arm_sort_frame(clock_ms,1,0,0,0,1);CHECK(k_count==0,14);
 clock_ms+=3001;arm_sort_frame(clock_ms,2,0,0,0,1);
 for(i=3;i<8;i++){clock_ms+=400;arm_sort_frame(clock_ms,i,0,0,0,1);}
 CHECK(k_count==1 && phase==A_SORT && arm_sort_result==0,15);
 CHECK(eq(arm_remote_command(REMOTE_START),"BUSY"),16);
 complete();CHECK(phase==A_IDLE && !(hw&6U),17);
 CHECK(eq(arm_remote_command(REMOTE_START),"OK"),18);arm5_poll();
 CHECK(eq(arm_remote_command(REMOTE_DISABLE),"OK"),19);
 CHECK(locator_resets>=2,44);
 CHECK(!hw && phase==A_IDLE,20);
 mail[33]=0x101;mail[34]=0;mail[32]=101;arm5_poll();
 CHECK(mail[41]==101 && mail[42]==2 && phase==A_HOME,21);
 mail[33]=0x102;mail[32]=102;arm5_poll();
 CHECK(mail[41]==102 && mail[42]==11,22);
 mail[33]=0x103;mail[32]=103;arm5_poll();
 CHECK(mail[41]==103 && mail[42]==2 && !hw && phase==A_IDLE,23);
 for(i=0;i<3;i++){
  unsigned f,before=k_count;MfBox box={0};box.class_id=i;box.score=1.0f;
  hw=27;phase_set(A_CLASSIFY);policy=(SortPolicy){0};policy.state=SORT_OBSERVE;policy.since=clock_ms;
  for(f=1;f<=5;f++){clock_ms+=400;arm_sort_frame(clock_ms,f,&box,1,1,1);}
  CHECK(k_count==before+1 && k_class==i+1 && phase==A_SORT,24);
  complete();CHECK(phase==A_IDLE,25);
 }
 {
  unsigned before=k_count;hw=27;phase_set(A_CLASSIFY);policy=(SortPolicy){0};
  policy.state=SORT_OBSERVE;policy.since=clock_ms;
  for(i=1;i<=6;i++){clock_ms+=400;arm_sort_frame(clock_ms,i,0,0,1,1);}
  CHECK(k_count==before,26);
  clock_ms+=10001;arm_sort_frame(clock_ms,7,0,0,1,1);
  CHECK(phase==A_ERROR && reason==23 && k_count==before,27);
 }
 CHECK(eq(arm_remote_command(REMOTE_RETRY),"OK"),32);
 {
  unsigned before_g=g_count,before_k=k_count,before_b=b_count;
  CHECK(eq(arm_remote_command(REMOTE_LOCATE),"OK"),33);arm5_poll();
  CHECK(phase==A_OBSERVE_MOVE && locate_only && b_count==before_b+1,34);
  CHECK(eq(arm_remote_command(REMOTE_TEACH),"BUSY"),35);
  CHECK(eq(arm_remote_command(REMOTE_LOCATE),"BUSY"),36);
  complete();clock_ms+=2001;arm5_poll();
  CHECK(phase==A_IDLE && !(hw&6U) && xy_valid && x_mm==50 && y_mm==54,37);
  CHECK(g_count==before_g && k_count==before_k,38);
  arm_remote_snapshot(&snap);
  CHECK(snap.phase==A_IDLE && snap.xy_valid && snap.locate_only && snap.x_mm==50 && snap.y_mm==54 && snap.flags==3 && snap.calibrated,39);
  CHECK(snap.claw==37 && snap.pose==pose && snap.measure==sequence,40);
  /* An idle physical key starts the full path, never the previous locate-only mode. */
  hw|=6U;arm5_poll();CHECK(phase==A_OBSERVE_MOVE && !locate_only && !xy_valid,41);
  complete();clock_ms+=2001;arm5_poll();
  CHECK(phase==A_APPROACH && g_count==before_g+1,42);
  CHECK(eq(arm_remote_command(REMOTE_DISABLE),"OK") && !xy_valid && !request_kind_pending && !locate_only,43);
 }
 CHECK(eq(arm_remote_command(REMOTE_TEACH),"NOT_READY"),44);
 CHECK(eq(arm_remote_command(REMOTE_ENABLE),"OK"),45);complete();
 mail[33]=0x105;mail[34]=0;mail[32]=105;arm5_poll();
 CHECK(mail[41]==105 && mail[42]==2 && teach_count==2,46);
 mail[33]=0x106;mail[32]=106;arm5_poll();
 CHECK(mail[41]==106 && mail[42]==2 && phase==A_OBSERVE_MOVE && locate_only,47);
 complete();clock_ms+=2001;arm5_poll();
 CHECK(phase==A_IDLE && xy_valid && locate_only,48);
 CHECK(eq(arm_remote_command(REMOTE_TEACH),"OK") && !xy_valid,49);
 return 0;
}
