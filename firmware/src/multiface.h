#ifndef MULTIFACE_H
#define MULTIFACE_H
#define MF_CANDIDATES 2535U
#define MF_MAX_FACES 4U
#define MF_CLASSES 3U
#define MF_STAIN_CONTAINMENT_PERMILLE 800U
#define MF_WEAK_THRESHOLD 0.50f
#define MF_NMS_PERMILLE 400U
#define MF_SCRATCH_STRONG_THRESHOLD 0.950f
#define MF_DENT_STRONG_THRESHOLD 0.990f
#define MF_STAIN_STRONG_THRESHOLD 0.950f
#define MF_DIAG_STRONG_THRESHOLD MF_SCRATCH_STRONG_THRESHOLD
typedef struct { const float *sig,*width[3],*height[3]; } MfTables;
typedef struct {
    float score;
    int x1,y1,x2,y2;
    unsigned class_id,head,anchor,row,col,suppressed;
} MfBox;
int mf_stain_duplicate(const MfBox *a,const MfBox *b);
static inline float mf_strong_threshold(unsigned class_id) {
    static const float threshold[MF_CLASSES] = {
        MF_SCRATCH_STRONG_THRESHOLD, MF_DENT_STRONG_THRESHOLD,
        MF_STAIN_STRONG_THRESHOLD
    };
    return class_id < MF_CLASSES ? threshold[class_id] : 2.0f;
}
static inline int mf_is_strong(const MfBox *box) {
    return box && box->class_id < MF_CLASSES &&
           box->score >= mf_strong_threshold(box->class_id);
}
/* Three classes: each anchor is [x,y,w,h,obj,scratch,dent,stain].
 * head15: 13*13*24 bytes; head22: 26*26*24 bytes, packed 8-channel groups.
 * Caller owns work[2535] and out[4]. No heap, MMIO or display writes.
 * threshold is true sigmoid(object)*sigmoid(class), NOT quantized-byte product.
 * Return selected count (0..4), -1 invalid arguments, -2 workspace too small. */
int mf_detect(const unsigned char *h15,unsigned size15,const unsigned char *h22,unsigned size22,
              const MfTables tables[2],float threshold,unsigned nms_permille,
              MfBox *work,unsigned capacity,MfBox out[MF_MAX_FACES],unsigned *candidates);
int mf_decode(const unsigned char *data,unsigned size,unsigned head,const MfTables *t,
              unsigned k,unsigned row,unsigned col,MfBox *out);
/* Destructive workspace: initializes from boxes with suppressed=0, marks them used.
 * work and out must not overlap. NMS is class-aware. IoU uses continuous coordinate areas, suppresses
 * strictly greater than nms_permille/1000. Coordinates exclude hardware offsets.
 * Only the returned number of output entries is valid; zero means clear all.
 * A threshold of .2 is diagnostic only, not a weak-track acquisition policy. */
unsigned mf_nms(MfBox *work,unsigned count,unsigned nms_permille,MfBox out[MF_MAX_FACES]);
#endif
