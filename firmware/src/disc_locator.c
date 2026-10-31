#include "disc_locator.h"
#include "disc_origin_reference.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifdef DISC_LOCATOR_TRACE
#include <stdio.h>
#define TRACE(...) fprintf(stderr,__VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif
#define W 320
#define H 180
#define N (W*H)
#define R 128
#define PAD 32
#define EXT (R+2*PAD)
#define RN (EXT*EXT)
typedef struct { double x,y; } Pt;
static uint8_t green[N], seen[N], roi[RN], roi_seen[RN];
static uint16_t queue[N];
static Pt points[N], hull[N*2];
static int green_floor;
static Pt calibrated_quad[4];
static DiscConfig calibrated_config;
static unsigned calibration_valid;
static unsigned partial_border_seen;
static unsigned border_disc_seen;
unsigned disc_locator_used_cache;
unsigned disc_locator_pose_refit_used;
unsigned disc_locator_outline_recovered;
void disc_locator_reset_calibration(void)
{calibration_valid=0;disc_locator_used_cache=0;disc_locator_pose_refit_used=0;disc_locator_outline_recovered=0;}
unsigned disc_locator_calibrated(void) {return calibration_valid;}
/* User confirmed 100x100 mm measured at the black tape's inner edges. */
const DiscConfig disc_default_config={100000,100000,1};
static int green_chroma(const uint8_t *p)
{
    int b=p[0],g=p[1],r=p[2];
    return g>=28 && g-r>=12 && 100*g>=116*r && 100*g>=108*b;
}
static int is_green(const uint8_t *p)
{return green_chroma(p) && p[1]>=green_floor;}
/* Red-frame geometry is independent of the table's green brightness.
 * BGR888 integer chroma gates reject green table, neutral metal and shadows.
 * A candidate still needs a closed frame, four native edge fits and pose
 * validation; red pixels alone never create a target or authorize motion. */
