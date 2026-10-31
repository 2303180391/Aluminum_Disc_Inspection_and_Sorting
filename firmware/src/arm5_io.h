#ifndef ARM5_IO_H
#define ARM5_IO_H
#include <stdint.h>
#define ARM5_BASE 0x43C10000U
#define ARM5_ID 0x41524D35U
void arm5_init(void);
void arm5_poll(void);
unsigned arm5_read(unsigned index);
unsigned arm5_issue(unsigned cmd,unsigned argc,const int *args);
unsigned disc_board_measure_for_arm(unsigned token);
#endif
