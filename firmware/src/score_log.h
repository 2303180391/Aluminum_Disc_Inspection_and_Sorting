#ifndef SCORE_LOG_H
#define SCORE_LOG_H
#include "xil_types.h"
void score_log_init(void);
u32 score_reference(int box[4],u32 *age_ms);
int score_hold_low(float score15,float score22);
void score_smooth_box(int *x1,int *y1,int *x2,int *y2);
/* reason 1=low scores, 2=invalid geometry, 3=box written */
void score_log(float score15,float score22,u32 reason,int x1,int y1,int x2,int y2);
#endif
