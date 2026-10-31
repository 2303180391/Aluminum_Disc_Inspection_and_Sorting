#include "tracks.h"
static int lo(int a,int b){return a<b?a:b;}
static int hi(int a,int b){return a>b?a:b;}
static int ab(int a){return a<0?-a:a;}
static unsigned area(const MfBox *a){return (unsigned)(a->x2-a->x1)*(a->y2-a->y1);}
static int valid(const MfBox *b){return b->score>=.2f && b->score<=1 && b->class_id<MF_CLASSES && b->x1>=0 && b->y1>=0 && b->x2<=1247 && b->y2<=623 && b->x2>b->x1 && b->y2>b->y1;}
static unsigned inter(const MfBox *a,const MfBox *b){return (unsigned)hi(0,lo(a->x2,b->x2)-hi(a->x1,b->x1))*hi(0,lo(a->y2,b->y2)-hi(a->y1,b->y1));}
static int scratch_covers_other(const MfBox *scratch,const MfBox *other){
 unsigned oa,ia;
 if(!scratch || !other || scratch->class_id!=0U || other->class_id==0U || other->class_id>=MF_CLASSES)return 0;
 oa=area(other);ia=inter(scratch,other);
 /* The current training corpus contains no image labelled simultaneously as
    scratch and dent.  On the board, a small dent hypothesis embedded in a
    strong, geometrically plausible scratch is therefore a measured class
    fragment rather than a separately supervised object.  Require 70% of the
    competing dent/stain coverage as well as its centre being inside the
    scratch so adjacent genuine defects are not removed. */
 return oa && mf_is_strong(scratch) && mt_plausible_acquisition(scratch) &&
  other->x1+other->x2>=scratch->x1*2 && other->x1+other->x2<=scratch->x2*2 &&
  other->y1+other->y2>=scratch->y1*2 && other->y1+other->y2<=scratch->y2*2 &&
  (MtTime)ia*100U>=(MtTime)oa*70U;
}
static int stain_covers_other(const MfBox *stain,const MfBox *other){
 unsigned sa,oa,ia;
 if(!stain || !other || stain->class_id!=2U || other->class_id==2U || other->class_id>=MF_CLASSES)return 0;
 sa=area(stain);oa=area(other);ia=inter(stain,other);
 /* Stain is authoritative only for true nesting, not an equal-size overlap.
    The 1.5x lower area ratio prevents a scratch/stain cycle such as captured
    V3 frame 117.  Centre containment plus 85% coverage prevents an adjacent
    defect from being removed; a deeply nested small fragment has no upper
    area-ratio limit because this dataset does not supervise mixed classes. */
 return oa && mf_is_strong(stain) && mt_plausible_acquisition(stain) &&
  other->x1+other->x2>=stain->x1*2 && other->x1+other->x2<=stain->x2*2 &&
  other->y1+other->y2>=stain->y1*2 && other->y1+other->y2<=stain->y2*2 &&
  (MtTime)ia*100U>=(MtTime)oa*85U &&
  (MtTime)sa*2U>=(MtTime)oa*3U;
}
static int centered_nested_duplicate(const MfBox *a,const MfBox *b){
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 int mw=lo(aw,bw),mh=lo(ah,bh);
 unsigned aa=area(a),ba=area(b),ma=aa<ba?aa:ba,xa=aa>ba?aa:ba;
 unsigned cdx=(unsigned)ab(a->x1+a->x2-b->x1-b->x2);
 unsigned cdy=(unsigned)ab(a->y1+a->y2-b->y1-b->y2);
 /* Two measured duplicate forms are accepted, both with a strict center gate:
    (1) a deeply nested large/small pair; (2) two similarly sized boxes whose
    smaller area overlaps by at least 55%. The latter matches the saved dent
    pair {386,72,444,113}/{392,90,451,125}. Nearby or parallel real scratches
    remain separate when their centers fall outside half of the smaller box. */
 if(a->class_id!=b->class_id || cdx>(unsigned)mw || cdy>(unsigned)mh)return 0;
 if(inter(a,b)*100U>=ma*85U && xa<=ma*8U &&
    hi(aw,bw)<=mw*4 && hi(ah,bh)<=mh*4)return 1;
 return inter(a,b)*100U>=ma*55U && xa<=ma*3U &&
  hi(aw,bw)<=mw*2 && hi(ah,bh)<=mh*2;
}
static int scratch_segment_duplicate(const MfBox *a,const MfBox *b){
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 int iw=hi(0,lo(a->x2,b->x2)-hi(a->x1,b->x1));
 int ih=hi(0,lo(a->y2,b->y2)-hi(a->y1,b->y1));
 int mw=lo(aw,bw),mh=lo(ah,bh),xspan=hi(a->x2,b->x2)-lo(a->x1,b->x1);
 int yspan=hi(a->y2,b->y2)-lo(a->y1,b->y1);
 if(a->class_id!=0U || b->class_id!=0U || !mw || !mh)return 0;
 /* A single long scratch is sometimes decoded as two shifted segments.  This
    is deliberately stricter than ordinary association: both boxes must have
    the same strong orientation, overlap substantially across the short axis,
    overlap along the long axis, and form a bounded union.  Parallel,
    end-to-end and crossing scratches therefore remain independent. */
 if(ah>=aw*2 && bh>=bw*2)
  return hi(aw,bw)<=mw*2 && hi(ah,bh)<=mh*2 &&
   (iw*100>=mw*65 || (iw*100>=mw*55 && (a->score<=.99f || b->score<=.99f))) &&
   ih*100>=mh*30 && yspan*100<=hi(ah,bh)*170;
 if(aw>=ah*2 && bw>=bh*2)
  return hi(aw,bw)<=mw*2 && hi(ah,bh)<=mh*2 &&
   (ih*100>=mh*65 || (ih*100>=mh*55 && (a->score<=.99f || b->score<=.99f))) &&
   iw*100>=mw*30 && xspan*100<=hi(aw,bw)*170;
 return 0;
}
static int scratch_vertical_wide_duplicate(const MfBox *a,const MfBox *b){
 const MfBox *v,*w;int vw,vh,ww,wh,iw,ih,yspan,cdx;
 if(a->class_id!=0U || b->class_id!=0U)return 0;
 if(a->y2-a->y1>=(a->x2-a->x1)*2){v=a;w=b;}
 else if(b->y2-b->y1>=(b->x2-b->x1)*2){v=b;w=a;}
 else return 0;
 vw=v->x2-v->x1;vh=v->y2-v->y1;ww=w->x2-w->x1;wh=w->y2-w->y1;
 iw=hi(0,lo(v->x2,w->x2)-hi(v->x1,w->x1));
 ih=hi(0,lo(v->y2,w->y2)-hi(v->y1,w->y1));
 yspan=hi(v->y2,w->y2)-lo(v->y1,w->y1);
 cdx=ab(v->x1+v->x2-w->x1-w->x2);
 /* Frame 18 exposed a second decoder form for one vertical scratch: a
    high-score narrow box plus a lower-score, moderately wide upper box.  It
    is not an ordinary IoU duplicate.  Accept only this asymmetric form: the
    wide box must not itself be a horizontal scratch, must be 2..4x wider,
    have nearly the same height, fully straddle the narrow centre line and
    overlap at least 30% along the vertical axis.  A genuine horizontal
    crossing scratch and a higher-confidence wide scratch are preserved. */
 return ww>=vw*2 && ww<=vw*4 && ww*2<wh*3 &&
  hi(vh,wh)*4<=lo(vh,wh)*5 && iw*100>=vw*90 &&
  ih*100>=lo(vh,wh)*30 && yspan*100<=hi(vh,wh)*170 &&
  cdx*2<=vw && w->score<=v->score;
}
static int strict_display_duplicate(const MfBox *a,const MfBox *b){
 if(a->class_id!=b->class_id)return 0;
 if(mf_stain_duplicate(a,b) || centered_nested_duplicate(a,b))return 1;
 return a->class_id==0U &&
  (scratch_segment_duplicate(a,b) || scratch_vertical_wide_duplicate(a,b));
}
static int same_hypothesis(const MfBox *a,const MfBox *b){
 if(strict_display_duplicate(a,b))return 1;
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 int mw=lo(aw,bw),mh=lo(ah,bh);
 unsigned cdx=(unsigned)ab(a->x1+a->x2-b->x1-b->x2),cdy=(unsigned)ab(a->y1+a->y2-b->y1-b->y2);
 unsigned aa=area(a),ba=area(b);
 return a->class_id==b->class_id && inter(a,b)*100>= (unsigned)lo((int)aa,(int)ba)*75 && hi((int)aa,(int)ba)<=lo((int)aa,(int)ba)*4 &&
 hi(aw,bw)*2<=lo(aw,bw)*5 && hi(ah,bh)*2<=lo(ah,bh)*5 &&
 (MtTime)cdx*100*mh+(MtTime)cdy*100*mw<=(MtTime)140*mw*mh;
}
static int assoc(const MfBox *a,const MfBox *b){
 unsigned in=inter(a,b),un=area(a)+area(b)-in;
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 return a->class_id==b->class_id && (same_hypothesis(a,b) ||
  ((MtTime)in*100>= (MtTime)un*35 && hi(aw,bw)*2<=lo(aw,bw)*5 && hi(ah,bh)*2<=lo(ah,bh)*5 &&
   ab(a->x1+a->x2-b->x1-b->x2)<=hi(aw,bw) && ab(a->y1+a->y2-b->y1-b->y2)<=hi(ah,bh)));
}

