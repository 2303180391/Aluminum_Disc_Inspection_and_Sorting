#include "arm_remote.h"
#include "arm_protocol.h"
#include "disc_origin_reference.h"
#ifdef ARM_UART_HOST_TEST
#include "uart_adapter_mock.h"
#else
#include "xuartps_hw.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "xtime_l.h"
#include "yolo_diag.h"
#endif
#define UART_BASE XPAR_XUARTPS_0_BASEADDR
static ArmProtocol protocol;
static uint32_t last_status;
static unsigned last_state=0xFFFFFFFFU,last_hw=0xFFFFFFFFU;
static unsigned last_phase=0xFFFFFFFFU,last_flags=0xFFFFFFFFU,last_measure,last_valid;
static uint32_t milliseconds(void){XTime t;XTime_GetTime(&t);return (uint32_t)(t/(COUNTS_PER_SECOND/1000U));}
static void emit(const char *line){
#ifdef ARM_UART_HOST_TEST
 arm_uart_test_emit(line);
#else
 xil_printf("%s\r\n",line);
#endif
}
static const char *execute(unsigned command){
 if(yolo_diag->magic!=0x59444731U || !yolo_diag->frame || yolo_diag->error || yolo_diag->stage==D_FAILED)return "BAD_DIAG";
 return arm_remote_command(command);
}
static void value(char *out,const char *key,uint32_t n){arm_text(out,300,key);arm_decimal(out,300,n);}
static void status(uint32_t now){
 ArmRemoteSnapshot s;char out[300]={0};unsigned i;
 arm_remote_snapshot(&s);
 arm_text(out,sizeof out,"ARMCTL STATE session=");arm_hex(out,sizeof out,protocol.session,8);
 arm_text(out,sizeof out," seq=00000000 tick=");arm_hex(out,sizeof out,now,8);
 value(out," state=",s.state);value(out," reason=",s.reason);
 arm_text(out,sizeof out," hw=");arm_hex(out,sizeof out,s.hw,8);
 value(out," frame=",yolo_diag->frame);value(out," result=",s.result);
 value(out," samples=",s.samples);value(out," window=",s.window);
 arm_text(out,sizeof out," votes=");
 for(i=0;i<4;i++){if(i)arm_text(out,sizeof out,"/");arm_decimal(out,sizeof out,s.votes[i]);}
 value(out," uncertain=",s.uncertain);value(out," diag=",yolo_diag->error);
 value(out," arm5=",1);
 arm_protocol_lease(&protocol,now);arm_protocol_send(&protocol,out);
 /* Bind the detailed workflow snapshot to the exact STATE session and tick. */
 out[0]=0;arm_text(out,sizeof out,"ARMCTL LOCSTATE session=");
 arm_hex(out,sizeof out,protocol.session,8);
 arm_text(out,sizeof out," seq=00000000 tick=");arm_hex(out,sizeof out,now,8);
 value(out," phase=",s.phase);value(out," flags=",s.flags);
 value(out," x=",s.x_mm);value(out," y=",s.y_mm);value(out," valid=",s.xy_valid);
 value(out," measure=",s.measure);value(out," mode=",s.locate_only);
 value(out," cal=",s.calibrated);
 arm_text(out,sizeof out," pose=");arm_hex(out,sizeof out,s.pose,8);
 value(out," claw=",s.claw);arm_protocol_send(&protocol,out);
 last_status=now;last_state=s.state;last_hw=s.hw;
 last_phase=s.phase;last_flags=s.flags;last_measure=s.measure;last_valid=s.xy_valid;
}
void arm_uart_init(void){
 unsigned n=64U;u32 cr=XUartPs_ReadReg(UART_BASE,XUARTPS_CR_OFFSET);
 /* Keep baud, TX, pin configuration and interrupts unchanged; enable RX only. */
 XUartPs_WriteReg(UART_BASE,XUARTPS_CR_OFFSET,(cr & ~XUARTPS_CR_RX_DIS)|XUARTPS_CR_RX_EN);
 while(n>0U && XUartPs_IsReceiveData(UART_BASE)){n--; (void)XUartPs_ReadReg(UART_BASE,XUARTPS_FIFO_OFFSET);}
 XUartPs_WriteReg(UART_BASE,XUARTPS_ISR_OFFSET,XUARTPS_IXR_OVER|XUARTPS_IXR_PARITY|XUARTPS_IXR_FRAMING);
 arm_protocol_init(&protocol,emit,execute);
 Xil_Out32(0x00E00700U,0x55415231U);
 Xil_Out32(0x00E00704U,1U);
 Xil_Out32(0x00E00708U,0x20261027U);
#if DISC_FIXED_ORIGIN
 arm_protocol_send(&protocol,"ARMCTL READY proto=1 firmware=20261029 capability=ARM5_LOC1 origin=FIXED_REF tape=RED edge=EDG2 reference=F5BDF86D pwm_default=0");
#else
 arm_protocol_send(&protocol,"ARMCTL READY proto=1 firmware=20261027 capability=ARM5_LOC1 origin=WHITE_MARKER pwm_default=0");
#endif
}
void arm_uart_poll(void){
 uint32_t now=milliseconds(),old_session=protocol.session;unsigned n=64U,had_data=0;
 ArmRemoteSnapshot s;
 u32 errors=XUartPs_ReadReg(UART_BASE,XUARTPS_ISR_OFFSET)&(XUARTPS_IXR_OVER|XUARTPS_IXR_PARITY|XUARTPS_IXR_FRAMING);
 if(errors){
  arm_protocol_drop(&protocol);
  /* Hardware RX errors invalidate the whole buffered command, never salvage it. */
  while(n>0U && XUartPs_IsReceiveData(UART_BASE)){n--; (void)XUartPs_ReadReg(UART_BASE,XUARTPS_FIFO_OFFSET);}
  XUartPs_WriteReg(UART_BASE,XUARTPS_ISR_OFFSET,errors);n=0;
 }
 while(n>0U && XUartPs_IsReceiveData(UART_BASE)){
  n--;
  arm_protocol_byte(&protocol,(unsigned char)XUartPs_ReadReg(UART_BASE,XUARTPS_FIFO_OFFSET),now);had_data=1;
 }
 arm_remote_snapshot(&s);
 if(had_data || protocol.session!=old_session || now-last_status>=1000U || s.state!=last_state || s.hw!=last_hw ||
    s.phase!=last_phase || s.flags!=last_flags || s.measure!=last_measure || s.xy_valid!=last_valid)status(now);
}
