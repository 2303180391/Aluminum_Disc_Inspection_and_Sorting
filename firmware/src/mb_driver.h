#ifndef MB_DRIVER_H
#define MB_DRIVER_H
#include "tracks.h"
typedef struct {
 unsigned (*read)(unsigned offset);
 void (*write)(unsigned offset,unsigned value);
 void (*barrier)(void);
 unsigned (*ms)(void);
} MbIo;
/* Coordinates and class IDs are committed atomically by the MB4-v2 overlay. */
/* 0=success, 1=wrong signature, 2=busy timeout, 3=apply timeout, 4=mask mismatch. */
int mb_submit(const MbIo *io,const MtState *tracks);
void mb_display_init(void);
void mb_display(const MtState *tracks);
#endif
