#include "arm5_io.h"
#include "xil_io.h"
#include "xtime_l.h"
unsigned arm5_read(unsigned index){
 Xil_Out32(ARM5_BASE+0x74,index);return Xil_In32(ARM5_BASE+0x78);
}
static void write_index(unsigned index,unsigned data){
 Xil_Out32(ARM5_BASE+0x74,index);Xil_Out32(ARM5_BASE+0x78,data);
}
unsigned arm5_issue(unsigned cmd,unsigned argc,const int *args){
 unsigned seq,i,r;XTime start,now;
 if(Xil_In32(ARM5_BASE+0x6c)!=ARM5_ID || argc>6)return 10;
 seq=arm5_read(7)>>16;
 for(i=0;i<argc;i++)write_index(i,(unsigned)args[i]&65535U);
 write_index(6,cmd|(argc<<8));Xil_Out32(ARM5_BASE+0x7c,0x41583521U);
 XTime_GetTime(&start);
 do {
  r=arm5_read(7);if((r>>16)!=seq)return r&15U;
  XTime_GetTime(&now);
 }while(now-start<COUNTS_PER_SECOND/100);
 return 15;
}
