#include "candidate_log.h"
#include "score_log.h"
#include "yolo_diag.h"
#include "sigmoid_exp_table.h"
#include "xil_io.h"
#include "xtime_l.h"
#include <math.h>
#define BASE (DIAG_BASE+0x1000U)
static volatile u32 *const header=(volatile u32 *)BASE;
static volatile u32 *const records=(volatile u32 *)(BASE+32U);
static int latest_box[4],latest_valid;
int candidate_geometry(int box[4]) {
    unsigned i;for(i=0;i<4;i++) box[i]=latest_box[i];return latest_valid;
}
static unsigned byte_at(const u8 *data,unsigned n,unsigned k,unsigned r,unsigned c,unsigned j) {
    unsigned ch=k*6U+j;
    return data[(ch/8U)*n*n*8U+(r*n+c)*8U+ch%8U];
}
static u32 ppm(float x) {return x>=0.0f && x<=1.0f?(u32)(x*1000000.0f):0xffffffffU;}
static int max_i(int a,int b) {return a>b?a:b;}
static int min_i(int a,int b) {return a<b?a:b;}
void candidate_log_init(void) {
    unsigned i;
    for(i=0;i<8U+32U*32U;i++) header[i]=0U;
    header[0]=0x43414E31U;header[1]=1U;header[2]=32U;
    latest_valid=0;
}
void candidate_log(float s15,float s22,unsigned k15,unsigned r15,unsigned c15,unsigned k22,unsigned r22,unsigned c22) {
    unsigned head=(s22>s15)?22U:15U,n=(head==22U)?26U:13U;
    unsigned k=(head==22U)?k22:k15,rr=(head==22U)?r22:r15,cc=(head==22U)?c22:c15;
    const u8 *data=(const u8 *)((head==22U)?0x03400000U:0x03300000U);
    float *sig=(head==22U)?l22_sigmoid_table:l15_sigmoid_table;
    float *ws[2][3]={{l15_w0_table,l15_w1_table,l15_w2_table},{l22_w0_table,l22_w1_table,l22_w2_table}};
    float *hs[2][3]={{l15_h0_table,l15_h1_table,l15_h2_table},{l22_h0_table,l22_h1_table,l22_h2_table}};
    unsigned q[6],j,hh,ak,ar,ac,bh=0,bk=0,br=0,bc=0;
    float best[2]={-1.0f,-1.0f},overall=-1.0f,x,y,w,h,e[4];
    int box[4]={0,0,0,0},prev[4];
    u32 age,flags=score_reference(prev,&age),iou=0,seq=header[3]+1U;
    u32 sw=Xil_In32(DIAG_BASE+0x540U),sh=Xil_In32(DIAG_BASE+0x544U);
    volatile u32 *out=records+((seq-1U)%32U)*32U;
    XTime now;
    for(j=0;j<6U;j++) q[j]=byte_at(data,n,k,rr,cc,j);
    x=(sig[q[0]]+cc)/n;y=(sig[q[1]]+rr)/n;
    w=ws[head==22U][k][q[2]];h=hs[head==22U][k][q[3]];
    if(sw>=1000U && sw<=2000U) w*=sw/1000.0f;
    if(sh>=1000U && sh<=2000U) h*=sh/1000.0f;
    e[0]=(x-w/2)*1248;e[1]=(y-h/2)*624;e[2]=(x+w/2)*1248;e[3]=(y+h/2)*624;
    /* Avoid undefined conversion for overflowing lookup-table values. Diagnostic
     * rejects invalid geometry; it does not change the production decoder. */
    for(j=0;j<4U;j++) if(!isfinite(e[j]) || e[j]<-2147483520.0f || e[j]>2147483520.0f) break;
    if(j==4U) {
        box[0]=max_i(0,(int)e[0]);box[1]=max_i(0,(int)e[1]);
        box[2]=min_i(1247,(int)e[2]);box[3]=min_i(623,(int)e[3]);
        if(box[2]>box[0] && box[3]>box[1]) flags|=1U;
    }
    latest_valid=(flags&1U)!=0;
    for(j=0;j<4U;j++) latest_box[j]=box[j];
    if((flags&5U)==5U) {
        int iw=max_i(0,min_i(box[2],prev[2])-max_i(box[0],prev[0]));
        int ih=max_i(0,min_i(box[3],prev[3])-max_i(box[1],prev[1]));
        u32 intersect=(u32)(iw*ih);
        u32 area=(u32)((box[2]-box[0])*(box[3]-box[1])+(prev[2]-prev[0])*(prev[3]-prev[1]))-intersect;
        if(area) iou=(u32)((u64)intersect*1000U/area);
    }
    /* Independent exhaustive decoded-probability ranking; observation only. */
    for(hh=0;hh<2;hh++) {
        unsigned nn=hh?26U:13U;
        const u8 *dd=(const u8 *)(hh?0x03400000U:0x03300000U);
        float *ss=hh?l22_sigmoid_table:l15_sigmoid_table;
        for(ak=0;ak<3;ak++) for(ar=0;ar<nn;ar++) for(ac=0;ac<nn;ac++) {
            float p=ss[byte_at(dd,nn,ak,ar,ac,4)]*ss[byte_at(dd,nn,ak,ar,ac,5)];
            if(p>=best[hh]) best[hh]=p;
            if(p>overall) {overall=p;bh=hh?22U:15U;bk=ak;br=ar;bc=ac;}
        }
    }
    XTime_GetTime(&now);
    out[31]=0U;__asm__ volatile("dsb sy" ::: "memory");
    out[0]=seq;out[1]=yolo_diag->frame;out[2]=head;out[3]=k;out[4]=rr;out[5]=cc;
    out[6]=q[4];out[7]=q[5];out[8]=ppm(head==22U?s22:s15);out[9]=flags;
    for(j=0;j<4U;j++) {out[10+j]=(u32)box[j];out[14+j]=(u32)prev[j];}
    out[18]=age;out[19]=iou;out[20]=ppm(best[0]);out[21]=ppm(best[1]);
    out[22]=bh;out[23]=bk;out[24]=br;out[25]=bc;
    out[26]=ppm(sig[q[4]]);out[27]=ppm(sig[q[5]]);out[28]=yolo_diag->input_reference;
    out[29]=q[0]|(q[1]<<8)|(q[2]<<16)|(q[3]<<24);out[30]=(u32)(now*1000ULL/COUNTS_PER_SECOND);
    __asm__ volatile("dsb sy" ::: "memory");out[31]=seq;
    __asm__ volatile("dsb sy" ::: "memory");header[3]=seq;
}
