#include "mb_driver.h"
static unsigned clip(int n,unsigned limit){return n<0?0U:((unsigned)n>=limit?limit-1U:(unsigned)n);}
int mb_submit(const MbIo *io,const MtState *s){
 unsigned i,j,mask=0,xy[16]={0},classes[4]={0},start,count,polls=0,status;
 if(io->read(0x4c)!=0x4d423402U)return 1;
 for(i=0;i<4;i++)if(s && s->t[i].active){
  const MfBox *b=&s->t[i].box;
  xy[i*4]=clip(b->x1,1248);xy[i*4+1]=clip(b->y1,624);
  xy[i*4+2]=clip(b->x2,1248);xy[i*4+3]=clip(b->y2,624);
  if(xy[i*4]<xy[i*4+2] && xy[i*4+1]<xy[i*4+3]){
   mask|=1U<<i;classes[i]=b->class_id;
  }
 }
 start=io->ms();
 while(io->read(0x48)&1U)if((unsigned)(io->ms()-start)>=100U || ++polls>=1000000U)return 2;
 count=io->read(0x50);
 for(j=0;j<16;j++)io->write(j*4,xy[j]);
 for(j=0;j<4;j++)io->write(0x54+j*4,classes[j]);
 io->write(0x40,mask);io->barrier();io->write(0x44,1);io->barrier();
 start=io->ms();polls=0;
 while(io->read(0x50)==count)if((unsigned)(io->ms()-start)>=100U || ++polls>=1000000U)return 3;
 status=io->read(0x48);
 if((status&1U) || ((status>>8)&15U)!=mask)return 4;
 return 0;
}
