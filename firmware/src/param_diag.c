#include "yolo_diag.h"
#include "yolo_accel_ctrl.h"

/* All snapshots fit in the existing uncached diagnostic section.
 * Layer0 output ends at EAA000. Parameters begin at EAC000. */
#define PARAM_RESULT (DIAG_BASE+0x300U)
#define PARAM_COPY (DIAG_BASE+0xAC000U)
static const u32 param_addr[3]={0x02000000U,0x02004000U,0x02005000U};
static const u32 param_len[3]={64U,256U,1152U};
static const u32 param_offset[3]={0U,64U,320U};
static u32 crc_bytes(const volatile u8 *p,u32 n) {
    u32 c=0xFFFFFFFFU,i,j;
    for(i=0;i<n;i++) {
        c^=p[i];
        for(j=0;j<8;j++) c=(c>>1)^(0xEDB88320U & (0U-(c&1U)));
    }
    return ~c;
}
void diag_params_init(void) {
    unsigned i;
    for(i=0;i<32;i++) ((volatile u32 *)PARAM_RESULT)[i]=0;
}
void diag_params_check(u32 phase) {
    volatile u32 *r=(volatile u32 *)PARAM_RESULT;
    unsigned k,i;
    u32 mismatch=0;
    for(k=0;k<3;k++) {
        volatile u8 *src=(volatile u8 *)param_addr[k];
        volatile u8 *ref=(volatile u8 *)(PARAM_COPY+param_offset[k]);
        u32 count=0,first=0xFFFFFFFFU,crc;
        /* Preserve dirty SD-loaded bytes before reading backing memory.
         * These buffers are CPU-owned DMA TX sources, never RX targets. */
        Xil_DCacheFlushRange(param_addr[k],param_len[k]);
        Xil_DCacheInvalidateRange(param_addr[k],param_len[k]);
        if(yolo_diag->frame==1U && phase==1U)
            for(i=0;i<param_len[k];i++) ref[i]=src[i];
        crc=crc_bytes(src,param_len[k]);
        if(yolo_diag->frame==1U && phase==1U) r[k]=crc;
        r[(phase==1U?3U:6U)+k]=crc;
        for(i=0;i<param_len[k];i++) if(src[i]!=ref[i]) {
            if(count==0U) first=i;
            count++;
        }
        r[(phase==1U?9U:12U)+k]=count;
        r[17U+k]=first;
        mismatch|=count;
    }
    r[15]=phase; r[16]=yolo_diag->frame;
    if(mismatch) diag_fail(E_PARAM_CHANGED);
}
