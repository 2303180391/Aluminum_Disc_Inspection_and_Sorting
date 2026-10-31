#include "disc_locator.h"
#include "disc_origin_reference.h"
#include <string.h>
#include "crc_fast.h"
#ifdef DISC_BOARD_HOST_TEST
#include "../disc_board_mock.h"
#else
#include "xil_io.h"
#include "xil_cache.h"
#include "xtime_l.h"
#define barrier() __asm__ volatile("dsb sy" ::: "memory")
#define DISC_FRAME_PTR(a) ((uint8_t *)(a))
#endif
/* E80000..E800FF lies in diag_init's uncached MiB, outside all existing rings.
 * Host owns word4 request, 40..42 dimensions/config. Firmware never writes
 * ARM control/PWM registers. A request never triggers a sorting decision.
 */
#define MAIL 0x00E80000U
#define VDMA 0x43000000U
#define RAW 0x04000000U
#define SINK 0x08000000U
#define SNAP 0x08400000U
#define BYTES 2764800U
static unsigned input_crc_table[4][256];
static unsigned arm_measure_authorized;
volatile unsigned disc_request=0,disc_status=0,disc_sequence=0;
volatile DiscResult disc_result;
static unsigned rd(unsigned n) {return Xil_In32(MAIL+n*4);}
static void wr(unsigned n,unsigned v) {Xil_Out32(MAIL+n*4,v);}
static unsigned elapsed(XTime start)
{XTime now;XTime_GetTime(&now);return (unsigned)((now-start)*1000000ULL/COUNTS_PER_SECOND);}
void disc_board_init(void)
{
    unsigned i;
    for(i=0;i<64;i++) wr(i,0);
    crc_fast_init(input_crc_table);
    disc_locator_reset_calibration();
    wr(1,3);wr(2,0x20261023);wr(3,64);
    wr(40,disc_default_config.width_um);wr(41,disc_default_config.height_um);
    wr(42,disc_default_config.dimensions_confirmed);
    wr(51,DISC_FIXED_ORIGIN?1U:0U);wr(52,DISC_ORIGIN_REFERENCE_TAG);
    wr(55,0x4F434331U); /* OCC1: conservative partial-occlusion policy */
    wr(56,0x52444631U); /* RDF1: direct red-frame geometry, green-table ROI */
    wr(57,DISC_EDGE_ROI_POLICY); /* EDG2: current visible-edge refit */
    barrier();wr(0,0x444C4332); /* DLC2 */
}
static int stream_ok(void)
{
    return (Xil_In32(VDMA+0x30)&0x00ff003f)==0x00010001 &&
           !(Xil_In32(VDMA+0x34)&0x8ff1);
}
static int fence(void)
{
    XTime start,now; unsigned count=0;
    XTime_GetTime(&start);Xil_Out32(VDMA+0x34,0x1000);
    while(count<3) {
        unsigned sr=Xil_In32(VDMA+0x34);
        if(!stream_ok()) return 0;
        if(sr&0x1000) {Xil_Out32(VDMA+0x34,0x1000);count++;}
        XTime_GetTime(&now);
        if(now-start>COUNTS_PER_SECOND) return 0;
    }
    XTime_GetTime(&start);
    do {XTime_GetTime(&now);} while(now-start<COUNTS_PER_SECOND/50);
    return stream_ok();
}
static void destination(unsigned address)
{
    Xil_Out32(VDMA+0xac,address);Xil_Out32(VDMA+0xa0,720);barrier();
}
void disc_board_poll(void)
{
    DiscResult result={0};DiscConfig config;unsigned token=rd(4),state=2,i;
    unsigned words[sizeof(DiscResult)/4];XTime start,step;
    unsigned save_input=rd(43),before_crc=0,snapshot_crc=0,after_crc=0;
    int ok=0;
    if(!token) return;
    XTime_GetTime(&start);
    disc_request=token;wr(4,0);wr(5,1);wr(7,token);
    wr(43,0);for(i=44;i<=50;i++) wr(i,0);
    for(i=8;i<12;i++) wr(i,0);
    for(i=16;i<16+sizeof(DiscResult)/4;i++) wr(i,0);
    config.width_um=rd(40);config.height_um=rd(41);config.dimensions_confirmed=rd(42)==1;
    if((arm_measure_authorized ? ((Xil_In32(0x43c10068)&0x13)!=3) :
        ((Xil_In32(0x43c10068)&0x17)!=0)) || Xil_In32(0x43c1006c)!=0x41524d35 ||
       Xil_In32(0x43c0000c)!=0x43535131 || !stream_ok() ||
       Xil_In32(VDMA+0xac)!=RAW || Xil_In32(VDMA+0xa0)!=720 ||
       Xil_In32(VDMA+0xa4)!=3840 || Xil_In32(VDMA+0xa8)!=3840 ||
       config.width_um<1000 || config.height_um<1000 ||
       config.width_um>1000000 || config.height_um>1000000) {
        state=3;
    } else {
        destination(SINK);XTime_GetTime(&step);
        ok=fence() && Xil_In32(VDMA+0xac)==SINK;
        wr(8,elapsed(step));
        if(ok) {
            Xil_DCacheInvalidateRange(RAW,BYTES);
            if(save_input==1) {
                XTime_GetTime(&step);
                before_crc=crc_fast(DISC_FRAME_PTR(RAW),BYTES,input_crc_table);
                memcpy(DISC_FRAME_PTR(SNAP),DISC_FRAME_PTR(RAW),BYTES);
                snapshot_crc=crc_fast(DISC_FRAME_PTR(SNAP),BYTES,input_crc_table);
                Xil_DCacheFlushRange(SNAP,BYTES);barrier();
                wr(48,elapsed(step));
            }
            XTime_GetTime(&step);
            disc_locate_config(DISC_FRAME_PTR(RAW),&config,&result);
            wr(9,elapsed(step));
            ok=stream_ok() && Xil_In32(VDMA+0xac)==SINK;
            if(save_input==1) {
                Xil_DCacheInvalidateRange(RAW,BYTES);
                after_crc=crc_fast(DISC_FRAME_PTR(RAW),BYTES,input_crc_table);
                ok=ok && before_crc==snapshot_crc && after_crc==snapshot_crc;
                wr(44,SNAP);wr(45,BYTES);wr(46,snapshot_crc);
                wr(49,before_crc);wr(50,after_crc);
            }
        }
        destination(RAW); /* Restore even after a capture/algorithm failure. */
        if(!fence() || Xil_In32(VDMA+0xac)!=RAW) ok=0;
        if(!ok) {memset(&result,0,sizeof(result));state=4;}
    }
    if(state!=2)disc_locator_reset_calibration();
    wr(53,disc_locator_calibrated());wr(54,disc_locator_used_cache);
    wr(58,disc_locator_pose_refit_used);
    wr(59,disc_locator_outline_recovered);
    if(state==2 && save_input==1) wr(47,token);
    wr(10,elapsed(start));wr(11,Xil_In32(0x00E00014));
    wr(12,config.width_um);wr(13,config.height_um);wr(14,config.dimensions_confirmed);
    disc_result=result;memcpy(words,&result,sizeof(words));
    for(i=0;i<sizeof(words)/sizeof(words[0]);i++) wr(16+i,words[i]);
    disc_sequence++;wr(6,disc_sequence);disc_status=state;disc_request=0;
    wr(63,token);barrier();wr(5,state); /* publish state last */
}
unsigned disc_board_measure_for_arm(unsigned token)
{
    if(!token || rd(4))return 0;
    wr(4,token);arm_measure_authorized=1;disc_board_poll();arm_measure_authorized=0;
    return rd(63)==token && rd(5)==2 && disc_result.valid && disc_result.mm_valid &&
           disc_result.frame_valid && disc_result.origin_valid && disc_result.status==LOC_OK;
}
