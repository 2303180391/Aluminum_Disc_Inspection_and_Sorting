#define ARM_UART_HOST_TEST
#include <string.h>
#include "../firmware/src/arm_protocol.c"
#include "../firmware/src/arm_uart.c"
static UartMockDiag mock_diag={0x59444731U,100,0,4};
volatile UartMockDiag *const yolo_diag=&mock_diag;
static ArmRemoteSnapshot snapshot;
static char fifo[256],sent[64][320];
static unsigned fifo_at,fifo_end,sent_count,executed,last_command,oversized;
static uint32_t mock_time=1000,rx_error;
static const char *mock_result="OK";
void XTime_GetTime(XTime *out){*out=mock_time;}
unsigned XUartPs_IsReceiveData(u32 base){(void)base;return fifo_at<fifo_end;}
u32 XUartPs_ReadReg(u32 base,u32 offset){
 (void)base;if(offset==XUARTPS_ISR_OFFSET)return rx_error;
 return offset==XUARTPS_FIFO_OFFSET?(unsigned char)fifo[fifo_at++]:0;
}
void XUartPs_WriteReg(u32 base,u32 offset,u32 value){
 (void)base;(void)value;if(offset==XUARTPS_ISR_OFFSET)rx_error=0;
}
void Xil_Out32(u32 address,u32 value){(void)address;(void)value;}
void arm_uart_test_emit(const char *line){
 unsigned n=(unsigned)strlen(line);
 if(n>=320 || sent_count>=64){oversized=1;return;}
 strcpy(sent[sent_count++],line);
}
void arm_remote_snapshot(ArmRemoteSnapshot *s){*s=snapshot;}
const char *arm_remote_command(unsigned c){executed++;last_command=c;return mock_result;}
static void request(uint32_t session,uint32_t seq,uint32_t tick,const char *verb){
 char body[90]={0};
 arm_text(body,sizeof body,"ARM1,");arm_hex(body,sizeof body,session,8);
 arm_text(body,sizeof body,",");arm_hex(body,sizeof body,seq,8);
 arm_text(body,sizeof body,",");arm_hex(body,sizeof body,tick,8);
 arm_text(body,sizeof body,",");arm_text(body,sizeof body,verb);
 fifo[0]=0;arm_text(fifo,sizeof fifo,"@");arm_text(fifo,sizeof fifo,body);
 arm_text(fifo,sizeof fifo,",");arm_hex(fifo,sizeof fifo,arm_crc16(body),4);arm_text(fifo,sizeof fifo,"\n");
 fifo_at=0;fifo_end=(unsigned)strlen(fifo);
}
static unsigned contains(const char *text){
 unsigned i;for(i=0;i<sent_count;i++)if(strstr(sent[i],text))return 1;return 0;
}
static unsigned checked_lines(void){
 unsigned i;
 for(i=0;i<sent_count;i++){
  char body[320],expected[5]={0};char *cut;
  strcpy(body,sent[i]);cut=strstr(body," crc=");if(!cut || strlen(cut)!=9)return 0;
  *cut=0;arm_hex(expected,sizeof expected,arm_crc16(body),4);
  if(strcmp(cut+5,expected))return 0;
 }
 return !oversized;
}
#define CHECK(c,n) do{if(!(c))return n;}while(0)
int main(void){
 unsigned before;
 snapshot.hw=9;snapshot.flags=3;snapshot.calibrated=1;snapshot.pose=0x4132965A;snapshot.claw=37;
 arm_uart_init();CHECK(contains("capability=ARM5_LOC1 origin=FIXED_REF") && !executed,1);
 request(1,1,0,"HELLO");arm_uart_poll();
 CHECK(contains("ARMCTL HELLO session=00000001") && !executed,2);
 CHECK(contains("ARMCTL STATE session=00000001 seq=00000000 tick=000003E8") && contains("arm5=1"),3);
 CHECK(contains("ARMCTL LOCSTATE session=00000001 seq=00000000 tick=000003E8") && checked_lines(),4);
 mock_time=1100;request(1,2,1000,"TEACH");arm_uart_poll();CHECK(executed==1 && last_command==REMOTE_TEACH,5);
 mock_time=1200;request(1,2,1000,"TEACH");arm_uart_poll();
 CHECK(executed==1 && contains("cmd=TEACH result=OK duplicate=1"),6);
 mock_time=1300;request(1,3,1200,"LOCATE");arm_uart_poll();CHECK(executed==2 && last_command==REMOTE_LOCATE,7);
 snapshot.phase=4;mock_time=1301;before=sent_count;arm_uart_poll();
 CHECK(sent_count==before+2 && contains("phase=4"),8);
 snapshot.phase=0;snapshot.x_mm=50;snapshot.y_mm=54;snapshot.xy_valid=1;snapshot.locate_only=1;snapshot.measure=2;
 mock_time=1302;arm_uart_poll();CHECK(contains("x=50 y=54 valid=1 measure=2 mode=1"),9);
 mock_diag.error=7;mock_time=1400;request(1,4,1302,"START");arm_uart_poll();
 CHECK(executed==2 && contains("cmd=START result=BAD_DIAG"),10);mock_diag.error=0;
 mock_time=1500;request(2,5,1400,"START");arm_uart_poll();CHECK(executed==2 && contains("result=NO_SESSION"),11);
 mock_time=1600;request(1,1,1500,"START");arm_uart_poll();CHECK(executed==2 && contains("result=OLD_SEQ"),12);
 mock_time=5000;request(1,5,1302,"LOCATE");arm_uart_poll();CHECK(executed==2 && contains("result=STALE_LEASE"),13);
 mock_time=5100;request(1,6,5000,"LOCATE");rx_error=XUARTPS_IXR_PARITY;arm_uart_poll();CHECK(executed==2 && !rx_error,14);
 /* Drop lasts until newline, not until the next apparently valid fragment. */
 mock_time=5200;request(1,6,5000,"LOCATE");arm_uart_poll();CHECK(executed==2,15);
 mock_time=5300;mock_result="BUSY";request(1,6,5200,"LOCATE");arm_uart_poll();
 CHECK(executed==3 && contains("cmd=LOCATE result=BUSY"),16);
 mock_time=5400;request(1,6,5200,"START");arm_uart_poll();
 CHECK(executed==3 && contains("result=SEQ_MISMATCH"),17);
 request(1,7,5300,"TEACH");fifo[34]='X';mock_time=5500;arm_uart_poll();CHECK(executed==3,18);
 request(2,1,0,"HELLO");mock_time=5600;arm_uart_poll();CHECK(executed==3 && protocol.session==2,19);
 request(2,2,5300,"START");mock_time=5700;arm_uart_poll();CHECK(executed==3,20);
 /* Bound every outbound status line, including maximum decimal counters. */
 sent_count=0;mock_diag.frame=0xFFFFFFFFU;snapshot.measure=0xFFFFFFFFU;snapshot.samples=1000000;
 snapshot.votes[0]=5;snapshot.window=5;mock_time=6800;arm_uart_poll();
 CHECK(sent_count==2 && checked_lines(),21);
 CHECK(strstr(sent[0],"4294967295") && strstr(sent[1],"measure=4294967295"),22);
 return 0;
}
