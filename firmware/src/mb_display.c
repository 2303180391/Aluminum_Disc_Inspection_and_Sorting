#include "mb_driver.h"
#include "box_size.h"
#include "xil_io.h"
#include "xtime_l.h"
#include "yolo_diag.h"
static volatile u32 *const logp=(volatile u32 *)0x00E07000U;
static unsigned readreg(unsigned o){return Xil_In32(0x43C10000U+o);}
static void writereg(unsigned o,unsigned v){Xil_Out32(0x43C10000U+o,v);}
static void barrier(void){__asm__ volatile("dsb sy" ::: "memory");}
static unsigned millis(void){XTime t;XTime_GetTime(&t);return (unsigned)(t/(COUNTS_PER_SECOND/1000U));}
static const MbIo io={readreg,writereg,barrier,millis};
static BoxSizeState sizes;
static MtState shown;
void mb_display(const MtState *s){
 unsigned i,start=millis();int rc;
 if(s)box_size_apply(&sizes,s,&shown);
 rc=mb_submit(&io,s?&shown:0);
 logp[2]=(unsigned)rc;logp[3]=millis()-start;
 if(rc==1){diag_fail(12U);} /* no writes made to unknown hardware */
 logp[4]=readreg(0x48);logp[5]=readreg(0x50);
 for(i=0;i<4;i++)logp[8+i]=(s && s->t[i].active)?s->t[i].id:0;
 for(i=0;i<4;i++){
  logp[16+i*4]=(s && s->t[i].active)?(u32)(s->t[i].box.x2-s->t[i].box.x1):0;
  logp[17+i*4]=(s && s->t[i].active)?(u32)(s->t[i].box.y2-s->t[i].box.y1):0;
  logp[18+i*4]=(s && shown.t[i].active)?(u32)(shown.t[i].box.x2-shown.t[i].box.x1):0;
  logp[19+i*4]=(s && shown.t[i].active)?(u32)(shown.t[i].box.y2-shown.t[i].box.y1):0;
 }
 if(rc)diag_fail(13U); /* do not loop indefinitely on a stopped video clock */
 logp[1]++;barrier();
}
void mb_display_init(void){unsigned i;for(i=0;i<32;i++)logp[i]=0;logp[0]=0x4d424434U;mb_display(0);}
