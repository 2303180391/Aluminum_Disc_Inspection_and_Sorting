#ifndef EVIDENCE_H
#define EVIDENCE_H
#include "multiface.h"
void evidence_init(void);
void evidence_raw(const MfBox *boxes,unsigned count);
void evidence_record(const MfBox *boxes,unsigned count,unsigned frame,unsigned phase,unsigned track_seq);
#endif
