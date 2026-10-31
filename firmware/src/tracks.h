#ifndef TRACKS_H
#define TRACKS_H
#include "multiface.h"
typedef unsigned long long MtTime;
enum {MT_ACQUIRE=1,MT_STRONG=2,MT_WEAK_PENDING=3,MT_WEAK_CONFIRMED=4,
      MT_BRIDGE=5,MT_LEASE_TIMEOUT=6,MT_STALE=7,MT_MISSES=8,
      MT_DUPLICATE_MERGED=9,MT_WEAK_ACQUIRE=10};
typedef struct {
 unsigned id,active,misses,reason,weak_hits;
 MfBox box;
 MtTime strong,evidence,weak_start,weak_until;
} MtTrack;
typedef struct {
 unsigned active,hits;
 MfBox box;
 MtTime first,last;
} MtTentative;
typedef struct {
 MtTrack t[4];
 unsigned next_id,removed_id[4],removed_reason[4],removed;
 unsigned duplicate_hits[4][4];
 MtTime last;
 MtTentative tentative[4];
 MtTentative strong_tentative[4];
} MtState;
unsigned mt_dedup(MfBox *b,unsigned count);
/* Last successful mt_step: bit per internal slot; ambiguous track count. */
extern unsigned motion_used_mask,motion_ambiguous;
/* V8 diagnostics: weak displaced association, material scene movement and
 * unmatched tracks cleared immediately because movement was observed. */
extern unsigned weak_motion_used_mask,scene_motion_detected,motion_fast_cleared;
/* Number of new tracks created this frame only after three recurring weak
 * scratch observations; zero in normal frames. */
extern unsigned weak_tentative_acquired;
/* Number of stale weak/bridge tracks retired because a current strong track
 * clearly took over the same physical object after motion. */
extern unsigned handoff_merged;
/* Last mt_step: unmatched strong boxes rejected only for new-track acquisition.
 * Existing tracks and their normal strong/weak refresh policy are unchanged. */
extern unsigned acquisition_guard_rejected;
int mt_edge_clipped(const MfBox *b);
int mt_plausible_acquisition(const MfBox *b);
int mt_step(MtState *s,MfBox *b,unsigned count,MtTime now);
#endif