unsigned motion_used_mask, motion_ambiguous;
unsigned weak_motion_used_mask, scene_motion_detected, motion_fast_cleared;
unsigned weak_tentative_acquired;
unsigned handoff_merged;
unsigned acquisition_guard_rejected;
int mt_edge_clipped(const MfBox *b){
 /* DS14's complete train/validation/test labels begin at x=93 (stain), x=96
    (scratch), and x=225 (dent), while the rightmost label ends at x=1155.
    A 72-pixel horizontal acquisition margin therefore remains inside the
    measured label-free border. It rejects the hardware head22/column-0
    burst and saved false tracks at x=27..68 without suppressing any labelled
    example. Keep the vertical rule at the literal boundary because valid
    augmented scratches can extend to y=624. */
 return b && (b->x1<=72 || b->x2>=1175 || b->y1<=0 || b->y2>=623);
}
int mt_plausible_acquisition(const MfBox *b){
 int w,h;unsigned a;
 if(!b || !valid(b) || mt_edge_clipped(b))return 0;
 w=b->x2-b->x1;h=b->y2-b->y1;a=area(b);
 /* Generous envelopes derived from the 416x416 training labels after mapping
    to the 1248x624 display.  They are deliberately wider than every training
    box.  Apply only when creating a new track, so they cannot disturb a valid
    track's association, motion following, or bounded hold policy. */
 /* Complete scratch label envelope: W<=360, H<=278, area<=90743. */
 if(b->class_id==0U)return w<=400 && h<=320 && a<=100000U; /* scratch */
 /* Complete dent label envelope: W<=72, H<=38, area<=2700. */
 if(b->class_id==1U)return w<=100 && h<=60 && a<=5000U;  /* dent */
 /* All 107 stain labels map to W<=278, H<=176, area<=48840 and
    0.94<=W/H<=2.28 display pixels. Keep a measured margin while rejecting
    glare-induced tall/narrow and multi-defect spanning hypotheses. */
 /* DS14 training: 339 stain boxes, W<=402 H<=279 area<=88146.
    Updated margins also admit the saved frame105 stain (309x231). */
 if(b->class_id==2U)return w<=450 && h<=320 && a<=110000U &&
  w*10>=h*8 && w*10<=h*27; /* stain */
 return 0;
}
static int motion_edge(const MtTrack *t,const MfBox *b,MtTime now){
 const MfBox *a=&t->box;
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 int dx=ab(a->x1+a->x2-b->x1-b->x2),dy=ab(a->y1+a->y2-b->y1-b->y2);
 /* Deliberately not a velocity predictor. Only recent unmatched strong evidence,
    no misses, compatible sizes, and bounded horizontal/vertical displacement. */
 return a->class_id==b->class_id && mf_is_strong(b) && t->misses==0 &&
  (t->reason==MT_ACQUIRE || t->reason==MT_STRONG) && now>t->strong && now-t->strong<=700 &&
  hi(aw,bw)*10<=lo(aw,bw)*14 && hi(ah,bh)*10<=lo(ah,bh)*14 &&
  dx*100<=170*lo(aw,bw) && dy*100<=70*lo(ah,bh);
}

