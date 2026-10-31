#ifndef DISC_LOCATOR_H
#define DISC_LOCATOR_H
#include <stdint.h>
/* Non-reentrant. Stable native 1280x720 BGR888 only. */
enum { LOC_OK=0, LOC_NO_FRAME=1, LOC_MULTI_FRAME=2, LOC_NO_ORIGIN=3,
       LOC_NO_DISC=4, LOC_MULTI_DISC=5, LOC_BAD_GEOMETRY=6,
       LOC_REFERENCE_MISMATCH=7 };
typedef struct {
    uint32_t width_um, height_um; /* inner edge: 100000 = 100 mm */
    uint32_t dimensions_confirmed;
} DiscConfig;
typedef struct {
    uint32_t valid, candidates, x_millipixel, y_millipixel, area_samples;
    uint32_t status, frame_valid, origin_valid, mm_valid;
    int32_t x_um, y_um; /* diagnostic only unless mm_valid==1 */
    uint32_t x_ppm, y_ppm; /* normalized O->X, O->Y */
    int32_t corners_millipixel[8]; /* O, X end, opposite, Y end */
    uint32_t frame_candidates, marker_pixels;
} DiscResult;
extern const DiscConfig disc_default_config;
void disc_locate_config(const uint8_t *bgr, const DiscConfig *cfg, DiscResult *out);
void disc_locate(const uint8_t *bgr, DiscResult *out);
/* Calibration is volatile, observation-pose-specific and never a saved target. */
void disc_locator_reset_calibration(void);
unsigned disc_locator_calibrated(void);
extern unsigned disc_locator_used_cache;
extern unsigned disc_locator_pose_refit_used;
extern unsigned disc_locator_outline_recovered;
void disc_board_poll(void);
void disc_board_init(void);
#endif
