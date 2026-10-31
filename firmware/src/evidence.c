#include "evidence.h"
#include "xil_types.h"
#include <string.h>
/* Uncached diagnostic MB; existing track ring ends at 0x00E12020. */
#define EV_RING 128U
#define EV_WORDS 528U
#define EV_LIMIT 32U
#ifdef EVIDENCE_HOST_TEST
#define EV_BARRIER() ((void)0)
static volatile u32 *const ev=host_ev;
static volatile u32 *const exact=host_exact;
#else
#define EV_BARRIER() __asm__ volatile("dsb sy" ::: "memory")
static volatile u32 *const ev=(volatile u32 *)0x00E20000U;
/* 0xE70000..0xE7901F: separate ring inside existing uncached MiB. */
static volatile u32 *const exact=(volatile u32 *)0x00E70000U;
#endif
#define EXACT_WORDS 72U
typedef char float_word_size[(sizeof(float)==sizeof(u32))?1:-1];
static u32 float_bits(float f){u32 bits;memcpy(&bits,&f,sizeof bits);return bits;}
static MfBox raw_best[EV_LIMIT];
static unsigned raw_count,raw_saved;
void evidence_init(void){unsigned i;for(i=0;i<8+EV_RING*EV_WORDS;i++)ev[i]=0;
 ev[0]=0x45564431U;ev[1]=1;ev[2]=EV_RING;ev[3]=EV_WORDS;
 for(i=0;i<8+EV_RING*EXACT_WORDS;i++)exact[i]=0;
 exact[0]=0x46505331U;exact[1]=1;exact[2]=EV_RING;exact[3]=EXACT_WORDS;
}
void evidence_raw(const MfBox *boxes,unsigned count){
 unsigned i,j;raw_count=count;raw_saved=0;
 for(i=0;i<count;i++){
  j=raw_saved;
  if(j==EV_LIMIT){if(boxes[i].score<=raw_best[j-1].score)continue;j--;}
  else raw_saved++;
  while(j && boxes[i].score>raw_best[j-1].score){raw_best[j]=raw_best[j-1];j--;}
  raw_best[j]=boxes[i];
 }
}
static void put_box(volatile u32 *p,const MfBox *b){
 p[0]=(u32)(b->score*1000000.0f+.5f);p[1]=(u32)b->x1;p[2]=(u32)b->y1;
 p[3]=(u32)b->x2;p[4]=(u32)b->y2;p[5]=b->head;p[6]=b->anchor;p[7]=(b->class_id<<24)|(b->row*32U+b->col);
}
void evidence_record(const MfBox *boxes,unsigned count,unsigned frame,unsigned phase,unsigned track_seq){
 unsigned i,n=count<EV_LIMIT?count:EV_LIMIT;volatile u32 *p=ev+8+((track_seq-1)%EV_RING)*EV_WORDS;
 volatile u32 *q=exact+8+((track_seq-1)%EV_RING)*EXACT_WORDS;
 p[0]=0;p[EV_WORDS-1]=0;q[0]=0;q[71]=0;EV_BARRIER();
 for(i=1;i<EV_WORDS-1;i++)p[i]=0;
 p[1]=frame;p[2]=phase;p[3]=raw_count;p[4]=count;p[5]=raw_saved;p[6]=n;
 p[7]=(raw_count>EV_LIMIT?1U:0U)|(count>EV_LIMIT?2U:0U);
 for(i=0;i<raw_saved;i++)put_box(p+8+i*8,&raw_best[i]);
 for(i=0;i<n;i++)put_box(p+264+i*8,&boxes[i]);
 for(i=1;i<71;i++)q[i]=0;
 q[1]=frame;q[2]=phase;q[3]=raw_saved;q[4]=n;q[5]=p[7];
 for(i=0;i<raw_saved;i++)q[7+i]=float_bits(raw_best[i].score);
 for(i=0;i<n;i++)q[39+i]=float_bits(boxes[i].score);
 EV_BARRIER();p[EV_WORDS-1]=track_seq;p[0]=track_seq;q[71]=track_seq;q[0]=track_seq;
 EV_BARRIER();ev[4]=track_seq;exact[4]=track_seq;
}
