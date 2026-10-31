#ifndef VIDEO_STARTUP_H
#define VIDEO_STARTUP_H
/* Startup only. Both current VDMA IPs enable flush-on-fsync.
 * PG020 Hardware Debug: partial power-up frames may leave sticky size errors.
 * Require three new error-free counter events on BOTH channels after the last
 * permitted startup clear. No runtime errors are masked or acknowledged here. */
#define VIDEO_STARTUP_MAGIC 0x56535931U
#define VIDEO_STARTUP_BASE0 0x43000000U
#define VIDEO_STARTUP_BASE1 0x43010000U
typedef struct {
    void *context;
    unsigned (*read)(void *, unsigned);
    void (*write)(void *, unsigned, unsigned);
    unsigned (*milliseconds)(void *);
} VideoStartupIo;
typedef struct {
    unsigned magic,version,phase,result;
    unsigned initial_sr[2],final_sr[2],control[2];
    unsigned clean_frames[2],clears[2],elapsed_ms,reason;
} VideoStartupReport;
/* reason: 1 timeout,2 control/halt,3 bus/SG error,4 internal without frame
 * mismatch,5 recurring/unclearable mismatch,6 unexpected geometry. */
int video_startup_sync(const VideoStartupIo *,volatile VideoStartupReport *);
#endif