static int is_red_tape(const uint8_t *p)
{
    int b=p[0],g=p[1],r=p[2];
    return r>=55 && r-g>=25 && r-b>=25 &&
           100*r>=130*g && 100*r>=130*b;
}
static double cross(Pt a,Pt b,Pt c)
{ return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
static double distance(Pt a,Pt b)
{ double x=a.x-b.x,y=a.y-b.y; return sqrt(x*x+y*y); }
static int compare_pt(const void *va,const void *vb)
{
    const Pt *a=(const Pt *)va,*b=(const Pt *)vb;
    if(a->x!=b->x) return a->x<b->x?-1:1;
    return (a->y>b->y)-(a->y<b->y);
}
static double polygon_area(const Pt *p,unsigned n)
{
    unsigned i; double sum=0;
    for(i=0;i<n;i++) sum+=p[i].x*p[(i+1)%n].y-p[(i+1)%n].x*p[i].y;
    return fabs(sum)*0.5;
}
static unsigned make_hull(unsigned n)
{
    unsigned i,k=0,t;
    qsort(points,n,sizeof(Pt),compare_pt);
    for(i=0;i<n;i++) {
        while(k>=2 && cross(hull[k-2],hull[k-1],points[i])<=0) k--;
        hull[k++]=points[i];
    }
    t=k+1;
    for(i=n-1;i>0;i--) {
        while(k>=t && cross(hull[k-2],hull[k-1],points[i-1])<=0) k--;
        hull[k++]=points[i-1];
    }
    return k>1?k-1:0;
}
static int quad_from_component(unsigned count,Pt q[4])
{
    unsigned i,n,boundary=0,k; double before,after;
    for(i=0;i<count;i++) {
        unsigned v=queue[i];
        if(!green[v-1] || !green[v+1] || !green[v-W] || !green[v+W]) {
            points[boundary].x=v%W; points[boundary++].y=v/W;
        }
    }
    if(boundary<16) return 0;
    n=make_hull(boundary); if(n<4) return 0;
    before=polygon_area(hull,n);
    while(n>4) {
        double best=1e30; k=0;
        for(i=0;i<n;i++) {
            double d=fabs(cross(hull[(i+n-1)%n],hull[i],hull[(i+1)%n]));
            if(d<best) {best=d;k=i;}
        }
        memmove(hull+k,hull+k+1,(n-k-1)*sizeof(Pt)); n--;
    }
    after=polygon_area(hull,4);
    if(after<700 || after<0.90*before) {TRACE("quad area ratio %.4f (%f)\n",after/before,after);return 0;}
    for(i=0;i<4;i++) {
        Pt a=hull[i],b=hull[(i+1)%4],c=hull[(i+2)%4];
        double la=distance(a,b),lb=distance(b,c);
        double dot=(a.x-b.x)*(c.x-b.x)+(a.y-b.y)*(c.y-b.y);
        if(la<24 || fabs(dot/(la*lb))>0.78) {TRACE("quad angle %.3f len %.2f\n",dot/(la*lb),la);return 0;}
        q[i]=a;
    }
    return 1;
}
static const uint8_t *sample(const uint8_t *src,double x,double y)
{
    int ix=(int)floor(x*4+2.5),iy=(int)floor(y*4+2.5);
    if(ix<0 || ix>=1280 || iy<0 || iy>=720) return NULL;
    return src+((unsigned)iy*1280+(unsigned)ix)*3;
}
static int red_frame_min(const uint8_t *src,const Pt q[4],unsigned minimum)
{
    unsigned i,j;
    /* Hull TL->TR->BR->BL: exterior normal is (dy,-dx). */
    for(i=0;i<4;i++) {
        Pt a=q[i],b=q[(i+1)%4]; double len=distance(a,b); unsigned hits=0;
        for(j=2;j<30;j++) {
            double t=j/32.0,offset; int dark=0;
            for(offset=1.5;offset<=4.5;offset+=1.5) {
                const uint8_t *p=sample(src,a.x+(b.x-a.x)*t+(b.y-a.y)*offset/len,
                    a.y+(b.y-a.y)*t-(b.x-a.x)*offset/len);
                if(p && is_red_tape(p)) dark=1;
            }
            hits+=(unsigned)dark;
        }
        if(hits<minimum) {TRACE("red side %u hits %u\n",i,hits);return 0;}
    }
    return 1;
}
/* Fast path: a cached mapping is reused only when ALL
 * four currently visible edge fragments still coincide with that mapping.
 * At least half of every side and 80% of all probes must be supported by
 * both a non-red-to-red transition and red tape outside. Search is only
 * +/-2 native pixels. EDG2 refits current visible edges if this fast match
 * fails; the unchanged cached coordinates alone never authorize a target. */
static int cached_pose_visible(const uint8_t *src)
{
    unsigned i,j,total=0;
    for(i=0;i<4;i++) {
        Pt a=calibrated_quad[i],b=calibrated_quad[(i+1)%4];
        double len=distance(a,b),nx=(b.y-a.y)/len,ny=-(b.x-a.x)/len;
        double residual=0;unsigned hits=0;
        for(j=5;j<60;j++) {
            double t=j/64.0,bx=a.x+(b.x-a.x)*t,by=a.y+(b.y-a.y)*t;
            double d,best=1e9,offset=0;int found=0,dark=0;
            for(d=-0.5;d<=0.5;d+=0.25) {
                const uint8_t *p=sample(src,bx+nx*d,by+ny*d);
                const uint8_t *r=sample(src,bx+nx*(d+0.25),by+ny*(d+0.25));
                const uint8_t *s=sample(src,bx+nx*(d+0.5),by+ny*(d+0.5));
                if(p && r && s && !is_red_tape(p) && is_red_tape(r) && is_red_tape(s) && fabs(d)<best) {
                    best=fabs(d);offset=d+0.125;found=1;
                }
            }
            for(d=1.5;d<=4.5;d+=1.5) {
                const uint8_t *p=sample(src,bx+nx*d,by+ny*d);
                if(p && is_red_tape(p))dark=1;
            }
            if(found && dark){hits++;residual+=fabs(offset);}
        }
        TRACE("cache side %u supported %u/55 residual %.3f\n",i,hits,hits?residual/hits:999.0);
        if(hits<28 || residual/hits>0.35)return 0;
        total+=hits;
    }
    return total>=176;
}
/* Fit a CURRENT red inner edge when the held camera has a small return-pose
 * offset. Old calibrated coordinates only bound the search; they are never
 * used as the final mapping after a failed pixel-alignment check. */
static int fit_visible_edge(const uint8_t *src,Pt a,Pt b,double line[3],unsigned *support)
{
    Pt p[64];double position[64],best_line[3]={0,0,0},best_error=1e30;
    double len=distance(a,b),nx,ny;unsigned i,j,n=0,best_count=0,pass;
    if(len<20)return 0;
    nx=(b.y-a.y)/len;ny=-(b.x-a.x)/len;
    for(j=5;j<60;j++) {
        double t=j/64.0,bx=a.x+(b.x-a.x)*t,by=a.y+(b.y-a.y)*t;
        double d,best=1e30,selected=0;int found=0;
        for(d=-24.0;d<=24.0;d+=0.25) {
            const uint8_t *u=sample(src,bx+nx*d,by+ny*d);
            const uint8_t *v=sample(src,bx+nx*(d+0.25),by+ny*(d+0.25));
            const uint8_t *w=sample(src,bx+nx*(d+0.50),by+ny*(d+0.50));
            const uint8_t *outside=sample(src,bx+nx*(d+1.5),by+ny*(d+1.5));
            if(u && v && w && outside && !is_red_tape(u) && is_red_tape(v) &&
               is_red_tape(w) && is_red_tape(outside) && fabs(d)<best) {
                best=fabs(d);selected=d+0.125;found=1;
            }
        }
        if(found) {p[n].x=bx+nx*selected;p[n].y=by+ny*selected;position[n]=t;n++;}
    }
    if(n<28)return 0;
    /* Deterministic wide-baseline line consensus rejects the curved metal
     * boundary in an occluded segment before total-least-squares fitting. */
    for(i=0;i<n;i+=3) for(j=i+1;j<n;j+=3) {
        double dx=p[j].x-p[i].x,dy=p[j].y-p[i].y,l=sqrt(dx*dx+dy*dy);
        double a0,a1,c,error=0;unsigned k,hits=0;
        if(l<len*0.35)continue;
        a0=-dy/l;a1=dx/l;c=a0*p[i].x+a1*p[i].y;
        for(k=0;k<n;k++) {
            double residual=fabs(a0*p[k].x+a1*p[k].y-c);
            if(residual<=0.5){hits++;error+=residual;}
        }
        if(hits>best_count || (hits==best_count && error<best_error)) {
            best_count=hits;best_error=error;best_line[0]=a0;best_line[1]=a1;best_line[2]=c;
        }
    }
    if(best_count<28)return 0;
    memcpy(line,best_line,sizeof(best_line));
    for(pass=0;pass<3;pass++) {
        double sx=0,sy=0,sxx=0,syy=0,sxy=0,cx,cy,vx,vy,cov,theta;unsigned hits=0;
        for(i=0;i<n;i++) if(fabs(line[0]*p[i].x+line[1]*p[i].y-line[2])<=0.5) {
            sx+=p[i].x;sy+=p[i].y;sxx+=p[i].x*p[i].x;
            syy+=p[i].y*p[i].y;sxy+=p[i].x*p[i].y;hits++;
        }
        if(hits<28)return 0;
        cx=sx/hits;cy=sy/hits;vx=sxx/hits-cx*cx;vy=syy/hits-cy*cy;cov=sxy/hits-cx*cy;
        theta=0.5*atan2(2*cov,vx-vy);
        line[0]=-sin(theta);line[1]=cos(theta);line[2]=line[0]*cx+line[1]*cy;
    }
    {
        double error=0,first=1,last=0;unsigned hits=0;
        for(i=0;i<n;i++) {
            double residual=fabs(line[0]*p[i].x+line[1]*p[i].y-line[2]);
            if(residual<=0.375) {
                hits++;error+=residual;
                if(position[i]<first)first=position[i];
                if(position[i]>last)last=position[i];
            }
        }
        TRACE("visible-edge support=%u/55 span=%.3f residual=%.3f\n",hits,last-first,hits?error/hits:999.0);
        if(hits<28 || last-first<0.55 || error/hits>0.30)return 0;
        *support=hits;
    }
    return 1;
}
static int current_cached_quad(const uint8_t *src,Pt q[4])
{
    double line[4][3];unsigned i,total=0;
    if(cached_pose_visible(src)) {memcpy(q,calibrated_quad,4*sizeof(Pt));return 1;}
    for(i=0;i<4;i++) {
        unsigned support=0;
        if(!fit_visible_edge(src,calibrated_quad[i],calibrated_quad[(i+1)%4],line[i],&support))return 0;
        total+=support;
    }
    if(total<176)return 0;
    for(i=0;i<4;i++) {
        const double *a=line[(i+3)%4],*b=line[i];double det=a[0]*b[1]-a[1]*b[0];
        if(fabs(det)<0.1)return 0;
        q[i].x=(a[2]*b[1]-a[1]*b[2])/det;
        q[i].y=(a[0]*b[2]-a[2]*b[0])/det;
        if(!isfinite(q[i].x) || !isfinite(q[i].y) || q[i].x<0 || q[i].x>=W || q[i].y<0 || q[i].y>=H)return 0;
    }
    if(!red_frame_min(src,q,18))return 0;
    disc_locator_pose_refit_used=1;
    return 1;
}
#if !DISC_FIXED_ORIGIN
static int inside(const Pt q[4],double x,double y)
{
    unsigned i; Pt p={x,y};
    for(i=0;i<4;i++) if(cross(q[i],q[(i+1)%4],p)<0) return 0;
    return 1;
}
#endif
/* Refit each inner edge against native-resolution non-red-to-red transitions.
 * Small-grid hull corners alone introduce several pixels of scale error. */
static int refine_quad(const uint8_t *src,Pt q[4])
{
    double line[4][3]; Pt refined[4]; unsigned i,j;
    for(i=0;i<4;i++) {
        Pt a=q[i],b=q[(i+1)%4]; double len=distance(a,b);
        double nx=(b.y-a.y)/len,ny=-(b.x-a.x)/len;
        double sx=0,sy=0,sxx=0,syy=0,sxy=0; unsigned count=0;
        Pt samples[64];
        for(j=5;j<60;j++) {
            double t=j/64.0,bx=a.x+(b.x-a.x)*t,by=a.y+(b.y-a.y)*t;
            double d,best=1e9,selected=0; int found=0;
            for(d=-2;d<=3;d+=0.25) {
                const uint8_t *p=sample(src,bx+nx*d,by+ny*d);
                const uint8_t *r=sample(src,bx+nx*(d+0.25),by+ny*(d+0.25));
                const uint8_t *s=sample(src,bx+nx*(d+0.50),by+ny*(d+0.50));
                if(p && r && s && !is_red_tape(p) && is_red_tape(r) && is_red_tape(s) && fabs(d)<best) {
                    best=fabs(d);selected=d+0.125;found=1;
                }
            }
            if(found) {
                double x=bx+nx*selected,y=by+ny*selected;
                samples[count].x=x;samples[count].y=y;
                sx+=x;sy+=y;sxx+=x*x;syy+=y*y;sxy+=x*y;count++;
            }
        }
        if(count<25) {TRACE("edge %u: only %u transitions\n",i,count);return 0;}
        {
            unsigned pass,n=count;
            for(pass=0;pass<3;pass++) {
            double cx=sx/count,cy=sy/count;
            double vx=sxx/count-cx*cx,vy=syy/count-cy*cy,cov=sxy/count-cx*cy;
            double noise=0.5*(vx+vy-sqrt((vx-vy)*(vx-vy)+4*cov*cov));
            double theta=0.5*atan2(2*cov,vx-vy);
            line[i][0]=-sin(theta);line[i][1]=cos(theta);
            line[i][2]=line[i][0]*cx+line[i][1]*cy;
            if(noise<=1.0 || pass==2) {
                if(noise>1.0) {TRACE("edge %u noise %.3f\n",i,noise);return 0;}
                break;
            }
            sx=sy=sxx=syy=sxy=0;count=0;
            for(j=0;j<n;j++) {
                double x=samples[j].x,y=samples[j].y;
                if(fabs(line[i][0]*x+line[i][1]*y-line[i][2])>1.0) continue;
                sx+=x;sy+=y;sxx+=x*x;syy+=y*y;sxy+=x*y;count++;
            }
            if(count<25) return 0;
            }
        }
    }
    for(i=0;i<4;i++) {
        const double *a=line[(i+3)%4],*b=line[i];
        double det=a[0]*b[1]-a[1]*b[0];
        if(fabs(det)<0.1) return 0;
        refined[i].x=(a[2]*b[1]-a[1]*b[2])/det;
        refined[i].y=(a[0]*b[2]-a[2]*b[0])/det;
        if(distance(refined[i],q[i])>8) {TRACE("corner %u shift %.2f: (%.2f,%.2f)->(%.2f,%.2f)\n",i,distance(refined[i],q[i]),q[i].x,q[i].y,refined[i].x,refined[i].y);return 0;}
    }
    memcpy(q,refined,sizeof(refined)); return 1;
}
#if DISC_FIXED_ORIGIN
static int fixed_origin_index(const Pt q[4])
{
    static const Pt ref[4]=DISC_ORIGIN_REFERENCE_CORNERS;
    Pt native[4];unsigned i,k;int selected=-1;
    for(i=0;i<4;i++) {
        native[i].x=q[i].x*4+2;native[i].y=q[i].y*4+2;
        if(!isfinite(native[i].x) || !isfinite(native[i].y))return -1;
    }
    /* Try every cyclic index; hull starting corner is not a stable identity.
     * Exactly one full corner correspondence must satisfy ALL pose gates. */
    for(i=0;i<4;i++) {
        unsigned good=1;double dx=0,dy=0;
        for(k=0;k<4;k++) {
            Pt a=native[(i+4-k)%4];
            dx+=(a.x-ref[k].x)/4;dy+=(a.y-ref[k].y)/4;
        }
        /* A coherent image translation does not change the physical table
         * coordinates: the current four corners define a fresh homography.
         * Limit that translation and independently bound shape distortion. */
        if(sqrt(dx*dx+dy*dy)>DISC_ORIGIN_MAX_TRANSLATION_PX+1e-6)continue;
        for(k=0;k<4;k++) {
            Pt a=native[(i+4-k)%4],b=native[(i+3-k)%4];
            Pt ra=ref[k],rb=ref[(k+1)%4];
            double len=distance(a,b),rlen=distance(ra,rb);
            double direction=(b.x-a.x)*(rb.x-ra.x)+(b.y-a.y)*(rb.y-ra.y);
            double residual_x=a.x-ra.x-dx,residual_y=a.y-ra.y-dy;
            if(sqrt(residual_x*residual_x+residual_y*residual_y)>DISC_ORIGIN_MAX_SHAPE_RESIDUAL_PX+1e-6 ||
               len<rlen*DISC_ORIGIN_MIN_EDGE_RATIO || len>rlen*DISC_ORIGIN_MAX_EDGE_RATIO ||
               direction<DISC_ORIGIN_MIN_DIRECTION_COS*len*rlen)good=0;
        }
        if(good) {if(selected>=0)return -1;selected=(int)i;}
    }
    return selected;
}
#else
static int origin_marker(const uint8_t *src,const Pt q[4],uint32_t *pixels)
{
    unsigned scores[4]={0},i,best=0,second=0,index=0;
    int white_floor=green_floor*5/4;
    if(white_floor<70) white_floor=70;
    for(i=0;i<4;i++) {
        int x,y;
        double radius=fmin(20.0,0.22*fmin(distance(q[i],q[(i+1)%4]),distance(q[i],q[(i+3)%4])));
        for(y=(int)(q[i].y-radius);y<=q[i].y+radius;y++)
        for(x=(int)(q[i].x-radius);x<=q[i].x+radius;x++) {
            const uint8_t *p; int hi,lo;
            if(inside(q,x,y) || hypot(x-q[i].x,y-q[i].y)>radius) continue;
            p=sample(src,x,y); if(!p) continue;
            hi=p[0]>p[1]?p[0]:p[1]; if(p[2]>hi) hi=p[2];
            lo=p[0]<p[1]?p[0]:p[1]; if(p[2]<lo) lo=p[2];
            if(lo>=white_floor && hi-lo<65) scores[i]++;
        }
        if(scores[i]>best) {second=best;best=scores[i];index=i;}
        else if(scores[i]>second) second=scores[i];
    }
    *pixels=best;
    if(best<3 || best<second*2+2) return -1;
    return (int)index;
}
#endif
/* Eight coefficients mapping normalized rectangle -> image. */
static int homography(const Pt q[4],double h[9])
{
    static const double uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    double a[8][9]={{0}}; unsigned i,j,k;
    for(i=0;i<4;i++) {
        double u=uv[i][0],v=uv[i][1];
        a[2*i][0]=u; a[2*i][1]=v; a[2*i][2]=1;
        a[2*i][6]=-u*q[i].x; a[2*i][7]=-v*q[i].x; a[2*i][8]=q[i].x;
        a[2*i+1][3]=u; a[2*i+1][4]=v; a[2*i+1][5]=1;
        a[2*i+1][6]=-u*q[i].y; a[2*i+1][7]=-v*q[i].y; a[2*i+1][8]=q[i].y;
    }
    for(i=0;i<8;i++) {
        unsigned pivot=i; double d;
        for(j=i+1;j<8;j++) if(fabs(a[j][i])>fabs(a[pivot][i])) pivot=j;
        if(fabs(a[pivot][i])<1e-9) return 0;
        for(k=0;k<9;k++) {d=a[i][k];a[i][k]=a[pivot][k];a[pivot][k]=d;}
        d=a[i][i]; for(k=i;k<9;k++) a[i][k]/=d;
        for(j=0;j<8;j++) if(j!=i) {
            d=a[j][i]; for(k=i;k<9;k++) a[j][k]-=d*a[i][k];
        }
    }
    for(i=0;i<8;i++) h[i]=a[i][8];
    h[8]=1; return 1;
}
static Pt project(const double h[9],double x,double y)
{
    double d=h[6]*x+h[7]*y+1;
    Pt p={(h[0]*x+h[1]*y+h[2])/d,(h[3]*x+h[4]*y+h[5])/d}; return p;
}
/* A stain at the rim can open a background-colour channel into an otherwise
 * circular bright silhouette. Recover only a small concavity supported by a
 * dense measured outer arc, rather than broadly relaxing the ellipse test. */
static int recover_disc_outline(unsigned count,unsigned side,double *cx,double *cy,
                                double *vx,double *vy,double *cov)
{
    unsigned i,n,boundary=0,supported=0,angle_mask=0,angle_count=0;
    double a2=0,sx=0,sy=0,sxx=0,syy=0,sxy=0,area,coverage,det,error=0;
    double x0,y0,ex2,ey2,exy,vx0,vy0,cov0;
    for(i=0;i<count;i++) {
        unsigned v=queue[i];
        if(!roi[v-1] || !roi[v+1] || !roi[v-side] || !roi[v+side]) {
            points[boundary].x=v%side;points[boundary].y=v/side;boundary++;
        }
    }
    n=make_hull(boundary);
    if(n<10)return 0;
    for(i=0;i<n;i++) {
        Pt a=hull[i],b=hull[(i+1)%n];double c=a.x*b.y-b.x*a.y;
        a2+=c;sx+=(a.x+b.x)*c;sy+=(a.y+b.y)*c;
        sxx+=(a.x*a.x+a.x*b.x+b.x*b.x)*c;
        syy+=(a.y*a.y+a.y*b.y+b.y*b.y)*c;
        sxy+=(2*a.x*a.y+a.x*b.y+b.x*a.y+2*b.x*b.y)*c;
    }
    area=fabs(a2)*0.5;
    if(area<50)return 0;
    coverage=count/area;
    if(coverage<0.80 || coverage>0.98)return 0;
    x0=sx/(3*a2);y0=sy/(3*a2);
    ex2=sxx/(6*a2);ey2=syy/(6*a2);exy=sxy/(12*a2);
    vx0=ex2-x0*x0;vy0=ey2-y0*y0;cov0=exy-x0*y0;
    det=vx0*vy0-cov0*cov0;
    if(det<=1 || det/((vx0+vy0)*(vx0+vy0))<0.18)return 0;
    if(area/(12.566370614359*sqrt(det))<0.93 || area/(12.566370614359*sqrt(det))>1.07)return 0;
    for(i=0;i<n;i++) {
        unsigned j;Pt a=hull[i],b=hull[(i+1)%n];
        for(j=0;j<2;j++) {
            double dx=(j?(a.x+b.x)*0.5:a.x)-x0,dy=(j?(a.y+b.y)*0.5:a.y)-y0;
            error+=fabs((vy0*dx*dx-2*cov0*dx*dy+vx0*dy*dy)/(4*det)-1);
        }
    }
    error/=(2*n);
    if(error>0.12)return 0;
    for(i=0;i<count;i++) {
        unsigned v=queue[i];
        if(!roi[v-1] || !roi[v+1] || !roi[v-side] || !roi[v+side]) {
            double dx=v%side-x0,dy=v/side-y0;
            if(fabs((vy0*dx*dx-2*cov0*dx*dy+vx0*dy*dy)/(4*det)-1)<=0.20) {
                unsigned bin=(unsigned)((atan2(dy,dx)+3.141592653589793)*(32.0/6.283185307179586));
                if(bin>31)bin=31;
                angle_mask|=1U<<bin;supported++;
            }
        }
    }
    for(i=0;i<32;i++)angle_count+=(angle_mask>>i)&1U;
    TRACE("outline hull coverage=%.3f shape_error=%.3f arc=%u/%u angular_bins=%u/32\n",coverage,error,supported,boundary,angle_count);
    /* Angular support is not biased by a long jagged stain boundary. */
    if(angle_count<24 || supported*100U<boundary*50U ||
       hypot(x0-*cx,y0-*cy)>0.30*sqrt(vx0+vy0))return 0;
    *cx=x0;*cy=y0;*vx=vx0;*vy=vy0;*cov=cov0;
    return 1;
}
static void locate_pass(const uint8_t *src,const DiscConfig *cfg,DiscResult *out,int robust_mask,int use_cache)
{
    unsigned i,x,y,frame_count=0; Pt quad[4]={{0,0}},ordered[4]; double h[9]; int origin;
    unsigned side=use_cache?EXT:R,offset=use_cache?PAD:0,roi_size=side*side;
    memset(out,0,sizeof(*out)); out->status=LOC_NO_FRAME; memset(seen,0,sizeof(seen));
    /* Green is used only for foreground separation inside the validated red
       frame. Keep its absolute noise floor, not a global exposure estimate. */
    green_floor=28;
    for(y=0;y<H;y++) for(x=0;x<W;x++)
        green[y*W+x]=(uint8_t)!is_red_tape(src+((y*4+2)*1280+x*4+2)*3);
    /* Colour noise can bridge the red tape with a one-pixel non-red chain.
       A 3x3 opening removes such chains before connected-component search. */
    if(robust_mask) {
    for(y=1;y<H-1;y++) for(x=1;x<W-1;x++) {
        unsigned v=y*W+x;
        seen[v]=(uint8_t)(green[v-W-1] && green[v-W] && green[v-W+1] &&
            green[v-1] && green[v] && green[v+1] &&
            green[v+W-1] && green[v+W] && green[v+W+1]);
    }
    memset(green,0,sizeof(green));
    for(y=1;y<H-1;y++) for(x=1;x<W-1;x++) {
        unsigned v=y*W+x;
        green[v]=(uint8_t)(seen[v-W-1] || seen[v-W] || seen[v-W+1] ||
            seen[v-1] || seen[v] || seen[v+1] ||
            seen[v+W-1] || seen[v+W] || seen[v+W+1]);
    }
    memset(seen,0,sizeof(seen));
    }
    if(use_cache) {
        if(!current_cached_quad(src,quad)) {disc_locator_reset_calibration();out->status=LOC_REFERENCE_MISMATCH;return;}
        frame_count=1;
        disc_locator_used_cache=1;
    } else for(i=0;i<N;i++) {
        unsigned head=0,tail=0,touch=0; Pt candidate[4];
        if(seen[i] || !green[i]) continue;
        seen[i]=1;queue[tail++]=(uint16_t)i;
        while(head<tail) {
            unsigned v=queue[head++],xx=v%W,yy=v/W,n[4],nn=0,j;
            if(!xx || !yy || xx==W-1 || yy==H-1) touch=1;
            if(xx) n[nn++]=v-1;
            if(xx<W-1) n[nn++]=v+1;
            if(yy) n[nn++]=v-W;
            if(yy<H-1) n[nn++]=v+W;
            for(j=0;j<nn;j++) if(!seen[n[j]] && green[n[j]]) {
                seen[n[j]]=1;queue[tail++]=(uint16_t)n[j];
            }
        }
        if(touch || tail<500) continue;
        if(!quad_from_component(tail,candidate)) {TRACE("quad rejected area %u\n",tail);continue;}
        if(!refine_quad(src,candidate)) {TRACE("edge fit rejected area %u\n",tail);continue;}
        if(!red_frame_min(src,candidate,18)) {TRACE("frame rejected area %u\n",tail);continue;}
        memcpy(quad,candidate,sizeof(quad)); frame_count++;
    }
    out->frame_candidates=frame_count;
    TRACE("floor=%d frame=%u quad %.8f %.8f / %.8f %.8f / %.8f %.8f / %.8f %.8f\n",green_floor,frame_count,quad[0].x,quad[0].y,quad[1].x,quad[1].y,quad[2].x,quad[2].y,quad[3].x,quad[3].y);
    if(frame_count!=1) {if(frame_count>1) out->status=LOC_MULTI_FRAME; return;}
    out->frame_valid=1;
#if DISC_FIXED_ORIGIN
    origin=fixed_origin_index(quad);
    if(origin<0) {out->status=LOC_REFERENCE_MISMATCH;return;}
#else
    origin=origin_marker(src,quad,&out->marker_pixels);
    if(origin<0) {out->status=LOC_NO_ORIGIN;return;}
#endif
    out->origin_valid=1;
#if DISC_FIXED_ORIGIN
    if(use_cache && disc_locator_pose_refit_used)memcpy(calibrated_quad,quad,sizeof(quad));
    if(!use_cache)partial_border_seen=!red_frame_min(src,quad,26);
    if(!use_cache && !partial_border_seen && cfg->dimensions_confirmed && cfg->width_um && cfg->height_um) {
        memcpy(calibrated_quad,quad,sizeof(quad));calibrated_config=*cfg;calibration_valid=1;
    }
    if(!use_cache && partial_border_seen) {out->status=LOC_BAD_GEOMETRY;return;}
#endif
    /* X=previous corner, Y=next. Native image must not be mirrored. */
    for(i=0;i<4;i++) {
        ordered[i]=quad[((unsigned)origin+4-i)%4];
        out->corners_millipixel[2*i]=(int32_t)((ordered[i].x*4+2)*1000+0.5);
        out->corners_millipixel[2*i+1]=(int32_t)((ordered[i].y*4+2)*1000+0.5);
    }
    if(!homography(ordered,h)) {out->status=LOC_BAD_GEOMETRY;return;}
    memset(roi_seen,0,sizeof(roi_seen));
    for(y=0;y<side;y++) for(x=0;x<side;x++) {
        Pt p=project(h,((double)x+.5-offset)/R,((double)y+.5-offset)/R);
        const uint8_t *bgr=sample(src,p.x,p.y);
        if(!bgr) {out->status=LOC_BAD_GEOMETRY;return;}
        /* Red tape is background, never part of a metallic silhouette. In a
         * padded cache path, also require bright metal. Enclosed defect holes
         * are filled below. Dark/low-contrast discs may safely reject. */
        roi[y*side+x]=(uint8_t)(!is_green(bgr) && !is_red_tape(bgr) && (!use_cache ||
            (29*bgr[0]+150*bgr[1]+77*bgr[2])/256>=110));
    }
    /* Disc stains/reflections may be classified as green. Fill only enclosed
       background holes, so they cannot shift the outer-silhouette centre. */
    {
        unsigned head=0,tail=0;
        for(i=0;i<roi_size;i++) if((i<side || i>=roi_size-side || i%side==0 || i%side==side-1) && !roi[i]) {
            roi_seen[i]=1;queue[tail++]=(uint16_t)i;
        }
        while(head<tail) {
            unsigned v=queue[head++],xx=v%side,yy=v/side,n[4],nn=0,j;
            if(xx) n[nn++]=v-1;
            if(xx<side-1) n[nn++]=v+1;
            if(yy) n[nn++]=v-side;
            if(yy<side-1) n[nn++]=v+side;
            for(j=0;j<nn;j++) if(!roi[n[j]] && !roi_seen[n[j]]) {
                roi_seen[n[j]]=1;queue[tail++]=(uint16_t)n[j];
            }
        }
        for(i=0;i<roi_size;i++) if(!roi[i] && !roi_seen[i]) roi[i]=1;
    }
    memset(roi_seen,0,sizeof(roi_seen));
    for(i=0;i<roi_size;i++) {
        unsigned head=0,tail=0,touch=0,k,boundary=0,recovered=0;
        double sx=0,sy=0,sxx=0,syy=0,sxy=0,cx,cy,vx,vy,cov,det,err=0,fill;
        if(roi_seen[i] || !roi[i]) continue;
        queue[tail++]=(uint16_t)i;roi_seen[i]=1;
        while(head<tail) {
            unsigned v=queue[head++],xx=v%side,yy=v/side,n[4],nn=0,j;
            sx+=xx;sy+=yy;sxx+=xx*xx;syy+=yy*yy;sxy+=xx*yy;
            if(xx<3 || yy<3 || xx>=side-3 || yy>=side-3) touch=1;
            if(xx) n[nn++]=v-1;
            if(xx<side-1) n[nn++]=v+1;
            if(yy) n[nn++]=v-side;
            if(yy<side-1) n[nn++]=v+side;
            for(j=0;j<nn;j++) if(!roi_seen[n[j]] && roi[n[j]]) {
                roi_seen[n[j]]=1;queue[tail++]=(uint16_t)n[j];
            }
        }
        /* A clipped component is not a target. Remember that a same-frame
         * padded check is needed once all four frame edges have calibrated.
         * Also check when an interior target exists: do not hide a second
         * disc at an edge and incorrectly report a unique target. */
        if(!use_cache && touch && tail>=60 && tail<=R*R*3/4)border_disc_seen=1;
        if(touch || tail<60 || tail>R*R*3/4) continue;
        cx=sx/tail;cy=sy/tail;vx=sxx/tail-cx*cx;vy=syy/tail-cy*cy;cov=sxy/tail-cx*cy;
        det=vx*vy-cov*cov;
        TRACE("disc area=%u center %.2f,%.2f detRatio %.4f\n",tail,cx,cy,det/((vx+vy)*(vx+vy)));
        if(det<=1 || det/((vx+vy)*(vx+vy))<0.18) continue;
        fill=tail/(12.566370614359*sqrt(det));
        TRACE("disc fill %.4f\n",fill);
        if(fill<0.88 || fill>1.10) {
            if(fill<0.75 || fill>1.10 || !recover_disc_outline(tail,side,&cx,&cy,&vx,&vy,&cov))continue;
            recovered=1;
        }
        for(k=0;!recovered && k<tail;k++) {
            unsigned v=queue[k];
            if(!roi[v-1] || !roi[v+1] || !roi[v-side] || !roi[v+side]) {
                double dx=v%side-cx,dy=v/side-cy;
                err+=fabs((vy*dx*dx-2*cov*dx*dy+vx*dy*dy)/(4*det)-1);boundary++;
            }
        }
        TRACE("disc error %.4f\n",boundary?err/boundary:999.0);
        if(!recovered && (!boundary || err/boundary>0.17)) {
            if(!recover_disc_outline(tail,side,&cx,&cy,&vx,&vy,&cov))continue;
            recovered=1;
        }
        if(recovered)disc_locator_outline_recovered=1;
        {
            double u=(cx+.5-offset)/R,v=(cy+.5-offset)/R; Pt p=project(h,u,v);
            /* Never command a centre outside the calibrated workspace. */
            if(u<0 || u>1 || v<0 || v>1) {out->status=LOC_BAD_GEOMETRY;out->valid=out->mm_valid=0;out->x_um=out->y_um=0;return;}
            out->candidates++;out->area_samples=tail;
            out->x_ppm=(uint32_t)(u*1000000+.5);out->y_ppm=(uint32_t)(v*1000000+.5);
            out->x_um=(int32_t)(u*cfg->width_um+.5);out->y_um=(int32_t)(v*cfg->height_um+.5);
            out->x_millipixel=(uint32_t)((p.x*4+2)*1000+.5);
            out->y_millipixel=(uint32_t)((p.y*4+2)*1000+.5);
        }
    }
    out->valid=(out->candidates==1);
    out->status=out->valid?LOC_OK:(out->candidates?LOC_MULTI_DISC:LOC_NO_DISC);
    out->mm_valid=out->valid && cfg->dimensions_confirmed && cfg->width_um && cfg->height_um;
    if(!out->valid) {
        out->x_um=out->y_um=0;out->x_ppm=out->y_ppm=0;
        out->x_millipixel=out->y_millipixel=out->area_samples=0;
    }
}
void disc_locate_config(const uint8_t *src,const DiscConfig *cfg,DiscResult *out)
{
    unsigned had_calibration=calibration_valid;
#if !DISC_FIXED_ORIGIN
    (void)had_calibration;
#endif
    disc_locator_used_cache=0;
    disc_locator_pose_refit_used=0;
    disc_locator_outline_recovered=0;
    partial_border_seen=0;
    border_disc_seen=0;
    if(calibration_valid && (cfg->width_um!=calibrated_config.width_um || cfg->height_um!=calibrated_config.height_um ||
        cfg->dimensions_confirmed!=calibrated_config.dimensions_confirmed)) {
        disc_locator_reset_calibration();had_calibration=0;
    }
    locate_pass(src,cfg,out,0,0);
    /* Retry only frame geometry, never select between ambiguous targets or
       bypass the configured origin policy/reference-pose gates. */
    if(out->status==LOC_NO_FRAME || out->status==LOC_BAD_GEOMETRY)
        locate_pass(src,cfg,out,1,0);
#if DISC_FIXED_ORIGIN
    if(out->status==LOC_MULTI_FRAME)disc_locator_reset_calibration();
    /* A fully verified frame in THIS input may establish the mapping for
     * its border retry. Never require a failed first user cycle to warm it.
     * The padded pass still verifies all four visible edge fragments,
     * rejects a truncated outer silhouette and checks centre range/uniqueness.
     * An already valid interior disc must also be rechecked if a separate
     * border component could be a second workpiece. */
    else if(calibration_valid &&
        ((had_calibration && (out->status==LOC_NO_FRAME ||
                              out->status==LOC_BAD_GEOMETRY || out->status==LOC_NO_DISC ||
                              out->status==LOC_REFERENCE_MISMATCH)) ||
         (border_disc_seen && out->frame_valid && out->origin_valid &&
          (out->status==LOC_OK || out->status==LOC_NO_DISC))))
        locate_pass(src,cfg,out,1,1);
    if(out->status==LOC_REFERENCE_MISMATCH)disc_locator_reset_calibration();
#endif
}
void disc_locate(const uint8_t *src,DiscResult *out)
{disc_locate_config(src,&disc_default_config,out);}
