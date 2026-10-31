#ifndef ARM5_DISC_BOARD_MOCK_H
#define ARM5_DISC_BOARD_MOCK_H
#include <stdint.h>
typedef uint64_t XTime;
#define COUNTS_PER_SECOND 1000000U
unsigned Xil_In32(unsigned address);
void Xil_Out32(unsigned address,unsigned value);
void XTime_GetTime(XTime *time);
void Xil_DCacheInvalidateRange(unsigned address,unsigned bytes);
void Xil_DCacheFlushRange(unsigned address,unsigned bytes);
uint8_t *disc_mock_frame(unsigned address);
#define DISC_FRAME_PTR(a) disc_mock_frame(a)
#define barrier() ((void)0)
#endif