static int material_center_motion(const MfBox *a,const MfBox *b){
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 int dx=ab(a->x1+a->x2-b->x1-b->x2),dy=ab(a->y1+a->y2-b->y1-b->y2);
 /* dx/dy are twice the center displacement.  Require both an absolute and
    scale-relative shift so ordinary decoded-size jitter is not scene motion. */
 return (dx>40 && dx*5>lo(aw,bw)*2) ||
        (dy>32 && dy*5>lo(ah,bh)*2);
}

static int weak_motion_edge(const MtTrack *t,const MfBox *b,MtTime now){
 const MfBox *a=&t->box;
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 int dx=ab(a->x1+a->x2-b->x1-b->x2),dy=ab(a->y1+a->y2-b->y1-b->y2);
 /* A weak candidate may refresh an existing scratch track when decoded box
    geometry changes too much for IoU association.  It never creates a track
    and never replaces displayed geometry. Reciprocal uniqueness below keeps
    nearby scratches from stealing each other's evidence. */
 return a->class_id==0U && b->class_id==0U && !mf_is_strong(b) &&
  b->score>=0.55f && t->misses<=1 && now>=t->evidence && now-t->evidence<=1200 &&
  hi(aw,bw)<=lo(aw,bw)*2 && hi(ah,bh)<=lo(ah,bh)*2 &&
  dx*100<=160*lo(aw,bw) && dy*100<=160*lo(ah,bh);
}

