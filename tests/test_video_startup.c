#include "../firmware/src/video_startup.c"
typedef struct {
    unsigned ms,last_frame,frames,mode,status[2],writes,frame_clears;
} Fake;
static void advance(Fake *f) {
    if(f->ms-f->last_frame>=40) {
        f->last_frame=f->ms;f->frames++;
        if(f->mode!=5) {
            f->status[0]|=IOC;
            if(f->mode!=6)f->status[1]|=IOC;
        }
        if(f->mode==1) {f->status[0]|=0x4810;f->status[1]|=0x4810;}
        if(f->mode==7 && f->frames==3)f->status[1]|=0x4810;
    }
}
static unsigned rd(void *p,unsigned address) {
    Fake *f=p;unsigned i=address>=VIDEO_STARTUP_BASE1?1U:0U;
    unsigned offset=address-bases[i];advance(f);
    if(offset==0x30)return f->mode==4?0U:0x10001U;
    if(offset==0x34)return f->status[i];
    if(offset==0xA0)return f->mode==8?999U:i?416U:720U;
    if(offset==0xA4 || offset==0xA8)return i?3328U:3840U;
    return 0;
}
static void wr(void *p,unsigned address,unsigned value) {
    Fake *f=p;unsigned i=address>=VIDEO_STARTUP_BASE1?1U:0U;
    /* Only W1C status writes, never CR, geometry, PWM or image buffers. */
    if(address!=bases[i]+0x34U || (value&~(FRAME_ERROR|INTERNAL_ERROR|IRQ_FLAGS))) {
        f->writes=0xFFFFFFFFU;return;
    }
    f->writes++;
    if(value&(FRAME_ERROR|INTERNAL_ERROR))f->frame_clears++;
    if(f->mode!=9)f->status[i]&=~value;
}
static unsigned ms(void *p) {Fake *f=p;f->ms+=10;return f->ms;}
static int run(Fake *f,VideoStartupReport *report) {
    VideoStartupIo io={f,rd,wr,ms};
    return video_startup_sync(&io,report);
}
int main(void) {
    Fake f={0};VideoStartupReport r;
    if(!run(&f,&r) || r.clean_frames[0]!=3 || r.clean_frames[1]!=3 || r.clears[0])return 1;
    f=(Fake){0};f.status[0]=f.status[1]=0x15810;
    if(!run(&f,&r) || r.clears[0]!=1 || r.clears[1]!=1 || f.frame_clears!=2 || f.frames<3)return 2;
    f=(Fake){0};f.mode=1;
    if(run(&f,&r) || r.reason!=5 || r.clears[0]!=3 || r.elapsed_ms>=5000)return 3;
    f=(Fake){0};f.status[1]=0x60;
    if(run(&f,&r) || r.reason!=3 || f.writes)return 4;
    f=(Fake){0};f.status[0]=0x10;
    if(run(&f,&r) || r.reason!=4 || f.writes)return 5;
    f=(Fake){0};f.mode=4;
    if(run(&f,&r) || r.reason!=2 || f.writes)return 6;
    f=(Fake){0};f.mode=5;
    if(run(&f,&r) || r.reason!=1 || r.elapsed_ms<5000)return 7;
    f=(Fake){0};f.mode=6;
    if(run(&f,&r) || r.reason!=1 || r.clean_frames[1])return 8;
    f=(Fake){0};f.mode=7;
    if(!run(&f,&r) || r.clears[1]!=1 || f.frames<6 || r.clean_frames[0]!=3 || r.clean_frames[1]!=3)return 9;
    f=(Fake){0};f.mode=8;
    if(run(&f,&r) || r.reason!=6 || f.writes)return 10;
    f=(Fake){0};f.mode=9;f.status[0]=f.status[1]=0x15810;
    if(run(&f,&r) || r.reason!=5 || r.clears[0]!=3)return 11;
    f=(Fake){0};f.ms=0xFFFFFFF0U;f.last_frame=f.ms;
    if(!run(&f,&r) || r.elapsed_ms>5000)return 12;
    f=(Fake){0};f.status[0]=1;
    if(run(&f,&r) || r.reason!=2 || f.writes)return 13;
    return 0;
}
