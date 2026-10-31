#ifndef ASSOCIATION_POLICY_H
#define ASSOCIATION_POLICY_H
/* Display-only bounded association. No hardware dependencies or model mutation. */
typedef struct {
    int active;
    unsigned misses, last_iou;
    unsigned long long strong_ms, evidence_ms;
    int anchor[4];
} AssociationPolicy;
enum { A_NO_TRACK=1, A_MATCH=5, A_BRIDGE=6, A_EXPIRED=7, A_MISSED=8, A_STALE=9 };
static inline int ap_min(int a,int b) {return a<b?a:b;}
static inline int ap_max(int a,int b) {return a>b?a:b;}
static inline int ap_abs(int a) {return a<0?-a:a;}
static inline void ap_acquire(AssociationPolicy *s,unsigned long long now,const int b[4]) {
    int i;
    s->active=1;s->misses=0;s->last_iou=1000;s->strong_ms=now;s->evidence_ms=now;
    for(i=0;i<4;i++) s->anchor[i]=b[i];
}
static inline int ap_associated(AssociationPolicy *s,const int b[4],int valid) {
    int w,h,pw,ph,iw,ih,area;
    s->last_iou=0;
    if(!valid || b[0]<0 || b[1]<0 || b[2]>1247 || b[3]>623) return 0;
    w=b[2]-b[0];h=b[3]-b[1];pw=s->anchor[2]-s->anchor[0];ph=s->anchor[3]-s->anchor[1];
    if(w<=0 || h<=0 || pw<=0 || ph<=0) return 0;
    iw=ap_max(0,ap_min(b[2],s->anchor[2])-ap_max(b[0],s->anchor[0]));
    ih=ap_max(0,ap_min(b[3],s->anchor[3])-ap_max(b[1],s->anchor[1]));
    area=w*h+pw*ph-iw*ih;
    s->last_iou=(unsigned)((unsigned long long)iw*ih*1000/area);
    return s->last_iou>=350 && w*2<=pw*5 && pw*2<=w*5 && h*2<=ph*5 && ph*2<=h*5 &&
        ap_abs(b[0]+b[2]-s->anchor[0]-s->anchor[2])<=ap_max(w,pw) &&
        ap_abs(b[1]+b[3]-s->anchor[1]-s->anchor[3])<=ap_max(h,ph);
}
static inline unsigned ap_weak(AssociationPolicy *s,unsigned long long now,float score,const int b[4],int geometry_valid) {
    s->last_iou=0;
    if(!s->active) return A_NO_TRACK;
    /* Absolute strong-evidence limit: matching weak frames cannot renew it. */
    if(now<s->strong_ms || now-s->strong_ms>3000) {s->active=0;return A_EXPIRED;}
    if(now<s->evidence_ms || now-s->evidence_ms>1200) {s->active=0;return A_STALE;}
    if(score>=0.20f && score<0.50f && ap_associated(s,b,geometry_valid)) {
        s->misses=0;s->evidence_ms=now;return A_MATCH;
    }
    s->last_iou=0;s->misses++;
    /* Bridge at most one missing result, never longer than 700 ms since evidence. */
    if(s->misses<2 && now-s->evidence_ms<=700) return A_BRIDGE;
    s->active=0;return A_MISSED;
}
#endif
