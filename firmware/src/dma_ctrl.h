#ifndef DMA_CTRL_H
#define	DMA_CTRL_H

#include "xaxidma.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_types.h"

void DMA_Init();
void DMA_PrepareImmutableParameters(void);
void DMA_PerfReset(void);
void DMA_PerfRead(u32 *skipped_calls,u32 *skipped_bytes,u32 *flush_calls,u32 *flush_bytes);
void DMA_BeginLayer(u32 layer,u32 source,u32 channels,u32 side);
void DMA_EndLayer(void);
void DMA_Tx(u32 TxAddr, u32 Length);
void DMA_Rx(u32 RxAddr, u32 Length);



#endif
