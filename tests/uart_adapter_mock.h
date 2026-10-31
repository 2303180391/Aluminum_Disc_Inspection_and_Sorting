#ifndef UART_ADAPTER_MOCK_H
#define UART_ADAPTER_MOCK_H
#include <stdint.h>
#include "xtime_l.h"
typedef uint32_t u32;
typedef struct {u32 magic,frame,error,stage;} UartMockDiag;
extern volatile UartMockDiag *const yolo_diag;
#define D_FAILED 255U
#define XPAR_XUARTPS_0_BASEADDR 0xE0001000U
#define XUARTPS_CR_OFFSET 0U
#define XUARTPS_ISR_OFFSET 0x14U
#define XUARTPS_FIFO_OFFSET 0x30U
#define XUARTPS_CR_RX_DIS 8U
#define XUARTPS_CR_RX_EN 4U
#define XUARTPS_IXR_OVER 1U
#define XUARTPS_IXR_PARITY 2U
#define XUARTPS_IXR_FRAMING 4U
u32 XUartPs_ReadReg(u32 base,u32 offset);
void XUartPs_WriteReg(u32 base,u32 offset,u32 value);
unsigned XUartPs_IsReceiveData(u32 base);
void Xil_Out32(u32 address,u32 value);
void arm_uart_test_emit(const char *line);
#endif
