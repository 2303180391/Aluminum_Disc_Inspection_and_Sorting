#include "../firmware/src/disc_locator.c"
#include <stdio.h>
static unsigned checks;
#define CHECK(c,n) do{checks++;if(!(c)){printf("fixed origin geometry failure %u\n",(unsigned)(n));return n;}}while(0)
static void corners(Pt q[4],unsigned shift,double dx,double dy,double scale,double degrees)
{
    static const Pt ref[4]=DISC_ORIGIN_REFERENCE_CORNERS;
    double cx=0,cy=0,a=degrees*3.141592653589793/180.0;unsigned i;
    for(i=0;i<4;i++){cx+=ref[i].x/4;cy+=ref[i].y/4;}
    for(i=0;i<4;i++){
        Pt p=ref[(7-i-shift)%4];double x=(p.x-cx)*scale,y=(p.y-cy)*scale;
        q[i].x=(cx+x*cos(a)-y*sin(a)+dx-2)/4;
        q[i].y=(cy+x*sin(a)+y*cos(a)+dy-2)/4;
    }
}
int main(void)
{
    Pt q[4];unsigned i;
    for(i=0;i<4;i++){corners(q,i,0,0,1,0);CHECK(fixed_origin_index(q)==(int)((3+4-i)%4),1);}
    corners(q,0,32,-20,1,0);CHECK(fixed_origin_index(q)==3,2);
    corners(q,0,48,0,1,0);CHECK(fixed_origin_index(q)==3,3);
    corners(q,0,48.01,0,1,0);CHECK(fixed_origin_index(q)<0,4);
    corners(q,0,0,0,1.09,0);CHECK(fixed_origin_index(q)==3,5);
    corners(q,0,0,0,1.11,0);CHECK(fixed_origin_index(q)<0,6);
    corners(q,0,0,0,0.89,0);CHECK(fixed_origin_index(q)<0,7);
    corners(q,0,0,0,1,7.5);CHECK(fixed_origin_index(q)==3,8);
    corners(q,0,0,0,1,8.5);CHECK(fixed_origin_index(q)<0,9);
    corners(q,0,0,0,1,0);q[0].x=NAN;CHECK(fixed_origin_index(q)<0,10);
    corners(q,0,0,0,1,0);q[2].y=INFINITY;CHECK(fixed_origin_index(q)<0,11);
    corners(q,0,0,0,1,0);q[1]=q[0];CHECK(fixed_origin_index(q)<0,12);
    corners(q,0,0,0,1,0);{Pt temp=q[1];q[1]=q[3];q[3]=temp;}CHECK(fixed_origin_index(q)<0,13);
    printf("FIXED_ORIGIN_GEOMETRY_PASS checks=%u\n",checks);return 0;
}
