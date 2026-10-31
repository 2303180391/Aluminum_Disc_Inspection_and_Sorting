#ifndef ARM_PROTOCOL_H
#define ARM_PROTOCOL_H
#include <stdint.h>
#define ARM_PROTOCOL_LINE 96U
typedef struct {
 uint32_t session,last_seq,leases[3];
 unsigned lease_pos,used,dropping;
 char input[ARM_PROTOCOL_LINE],last_verb[12],last_result[28];
 void (*emit)(const char *line);
 const char *(*execute)(unsigned command);
} ArmProtocol;
uint16_t arm_crc16(const char *text);
void arm_protocol_init(ArmProtocol *p,void (*emit)(const char *),
                       const char *(*execute)(unsigned));
void arm_protocol_byte(ArmProtocol *p,unsigned char byte,uint32_t now);
void arm_protocol_lease(ArmProtocol *p,uint32_t now);
void arm_protocol_drop(ArmProtocol *p);
void arm_protocol_send(ArmProtocol *p,const char *body);
/* Bounded ASCII builder shared with the platform adapter; all integers unsigned. */
void arm_text(char *out,unsigned capacity,const char *text);
void arm_hex(char *out,unsigned capacity,uint32_t value,unsigned digits);
void arm_decimal(char *out,unsigned capacity,uint32_t value);
#endif