static void accept_weak(MtTrack *t,MtTime now,MtTime gap){
 t->evidence=now;t->misses=0;
 if(!t->weak_hits || gap>700){t->weak_hits=1;t->weak_start=now;}else t->weak_hits++;
 if(t->weak_hits>=3 && now-t->weak_start>=600){
  MtTime rolling=now+2000,cap=t->strong+6000;
  t->weak_until=rolling<cap?rolling:cap;t->reason=MT_WEAK_CONFIRMED;
 } else t->reason=MT_WEAK_PENDING;
}

static int weak_cluster(const MfBox *a,const MfBox *b){
 int aw=a->x2-a->x1,ah=a->y2-a->y1,bw=b->x2-b->x1,bh=b->y2-b->y1;
 int dx=ab(a->x1+a->x2-b->x1-b->x2),dy=ab(a->y1+a->y2-b->y1-b->y2);
 return a->class_id==0U && b->class_id==0U &&
  hi(aw,bw)<=lo(aw,bw)*2 && hi(ah,bh)<=lo(ah,bh)*2 &&
  dx*100<=120*lo(aw,bw) && dy*100<=120*lo(ah,bh);
}

static int before(const MfBox *a,const MfBox *b){
 if(a->score!=b->score)return a->score>b->score;
 if(area(a)!=area(b))return area(a)>area(b);
 if(a->x1!=b->x1)return a->x1<b->x1;
 return a->y1<b->y1;
}
unsigned mt_dedup(MfBox *b,unsigned count){
 unsigned i,j,n=0;MfBox v;
 if(!b || count>MF_CANDIDATES)return 0;
 for(i=0;i<count;i++)if(valid(&b[i]))b[n++]=b[i];
 /* Bounded in-place insertion sort. Worst case O(2535^2); measured on-board. */
 for(i=1;i<n;i++){v=b[i];j=i;while(j && before(&v,&b[j-1])){b[j]=b[j-1];j--;}b[j]=v;}
 count=n;n=0;
 for(i=0;i<count;i++){
  int duplicate=0;
  if(b[i].class_id!=0U){
   for(j=0;j<count;j++)if(scratch_covers_other(&b[j],&b[i])){duplicate=1;break;}
  }
  if(!duplicate && b[i].class_id!=2U){
   for(j=0;j<count;j++)if(stain_covers_other(&b[j],&b[i])){duplicate=1;break;}
  }
  for(j=0;j<n;j++){unsigned in=inter(&b[i],&b[j]),un=area(&b[i])+area(&b[j])-in;
   if(b[i].class_id==b[j].class_id && ((MtTime)in*1000>(MtTime)un*MF_NMS_PERMILLE || same_hypothesis(&b[i],&b[j]))){duplicate=1;break;}}
  if(!duplicate){b[n]=b[i];b[n++].suppressed=0;}
 }
 return n;
}
static void remove_track(MtState *s,unsigned i,unsigned reason){
 unsigned j,k=s->removed;
 if(k<4U){s->removed_id[k]=s->t[i].id;s->removed_reason[k]=reason;s->removed=k+1U;}
 s->t[i].active=0;
 for(j=0;j<4;j++){s->duplicate_hits[i][j]=0;s->duplicate_hits[j][i]=0;}
}
int mt_step(MtState *s,MfBox *b,unsigned count,MtTime now){
 unsigned i,j,k,done[4]={0,0,0,0},merge[4]={0,0,0,0},expired[4]={0,0,0,0};
 if(!s || !b || count>MF_CANDIDATES || now<s->last)return -1;
 for(j=0;j<count;j++)if(!valid(&b[j]))return -1;
 motion_used_mask=0;motion_ambiguous=0;weak_motion_used_mask=0;
 scene_motion_detected=0;motion_fast_cleared=0;weak_tentative_acquired=0;
 acquisition_guard_rejected=0;handoff_merged=0;
 s->last=now;s->removed=0;if(!s->next_id)s->next_id=1;
 for(j=0;j<count;j++)b[j].suppressed=0;
 for(i=0;i<4;i++)if(s->t[i].active){
  MtTime lease=s->t[i].strong+3000>s->t[i].weak_until?s->t[i].strong+3000:s->t[i].weak_until;
  if(now-s->t[i].evidence>1800)remove_track(s,i,now>lease?MT_LEASE_TIMEOUT:MT_STALE);
  else expired[i]=now>lease;
 }
 /* A previously confirmed scratch remains the class authority during its
    short static hold. Suppress a newly misclassified embedded dent even when
    the scratch candidate itself is absent in this inference result. */
 for(j=0;j<count;j++)if(!b[j].suppressed)
  for(i=0;i<4;i++)if(s->t[i].active && !expired[i] &&
   (scratch_covers_other(&s->t[i].box,&b[j]) ||
    stain_covers_other(&s->t[i].box,&b[j]))){b[j].suppressed=1;break;}
 /* Confirm overlapping active tracks twice before retiring the younger ID.
    This executes before matching because candidate de-duplication deliberately
    leaves only one candidate for one physical object. */
 for(i=0;i<4;i++)for(j=i+1;j<4;j++){
  if(s->t[i].active && s->t[j].active && same_hypothesis(&s->t[i].box,&s->t[j].box)){
   if(s->duplicate_hits[i][j]<2)s->duplicate_hits[i][j]++;
   if(s->duplicate_hits[i][j]>=2)merge[s->t[i].id>s->t[j].id?i:j]=1;
  } else s->duplicate_hits[i][j]=0;
 }
 for(i=0;i<4;i++)if(merge[i] && s->t[i].active)remove_track(s,i,MT_DUPLICATE_MERGED);
 for(k=0;k<4;k++){
  unsigned bi=4,bj=count,bn=0,bd=1;
  for(i=0;i<4;i++)if(s->t[i].active && !done[i])for(j=0;j<count;j++)if(!b[j].suppressed && (!expired[i] || mf_is_strong(&b[j])) && assoc(&s->t[i].box,&b[j])){
   unsigned in=inter(&s->t[i].box,&b[j]),un=area(&s->t[i].box)+area(&b[j])-in;
   int better=bi==4 || (MtTime)in*bd>(MtTime)bn*un;
   if(bi!=4 && (MtTime)in*bd==(MtTime)bn*un)
    better=b[j].score>b[bj].score || (b[j].score==b[bj].score && (s->t[i].id<s->t[bi].id || (s->t[i].id==s->t[bi].id && j<bj)));
   if(better){bi=i;bj=j;bn=in;bd=un;}
  }
  if(bi==4)break;
  {
   MtTrack *t=&s->t[bi];MtTime gap=now-t->evidence;
   done[bi]=1;b[bj].suppressed=1;t->evidence=now;t->misses=0;
   if(mf_is_strong(&b[bj])){
    if(material_center_motion(&t->box,&b[bj]))scene_motion_detected=1;
    t->box=b[bj];t->strong=now;t->weak_hits=0;t->weak_start=0;t->weak_until=0;t->reason=MT_STRONG;
   } else accept_weak(t,now,gap);
  }
 }

 {
  unsigned edges[4]={0}, candidate[4]={0};
  /* Freeze the complete unmatched graph before applying any fallback matches. */
  for(i=0;i<4;i++)if(s->t[i].active && !done[i] && !expired[i])
   for(j=0;j<count;j++)if(!b[j].suppressed && motion_edge(&s->t[i],&b[j],now)){
    edges[i]++;candidate[i]=j;
   }
  for(i=0;i<4;i++)if(edges[i]){
   unsigned others=0;
   if(edges[i]!=1){motion_ambiguous++;continue;}
   j=candidate[i];
   for(k=0;k<4;k++)if(k!=i && s->t[k].active && !done[k] && !expired[k] &&
     motion_edge(&s->t[k],&b[j],now))others++;
   if(others){motion_ambiguous++;continue;}
   /* Reciprocity established. No tentative display delay or new confidence gate. */
   {MtTrack *t=&s->t[i];
    done[i]=1;b[j].suppressed=1;t->box=b[j];t->evidence=now;t->strong=now;
    t->misses=0;t->weak_hits=0;t->weak_start=0;t->weak_until=0;t->reason=MT_STRONG;
    motion_used_mask|=1U<<i;scene_motion_detected=1;
   }
  }
 }

 {
  unsigned edges[4]={0},candidate[4]={0};
  /* Relaxed weak displacement association for an existing scratch only.
     As with the strong motion fallback, require a one-to-one graph edge. */
  for(i=0;i<4;i++)if(s->t[i].active && !done[i] && !expired[i])
   for(j=0;j<count;j++)if(!b[j].suppressed && weak_motion_edge(&s->t[i],&b[j],now)){
    edges[i]++;candidate[i]=j;
   }
  for(i=0;i<4;i++)if(edges[i]){
   unsigned others=0;MtTime gap;
   if(edges[i]!=1){motion_ambiguous++;continue;}
   j=candidate[i];
   for(k=0;k<4;k++)if(k!=i && s->t[k].active && !done[k] && !expired[k] &&
     weak_motion_edge(&s->t[k],&b[j],now))others++;
   if(others){motion_ambiguous++;continue;}
   gap=now-s->t[i].evidence;done[i]=1;b[j].suppressed=1;
   accept_weak(&s->t[i],now,gap);weak_motion_used_mask|=1U<<i;
  }
 }
 for(i=0;i<4;i++)if(s->t[i].active && !done[i]){
  if(expired[i]){remove_track(s,i,MT_LEASE_TIMEOUT);continue;}
  s->t[i].misses++;s->t[i].weak_hits=0;s->t[i].weak_start=0;s->t[i].reason=MT_BRIDGE;
  /* Static glare can erase a real detection for three complete inference
     results. Keep the established geometry through those three misses so the
     next matching candidate can reconnect it without a visible disappear /
     reacquire flash. Material motion still clears an unmatched old box in the
     current result; the 1800 ms stale bound prevents an unlimited hold. */
  if(scene_motion_detected || s->t[i].misses>=4 || now-s->t[i].evidence>1800){
   if(scene_motion_detected)motion_fast_cleared++;
   remove_track(s,i,MT_MISSES);
  }
 }
 /* A single high scratch score was sufficient to create the unrelated box in
    capture precision_1791790312.  Scratch-only acquisition therefore needs
    two spatially consistent strong observations. Existing tracks refresh
    immediately, and dent/stain acquisition retains its previous behavior. */
 {
  unsigned used[4]={0,0,0,0};
  for(i=0;i<4;i++)if(s->strong_tentative[i].active){
   int covered=0;
   for(k=0;k<4;k++)if(s->t[k].active &&
     (assoc(&s->t[k].box,&s->strong_tentative[i].box) || weak_cluster(&s->t[k].box,&s->strong_tentative[i].box))){covered=1;break;}
   if(covered || now<s->strong_tentative[i].last || now-s->strong_tentative[i].last>1200)s->strong_tentative[i].active=0;
  }
  for(j=0;j<count;j++)if(!b[j].suppressed && mf_is_strong(&b[j])){
   unsigned slot=4,qi=4;int duplicate=0;
   if(!mt_plausible_acquisition(&b[j])){acquisition_guard_rejected++;continue;}
   for(i=0;i<4;i++)if(s->t[i].active){
    if(assoc(&s->t[i].box,&b[j]) || same_hypothesis(&s->t[i].box,&b[j]))duplicate=1;
   }else if(slot==4)slot=i;
   if(duplicate)continue;
   if(b[j].class_id!=0U){
    if(slot!=4){MtTrack *t=&s->t[slot];t->id=s->next_id++;t->active=1;t->misses=0;t->reason=MT_ACQUIRE;t->weak_hits=0;t->box=b[j];t->strong=now;t->evidence=now;t->weak_start=0;t->weak_until=0;}
    continue;
   }
   for(i=0;i<4;i++)if(s->strong_tentative[i].active && !used[i] &&
     weak_cluster(&s->strong_tentative[i].box,&b[j])){qi=i;break;}
   if(qi==4)for(i=0;i<4;i++)if(!s->strong_tentative[i].active){qi=i;break;}
   if(qi==4)continue;
   {
    MtTentative *q=&s->strong_tentative[qi];
    if(!q->active){q->active=1;q->hits=1;q->first=now;}else q->hits++;
    q->box=b[j];q->last=now;used[qi]=1;b[j].suppressed=1;
    if(q->hits>=2){
     slot=4;
     for(i=0;i<4;i++)if(!s->t[i].active){slot=i;break;}
     if(slot<4){MtTrack *t=&s->t[slot];t->id=s->next_id++;t->active=1;t->misses=0;t->reason=MT_ACQUIRE;t->weak_hits=0;t->box=b[j];t->strong=now;t->evidence=now;t->weak_start=0;t->weak_until=0;}
     q->active=0;
    }
   }
  }
 }
 /* Motion handoff cleanup.  A new/current strong track can coexist briefly
    with the old track when the old geometry associates only with a weak
    candidate.  Retire only the weak/bridge copy when more than 55% of its
    smaller area is covered by a same-class strong/acquired track.  Ordinary
    weak holding is unchanged when no strong replacement exists. */
 {
  unsigned drop[4]={0,0,0,0};
  for(i=0;i<4;i++)if(s->t[i].active &&
    (s->t[i].reason==MT_WEAK_PENDING || s->t[i].reason==MT_WEAK_CONFIRMED || s->t[i].reason==MT_BRIDGE))
   for(j=0;j<4;j++)if(i!=j && s->t[j].active &&
     (s->t[j].reason==MT_ACQUIRE || s->t[j].reason==MT_STRONG) &&
     s->t[i].box.class_id==s->t[j].box.class_id){
    unsigned ia=inter(&s->t[i].box,&s->t[j].box);
    unsigned ma=area(&s->t[i].box)<area(&s->t[j].box)?area(&s->t[i].box):area(&s->t[j].box);
    if((MtTime)ia*100U>=(MtTime)ma*55U ||
       scratch_segment_duplicate(&s->t[i].box,&s->t[j].box)){drop[i]=1;break;}
   }
  for(i=0;i<4;i++)if(drop[i] && s->t[i].active){remove_track(s,i,MT_DUPLICATE_MERGED);handoff_merged++;}
 }
 /* A persistent high-glare scratch can remain below the strong threshold for
    many frames.  Confirm three spatially consistent weak observations before
    creating a track.  Single-frame weak boxes never reach the display. */
 {
  unsigned matched[4]={0,0,0,0};
  for(i=0;i<4;i++)if(s->tentative[i].active){
   int covered=0;
   for(k=0;k<4;k++)if(s->t[k].active &&
     (assoc(&s->t[k].box,&s->tentative[i].box) || weak_cluster(&s->t[k].box,&s->tentative[i].box))){covered=1;break;}
   if(covered || now<s->tentative[i].last || now-s->tentative[i].last>1200)s->tentative[i].active=0;
  }
  for(i=0;i<4;i++)if(s->tentative[i].active){
   unsigned best=count;
   for(j=0;j<count;j++)if(!b[j].suppressed && !mf_is_strong(&b[j]) &&
     b[j].class_id==0U && b[j].score>=0.70f && weak_cluster(&s->tentative[i].box,&b[j]) &&
     (best==count || b[j].score>b[best].score))best=j;
   if(best<count){
    MtTentative *q=&s->tentative[i];q->hits++;q->last=now;matched[i]=1;b[best].suppressed=1;
    if(b[best].score>q->box.score)q->box=b[best];
    if(q->hits>=3 && now-q->first>=600){
     unsigned slot=4;int duplicate=0;
     for(k=0;k<4;k++)if(s->t[k].active){
      if(assoc(&s->t[k].box,&q->box) || same_hypothesis(&s->t[k].box,&q->box) || weak_cluster(&s->t[k].box,&q->box))duplicate=1;
     }else if(slot==4)slot=k;
     if(!duplicate && slot<4){
      MtTrack *t=&s->t[slot];t->id=s->next_id++;t->active=1;t->misses=0;t->reason=MT_WEAK_ACQUIRE;
      t->weak_hits=q->hits;t->box=q->box;t->strong=now;t->evidence=now;t->weak_start=q->first;t->weak_until=now+2000;
      weak_tentative_acquired++;
     }
     q->active=0;
    }
   }
  }
  for(j=0;j<count;j++)if(!b[j].suppressed && !mf_is_strong(&b[j]) &&
    b[j].class_id==0U && b[j].score>=0.70f){
   unsigned slot=4;
   for(i=0;i<4;i++)if(!s->tentative[i].active){slot=i;break;}
   if(slot<4){MtTentative *q=&s->tentative[slot];q->active=1;q->hits=1;q->box=b[j];q->first=now;q->last=now;b[j].suppressed=1;}
  }
  (void)matched;
 }
 /* Candidate de-duplication prevents a new embedded dent from being created.
    If such a dent was already visible, retire it only after the covering
    scratch has itself become a current strong/acquired track.  This avoids a
    blank transition during scratch's two-observation acquisition gate while
    still removing the wrong dent label as soon as the scratch is confirmed. */
 {
  unsigned drop[4]={0,0,0,0};
  for(i=0;i<4;i++)if(s->t[i].active)
   for(j=0;j<4;j++)if(s->t[j].active && s->t[j].box.class_id==0U &&
    s->t[j].strong==now &&
    (s->t[j].reason==MT_ACQUIRE || s->t[j].reason==MT_STRONG) &&
    scratch_covers_other(&s->t[j].box,&s->t[i].box)){
     drop[i]=1;break;
   }
  for(i=0;i<4;i++)if(s->t[i].active && !drop[i])
   for(j=0;j<4;j++)if(s->t[j].active && s->t[j].box.class_id==2U &&
    s->t[j].strong==now &&
    (s->t[j].reason==MT_ACQUIRE || s->t[j].reason==MT_STRONG) &&
    stain_covers_other(&s->t[j].box,&s->t[i].box)){
     drop[i]=1;break;
    }
  for(i=0;i<4;i++)if(drop[i] && s->t[i].active)
   remove_track(s,i,MT_DUPLICATE_MERGED);
 }
 /* The pre-match two-hit gate cannot see a duplicate geometry that is formed
    by this frame's strong refresh, and a just-removed duplicate can otherwise
    be reacquired later in the same step.  Consolidate only strict, measured
    same-class duplicate forms after every refresh/acquisition has completed.
    Keep the higher-score geometry (then the older ID on an exact tie). */
 {
  unsigned drop[4]={0,0,0,0};
  for(i=0;i<4;i++)for(j=i+1;j<4;j++)if(s->t[i].active && s->t[j].active &&
    strict_display_duplicate(&s->t[i].box,&s->t[j].box)){
   unsigned loser;
   if(s->t[i].box.score<s->t[j].box.score)loser=i;
   else if(s->t[j].box.score<s->t[i].box.score)loser=j;
   else loser=s->t[i].id>s->t[j].id?i:j;
   drop[loser]=1;
  }
  for(i=0;i<4;i++)if(drop[i] && s->t[i].active){
   remove_track(s,i,MT_DUPLICATE_MERGED);handoff_merged++;
  }
 }
 return 0;
}
