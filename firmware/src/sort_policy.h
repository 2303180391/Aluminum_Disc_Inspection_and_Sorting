#ifndef SORT_POLICY_H
#define SORT_POLICY_H
#include <stdint.h>
enum { SORT_IDLE, SORT_SETTLE, SORT_OBSERVE, SORT_COMMAND, SORT_RUNNING, SORT_ERROR };
enum { SORT_REASON_NONE, SORT_REASON_INVALID, SORT_REASON_CONFLICT_TIMEOUT,
       SORT_REASON_NO_CONSENSUS, SORT_REASON_START_TIMEOUT, SORT_REASON_MOTION_TIMEOUT };
#define SORT_WINDOW 5U
#define SORT_UNCERTAIN 4U
typedef struct {
    unsigned state, result, samples, votes[4], uncertain, last_frame;
    uint64_t since;
    unsigned window[SORT_WINDOW], window_pos, window_count;
    unsigned mixed_total, reason, last_mask, last_any;
} SortPolicy;
/* fresh strong_mask bits: scratch=1, dent=2, stain=4.
 * any_candidate includes weak evidence; no held tracking boxes are inputs.
 * start is a debounced pulse, accepted only while idle. */
static inline int sort_update(SortPolicy *p, uint64_t ms, unsigned frame,
    unsigned start, unsigned valid, unsigned strong_mask,
    unsigned any_candidate, unsigned arm_busy, unsigned arm_done)
{
    unsigned c,vote;
    if (p->state==SORT_IDLE) {
        if(start) {
            *p=(SortPolicy){0};
            p->state=SORT_SETTLE; p->since=ms; p->last_frame=frame;
        }
        return 0;
    }
    if(p->state==SORT_SETTLE) {
        if(ms-p->since>=3000) {
            p->state=SORT_OBSERVE;p->since=ms;p->last_frame=frame;
        }
        return 0;
    }
    if(p->state==SORT_OBSERVE) {
        if(ms-p->since>10000) {
            p->reason=p->mixed_total?SORT_REASON_CONFLICT_TIMEOUT:SORT_REASON_NO_CONSENSUS;
            p->state=SORT_ERROR;return 0;
        }
        if(frame==p->last_frame)return 0;
        p->last_frame=frame;
        p->last_mask=strong_mask;p->last_any=any_candidate;
        if(!valid || (strong_mask & ~7U)) {
            p->reason=SORT_REASON_INVALID;p->state=SORT_ERROR;return 0;
        }
        p->samples++;
        if(strong_mask==1)vote=1;
        else if(strong_mask==2)vote=2;
        else if(strong_mask==4)vote=3;
        else if(!strong_mask && !any_candidate)vote=0;
        else {
            vote=SORT_UNCERTAIN;
            if(strong_mask && (strong_mask & (strong_mask-1U)))p->mixed_total++;
        }
        /* Count the latest five distinct frames. Mixed/weak frames abstain;
         * a transient outlier ages out rather than latching an error. */
        if(p->window_count==SORT_WINDOW) {
            unsigned old=p->window[p->window_pos];
            if(old<SORT_UNCERTAIN)p->votes[old]--;else p->uncertain--;
        } else p->window_count++;
        p->window[p->window_pos]=vote;
        p->window_pos=(p->window_pos+1U)%SORT_WINDOW;
        if(vote<SORT_UNCERTAIN)p->votes[vote]++;else p->uncertain++;
        if(p->window_count==SORT_WINDOW && !arm_busy) {
            for(c=0;c<4;c++)if(vote==c && p->votes[c]>=4 &&
                (c || (p->votes[0]==SORT_WINDOW && !p->uncertain))) {
                p->result=c;p->state=SORT_COMMAND;p->since=ms;return 1;
            }
        }
        return 0;
    }
    if(p->state==SORT_COMMAND) {
        if(arm_busy){p->state=SORT_RUNNING;p->since=ms;}
        else if(ms-p->since>2000){p->reason=SORT_REASON_START_TIMEOUT;p->state=SORT_ERROR;}
    } else if(p->state==SORT_RUNNING) {
        if(arm_done && !arm_busy)p->state=SORT_IDLE;
        else if(ms-p->since>60000){p->reason=SORT_REASON_MOTION_TIMEOUT;p->state=SORT_ERROR;}
    }
    /* Error needs explicit acknowledgement, never an implicit good result. */
    return 0;
}
#endif
