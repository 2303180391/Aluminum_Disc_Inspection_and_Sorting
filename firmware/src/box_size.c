#include "box_size.h"
#include <string.h>
static int distance(int a,int b){return a>b?a-b:b-a;}
static int eighth(int old,int target,int deadband){
 int d=target-old,m;
 if(distance(old,target)<=deadband)return old;
 m=(distance(old,target)+7)/8;
 return old+(d>0?m:-m);
}
static int half(int old,int target,int deadband){
 int d=target-old,m;
 if(distance(old,target)<=deadband)return old;
 m=(distance(old,target)+1)/2;
 return old+(d>0?m:-m);
}
static int step(int old,int target){
 int d=target-old;
 /* Faster recovery toward an already accepted size; never overshoot.
  * Small remainder snaps to target, still bounded by 50% per update. */
 if(d>0){int change=d<=8?d:(3*d+3)/4,cap=(old+1)/2;return old+(change<cap?change:cap);}
 if(-d<=2)return old;
 return old-((-d+3)/4);
}
void box_size_apply(BoxSizeState *state,const MtState *raw,MtState *out){
 BoxSizeState next;unsigned i,j;memset(&next,0,sizeof next);*out=*raw;
 for(i=0;i<4;i++)if(raw->t[i].active){
  const MtTrack *t=&raw->t[i];MfBox *b=&out->t[i].box;BoxSizeSlot s={0};
  int w=b->x2-b->x1,h=b->y2-b->y1,cx=b->x1+b->x2,cy=b->y1+b->y2;
  if(w<=0 || h<=0){out->t[i].active=0;continue;}
  for(j=0;j<4;j++)if(state->slot[j].id==t->id){s=state->slot[j];break;}
  if(!s.id){s.id=t->id;s.w=s.tw=w;s.h=s.th=h;s.cx=cx;s.cy=cy;s.strong=t->strong;}
  else if(t->reason==MT_STRONG && t->strong!=s.strong){
   int shrink=w*100<s.tw*75 || h*100<s.th*75;
   int huge=w*100>s.tw*135 || h*100>s.th*135;
   s.strong=t->strong;
   if(shrink || huge){
    if(s.hits && distance(w,s.pw)*100<=s.pw*15 && distance(h,s.ph)*100<=s.ph*15){if(s.hits<3)s.hits++;}
    else{s.pw=w;s.ph=h;s.hits=1;}
    if(s.hits>=2U){s.tw=w;s.th=h;s.hits=0;}
   }else{s.hits=0;s.tw=eighth(s.tw,w,2);s.th=eighth(s.th,h,2);}
  }
  if(t->reason==MT_ACQUIRE || t->reason==MT_WEAK_ACQUIRE){s.cx=cx;s.cy=cy;}
  else if(scene_motion_detected){s.cx=half(s.cx,cx,8);s.cy=half(s.cy,cy,8);}
  else{s.cx=eighth(s.cx,cx,8);s.cy=eighth(s.cy,cy,8);}
  /* Continue toward an already accepted target on held frames. This is
   * animation, not fresh evidence; weak frames never increment hits. */
  s.w=step(s.w,s.tw);s.h=step(s.h,s.th);
  /* Static center uses a small deadband/quarter-step. Confirmed scene motion
   * snaps to the current center, so smoothing does not slow deliberate moves. */
  b->x1=(s.cx-s.w)/2;b->x2=b->x1+s.w;b->y1=(s.cy-s.h)/2;b->y2=b->y1+s.h;
  next.slot[i]=s;
 }
 *state=next; /* Inactive IDs disappear immediately, no extra box lease. */
}
