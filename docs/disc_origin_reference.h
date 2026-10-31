#ifndef DISC_ORIGIN_REFERENCE_H
#define DISC_ORIGIN_REFERENCE_H
/* Explicit user-selected native 1280x720 reference, not a fixed pixel centre.
 * Fixed RED inner frame, user confirmed fixed pose 2026-10-29, 100x100 mm.
 * Image SHA256: F5BDF86DEA0E1A7DD58C3DAEAF2FC36FF0D5C42D5D6D75EF4398C9D4C4B10B0A
 * Ordered INNER corners O, X, XY, Y, from native non-red-to-red edge fits.
 * O is bottom-left in this native camera view, X right, Y up, 100x100 mm.
 * Set DISC_FIXED_ORIGIN=0 only for an explicit marker-mode comparison build. */
#ifndef DISC_FIXED_ORIGIN
#define DISC_FIXED_ORIGIN 1
#endif
#define DISC_ORIGIN_REFERENCE_TAG 0xF5BDF86DU
#define DISC_TAPE_RED 1
#define DISC_EDGE_ROI_POLICY 0x45444732U /* EDG2: current visible-edge refit */
#define DISC_ORIGIN_REFERENCE_CORNERS { \
    {434.801,518.695}, {928.383,515.982}, \
    {920.198,137.363}, {427.720,147.091} }
/* Conservative experimental camera-pose gates; require board validation.
 * No gate can detect a 180-degree flip of a perfectly symmetric unmarked
 * square. Camera mount, mirroring and taught observation direction must stay
 * unchanged; re-calibrate after changing any of them. */
#define DISC_ORIGIN_MAX_TRANSLATION_PX 80.0
#define DISC_ORIGIN_MAX_SHAPE_RESIDUAL_PX 32.0
#define DISC_ORIGIN_MIN_EDGE_RATIO 0.90
#define DISC_ORIGIN_MAX_EDGE_RATIO 1.10
#define DISC_ORIGIN_MIN_DIRECTION_COS 0.9902680687415704 /* 8 degrees */
#endif
