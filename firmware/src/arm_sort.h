#ifndef ARM_SORT_H
#define ARM_SORT_H
#include "multiface.h"
#include <stdint.h>
void arm_sort_frame(uint64_t ms,unsigned frame,const MfBox *boxes,
                    unsigned count,unsigned any_candidate,unsigned valid);
#endif
