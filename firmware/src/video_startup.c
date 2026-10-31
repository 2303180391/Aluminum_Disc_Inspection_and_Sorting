#include "video_startup.h"
#define FRAME_ERROR 0x00008980U
#define INTERNAL_ERROR 0x00000010U
#define ALL_ERROR 0x00008FF0U
#define IOC 0x00001000U
#define IRQ_FLAGS 0x00007000U
#define CLEAN_EVENTS 3U
#define MAX_CLEARS 3U
#define TIMEOUT_MS 5000U
static const unsigned bases[2]={VIDEO_STARTUP_BASE0,VIDEO_STARTUP_BASE1};
static unsigned guard(unsigned control,unsigned status) {
    if((control&0x15U)!=1U || (status&1U))return 2;
    if(status&(ALL_ERROR&~(FRAME_ERROR|INTERNAL_ERROR)))return 3;
    if((status&INTERNAL_ERROR) && !(status&FRAME_ERROR))return 4;
    return 0;
}
static int finish(volatile VideoStartupReport *r,unsigned reason,unsigned elapsed) {
    r->elapsed_ms=elapsed;r->reason=reason;r->result=reason?2U:1U;
    r->phase=reason?255U:3U;
    return reason==0;
}
int video_startup_sync(const VideoStartupIo *io,volatile VideoStartupReport *r) {
    unsigned i,start=io->milliseconds(io->context),status[2],why,soft;
    for(i=0;i<sizeof(*r)/sizeof(unsigned);i++)((volatile unsigned *)r)[i]=0;
    r->magic=VIDEO_STARTUP_MAGIC;r->version=1;r->phase=1;
    /* Inspect both channels before performing any status acknowledgements. */
    for(i=0;i<2;i++) {
        unsigned expected_v=i?416U:720U,expected_h=i?3328U:3840U;
        r->control[i]=io->read(io->context,bases[i]+0x30U);
        status[i]=io->read(io->context,bases[i]+0x34U);
        r->initial_sr[i]=status[i];r->final_sr[i]=status[i];
        why=guard(r->control[i],status[i]);
        if(why)return finish(r,why,io->milliseconds(io->context)-start);
        if(io->read(io->context,bases[i]+0xA0U)!=expected_v ||
           io->read(io->context,bases[i]+0xA4U)!=expected_h ||
           (io->read(io->context,bases[i]+0xA8U)&0xFFFFFFU)!=expected_h)
            return finish(r,6,io->milliseconds(io->context)-start);
    }
    /* Discard old interrupt events; frame error causes remain for inspection. */
    for(i=0;i<2;i++)io->write(io->context,bases[i]+0x34U,IRQ_FLAGS);
    r->phase=2;
    for(;;) {
        soft=0;
        for(i=0;i<2;i++) {
            r->control[i]=io->read(io->context,bases[i]+0x30U);
            status[i]=io->read(io->context,bases[i]+0x34U);
            r->final_sr[i]=status[i];
            why=guard(r->control[i],status[i]);
            if(why)return finish(r,why,io->milliseconds(io->context)-start);
            if(status[i]&FRAME_ERROR)soft|=1U<<i;
        }
        if(soft) {
            /* A size error on either path invalidates BOTH clean windows. */
            r->clean_frames[0]=0;r->clean_frames[1]=0;
            for(i=0;i<2;i++) {
                if(soft&(1U<<i)) {
                    if(r->clears[i]>=MAX_CLEARS)
                        return finish(r,5,io->milliseconds(io->context)-start);
                    r->clears[i]++;
                    io->write(io->context,bases[i]+0x34U,
                              status[i]&(FRAME_ERROR|INTERNAL_ERROR|IRQ_FLAGS));
                } else io->write(io->context,bases[i]+0x34U,IOC);
            }
        } else {
            for(i=0;i<2;i++)if(status[i]&IOC) {
                if(r->clean_frames[i]<CLEAN_EVENTS)r->clean_frames[i]++;
                io->write(io->context,bases[i]+0x34U,IOC);
            }
            if(r->clean_frames[0]>=CLEAN_EVENTS && r->clean_frames[1]>=CLEAN_EVENTS)
                return finish(r,0,io->milliseconds(io->context)-start);
        }
        r->elapsed_ms=io->milliseconds(io->context)-start;
        if(r->elapsed_ms>=TIMEOUT_MS)return finish(r,1,r->elapsed_ms);
    }
}
