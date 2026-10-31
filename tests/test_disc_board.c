#define DISC_BOARD_HOST_TEST
static unsigned failures;
#define assert(x) do{if(!(x)) failures++;}while(0)

#include <string.h>
#include "../firmware/src/disc_locator.h"
#include "../firmware/src/disc_origin_reference.h"
#include "../runtime/disc_board_mock.h"
unsigned disc_board_measure_for_arm(unsigned token);
unsigned disc_locator_used_cache;
void disc_locator_reset_calibration(void){disc_locator_used_cache=0;}
unsigned disc_locator_calibrated(void){return 0;}
static unsigned mail[64],mock_destination=0x04000000,arm,fail_sink,fail_restore,wrong_hw,calls,writes,mutate;
static XTime ticks;
static uint8_t raw[2764800],snapshot[2764800];
uint8_t *disc_mock_frame(unsigned a) {assert(a==0x04000000 || a==0x08400000);return a==0x04000000?raw:snapshot;}
const DiscConfig disc_default_config={100000,100000,0};
unsigned Xil_In32(unsigned a)
{
    if(a>=0x00e80000 && a<0x00e80100) return mail[(a-0x00e80000)/4];
    if(a==0x43c10068) return arm;
    if(a==0x43c1006c) return 0x41524d35;
    if(a==0x43c0000c) return wrong_hw?0:0x43535131;
    if(a==0x43000030) return 0x00010001;
    if(a==0x43000034) {
        if(fail_sink && mock_destination==0x08000000) return 0x20;
        if(fail_restore && mock_destination==0x04000000 && writes) return 0;
        return 0x1000;
    }
    if(a==0x430000ac) return mock_destination;
    if(a==0x430000a0) return 720;
    if(a==0x430000a4 || a==0x430000a8) return 3840;
    if(a==0x00e00014) return 123;
    assert(!"unexpected MMIO read");return 0;
}
void Xil_Out32(unsigned a,unsigned v)
{
    if(a>=0x00e80000 && a<0x00e80100) {mail[(a-0x00e80000)/4]=v;return;}
    assert(a==0x430000ac || a==0x430000a0 || a==0x43000034);
    if(a==0x430000ac) {assert(v==0x04000000 || v==0x08000000);mock_destination=v;writes++;}
}
void XTime_GetTime(XTime *t) {*t=(ticks+=10000);}
void Xil_DCacheInvalidateRange(unsigned a,unsigned n) {assert(a==0x04000000 && n==2764800);}
void Xil_DCacheFlushRange(unsigned a,unsigned n) {assert(a==0x08400000 && n==2764800);}
void disc_locate_config(const uint8_t *p,const DiscConfig *c,DiscResult *r)
{assert(p==raw);assert(c->width_um==100000);memset(r,0,sizeof(*r));r->valid=1;r->mm_valid=1;r->frame_valid=1;r->origin_valid=1;r->status=LOC_OK;r->x_um=30000;calls++;if(mutate) raw[0]^=1;}
static void reset(void)
{mock_destination=0x04000000;arm=fail_sink=fail_restore=wrong_hw=calls=writes=mutate=0;ticks=0;disc_board_init();mail[4]=77;}
int main(void)
{
    reset();assert(mail[51]==1 && mail[52]==DISC_ORIGIN_REFERENCE_TAG && mail[55]==0x4F434331U);
    disc_board_poll();assert(mail[5]==2 && mail[63]==77 && mail[16]==1 && calls==1 && writes==2 && mock_destination==0x04000000);
    reset();arm=1;disc_board_poll();assert(mail[5]==3 && !calls && !writes && !mail[16]);
    reset();wrong_hw=1;disc_board_poll();assert(mail[5]==3 && !calls && !writes);
    reset();fail_sink=1;disc_board_poll();assert(mail[5]==4 && !calls && mock_destination==0x04000000 && writes==2 && !mail[16]);
    reset();fail_restore=1;disc_board_poll();assert(mail[5]==4 && calls==1 && mock_destination==0x04000000 && !mail[16]);
    reset();mail[4]=0;disc_board_poll();assert(!calls && !writes && mail[5]==0);
    reset();mail[4]=0;arm=3;
    assert(disc_board_measure_for_arm(88)==1 && calls==1 && mock_destination==0x04000000);
    reset();mail[4]=0;arm=19;
    assert(disc_board_measure_for_arm(89)==0 && calls==0 && writes==0);
    reset();arm=3;
    assert(disc_board_measure_for_arm(90)==0 && calls==0 && writes==0);
    return failures?1:0;
}
#include "../firmware/src/disc_board.c"
