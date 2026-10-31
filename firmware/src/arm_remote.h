#ifndef ARM_REMOTE_H
#define ARM_REMOTE_H
#include <stdint.h>
enum { REMOTE_ENABLE=1, REMOTE_START, REMOTE_DISABLE, REMOTE_RETRY,
       REMOTE_TEACH, REMOTE_LOCATE };
typedef struct {
 unsigned state,reason,result,samples,frame,window,votes[4],uncertain,hw;
 unsigned phase,flags,x_mm,y_mm,xy_valid,measure,locate_only,calibrated,pose,claw;
} ArmRemoteSnapshot;
void arm_remote_snapshot(ArmRemoteSnapshot *out);
const char *arm_remote_command(unsigned command);
void arm_uart_init(void);
void arm_uart_poll(void);
#endif
