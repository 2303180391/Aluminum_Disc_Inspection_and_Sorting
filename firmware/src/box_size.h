#ifndef BOX_SIZE_H
#define BOX_SIZE_H
#include "tracks.h"
typedef struct {unsigned id,hits;int w,h,pw,ph,tw,th,cx,cy;MtTime strong;} BoxSizeSlot;
typedef struct {BoxSizeSlot slot[4];} BoxSizeState;
/* Display copy only: raw tracks and their association geometry are untouched. */
void box_size_apply(BoxSizeState *state,const MtState *raw,MtState *display);
#endif
