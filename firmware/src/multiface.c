#include "multiface.h"
static unsigned at(const unsigned char *p,unsigned n,unsigned k,unsigned r,unsigned c,unsigned j) {
    unsigned ch=k*8+j;return p[(ch/8)*n*n*8+(r*n+c)*8+ch%8];
}
static int min_i(int a,int b){return a<b?a:b;}
static int max_i(int a,int b){return a>b?a:b;}
static int valid_box(const MfBox *b) {
    return b->x1>=0 && b->y1>=0 && b->x2<=1247 && b->y2<=623 &&
           b->x2>b->x1 && b->y2>b->y1 && b->score>=0 && b->score<=1 && b->class_id<MF_CLASSES;
}
static int valid_tables(const MfTables *t) {
    return t && t->sig && t->width[0] && t->width[1] && t->width[2] && t->height[0] && t->height[1] && t->height[2];
}
int mf_decode(const unsigned char *data,unsigned size,unsigned head,const MfTables *t,
              unsigned k,unsigned row,unsigned col,MfBox *out) {
    unsigned n=head==15?13:26,j,best_class=0,best_class_q;float x,y,w,h,e[4],score,class_score;
    if(!data || !out || !valid_tables(t) || (head!=15 && head!=22) || size!=n*n*24 || k>=3 || row>=n || col>=n)return 0;
    /* Compare the raw quantized logits, not float sigmoid results. Sigmoid is
       monotonic, but very positive logits can all round to exactly 1.0f and
       incorrectly tie as class zero. */
    best_class_q=at(data,n,k,row,col,5);
    for(j=1;j<MF_CLASSES;j++){
        unsigned candidate_q=at(data,n,k,row,col,5+j);
        if(candidate_q>best_class_q){best_class_q=candidate_q;best_class=j;}
    }
    class_score=t->sig[best_class_q];
    score=t->sig[at(data,n,k,row,col,4)]*class_score;
    if(!(score>=0 && score<=1))return 0;
    x=(t->sig[at(data,n,k,row,col,0)]+col)/n;
    y=(t->sig[at(data,n,k,row,col,1)]+row)/n;
    w=t->width[k][at(data,n,k,row,col,2)];h=t->height[k][at(data,n,k,row,col,3)];
    if(!(w>0 && h>0))return 0;
    e[0]=(x-w/2)*1248;e[1]=(y-h/2)*624;e[2]=(x+w/2)*1248;e[3]=(y+h/2)*624;
    for(j=0;j<4;j++)if(!(e[j]>=-2147483520.0f && e[j]<=2147483520.0f))return 0;
    out->x1=max_i(0,(int)e[0]);out->y1=max_i(0,(int)e[1]);
    out->x2=min_i(1247,(int)e[2]);out->y2=min_i(623,(int)e[3]);
    if(out->x2<=out->x1 || out->y2<=out->y1)return 0;
    out->score=score;out->class_id=best_class;out->head=head;out->anchor=k;out->row=row;out->col=col;out->suppressed=0;
    return 1;
}
int mf_stain_duplicate(const MfBox *a,const MfBox *b) {
    unsigned long long aa,ba,in;int w,h;
    if(!a || !b || a->class_id!=2U || b->class_id!=2U || !valid_box(a) || !valid_box(b))return 0;
    aa=(unsigned long long)(a->x2-a->x1)*(a->y2-a->y1);
    ba=(unsigned long long)(b->x2-b->x1)*(b->y2-b->y1);
    w=max_i(0,min_i(a->x2,b->x2)-max_i(a->x1,b->x1));
    h=max_i(0,min_i(a->y2,b->y2)-max_i(a->y1,b->y1));
    in=(unsigned long long)w*h;
    return in*1000U>=(aa<ba?aa:ba)*MF_STAIN_CONTAINMENT_PERMILLE;
}
static int overlap(const MfBox *a,const MfBox *b,unsigned threshold) {
    int w=max_i(0,min_i(a->x2,b->x2)-max_i(a->x1,b->x1));
    int h=max_i(0,min_i(a->y2,b->y2)-max_i(a->y1,b->y1));
    unsigned long long intersection=(unsigned long long)w*h;
    unsigned long long area=(unsigned long long)(a->x2-a->x1)*(a->y2-a->y1)+(unsigned long long)(b->x2-b->x1)*(b->y2-b->y1)-intersection;
    return intersection*1000>area*threshold;
}
unsigned mf_nms(MfBox *work,unsigned count,unsigned threshold,MfBox out[MF_MAX_FACES]) {
    unsigned n=0,i,best;
    if(!work || !out || count>MF_CANDIDATES || threshold>1000)return 0;
    /* Repeated maximum selection avoids full sorting and pre-NMS top-K losses.
     * Stable tie order: incoming head15 then head22, anchor,row,column order. */
    while(n<MF_MAX_FACES) {
        best=count;
        for(i=0;i<count;i++)if(!work[i].suppressed && valid_box(&work[i]) &&
            (best==count || work[i].score>work[best].score))best=i;
        if(best==count)break;
        out[n++]=work[best];work[best].suppressed=1;
        for(i=0;i<count;i++)if(!work[i].suppressed && valid_box(&work[i]) &&
            work[i].class_id==work[best].class_id && (overlap(&work[best],&work[i],threshold) || mf_stain_duplicate(&work[best],&work[i])))work[i].suppressed=1;
    }
    return n;
}
int mf_detect(const unsigned char *h15,unsigned size15,const unsigned char *h22,unsigned size22,
              const MfTables tables[2],float threshold,unsigned nms_permille,
              MfBox *work,unsigned capacity,MfBox out[MF_MAX_FACES],unsigned *candidates) {
    unsigned hh,k,r,c,n,count=0;const unsigned char *p;
    if(candidates)*candidates=0;
    if(!h15 || !h22 || !tables || !work || !out || !candidates || size15!=4056 || size22!=16224 ||
       !(threshold>=0 && threshold<=1) || nms_permille>1000 || !valid_tables(&tables[0]) || !valid_tables(&tables[1]))return -1;
    if(capacity<MF_CANDIDATES)return -2;
    for(hh=0;hh<2;hh++) {
        n=hh?26:13;p=hh?h22:h15;
        for(k=0;k<3;k++)for(r=0;r<n;r++)for(c=0;c<n;c++) {
            MfBox decoded;
            if(mf_decode(p,n*n*24,hh?22:15,&tables[hh],k,r,c,&decoded) && decoded.score>=threshold)
                work[count++]=decoded;
        }
    }
    *candidates=count;return (int)mf_nms(work,count,nms_permille,out);
}
