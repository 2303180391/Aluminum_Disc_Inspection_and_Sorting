#ifndef PERF_PROFILE_H
#define PERF_PROFILE_H
#ifdef PROFILE_HOST_TEST
#include "../tests/profile_mock.h"
#else
#include "xil_types.h"
#include "xtime_l.h"
#endif
#define PROFILE_BASE 0x00EB0000U
#define PROFILE_MAGIC 0x50465231U
#define PROFILE_HEADER_WORDS 16U
#define PROFILE_RING 32U
#define PROFILE_WORDS 544U
#define PROFILE_LAYER_WORDS 32U
#ifndef PROFILE_TASK_DETAIL
#define PROFILE_TASK_DETAIL 0
#endif
/* New mailbox occupies EB0000..EC103F, outside every existing diagnostic ring.
 * Host owns words 6 (mode: 0 live/1 replay) and 7 (expected input CRC).
 * No motion command is issued by this module. */
void profile_init(void);
void profile_frame_begin(void);
void profile_frame_end(u32 capture_us,u32 inference_us,u32 head_cache_us,u32 body_us);
void profile_layer_begin(u32 layer);
void profile_layer_end(u32 layer,u32 crc,u32 audited);
#if PROFILE_TASK_DETAIL
void profile_task_begin(u32 command);
void profile_task_end(void);
void profile_dma(u32 direction,u32 length);
void profile_cache(u32 direction,XTime elapsed);
void profile_wait(u32 dma_tail,XTime elapsed);
#else
/* Deliberately discard arguments too: no hidden timer read in the hot path. */
#define profile_task_begin(command) ((void)0)
#define profile_task_end() ((void)0)
#define profile_dma(direction,length) ((void)0)
#define profile_cache(direction,elapsed) ((void)0)
#define profile_wait(dma_tail,elapsed) ((void)0)
#endif
int profile_replay(void);
u32 profile_expected_crc(void);
#endif
