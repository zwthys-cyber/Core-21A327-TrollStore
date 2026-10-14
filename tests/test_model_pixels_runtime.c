#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelPixels.h"
static unsigned Queries,Spans,StopAfter;
static bool Query(void *context,const CMVec3 *camera,const CMVec3 *world,bool *blocked) {
    (void)context;(void)camera;if(StopAfter&&Queries>=StopAfter)return false;Queries++;*blocked=world->y>=0;return true;
}
static void Span(void *context,int x,int y,int width,uint8_t colour) {(void)context;assert(x>=1&&y>=1&&width>0&&(colour==1||colour==2));Spans++;}
int main(void) {
    uint8_t raw[52]={0};float fov=90;memcpy(raw+48,&fov,4);CMPView view;assert(CMPMakeView(raw,8,8,&view));
    CMPVertex tri[3];CMVec3 world[3]={{10,-7.5f,7.5f},{10,7.5f,7.5f},{10,-7.5f,-7.5f}};
    for(unsigned i=0;i<3;i++)assert(CMPProject(&view,world[i],&tri[i]));
    CMPPixel pixels[64]={0};CMPMask mask={0,0,8,8,pixels};assert(CMPRasterTriangle(&mask,tri));uint32_t white=0,red=0;
    assert(CMPClassify(&mask,view.position,Query,NULL,&white,&red));assert(white==15&&red==6&&Queries==21);
    assert(CMPSpans(&mask,NULL,NULL,100)==9);assert(CMPSpans(&mask,Span,NULL,100)==9&&Spans==9);
    memset(pixels,0,sizeof(pixels));tri[1].world.x=tri[2].world.x=20;tri[1].world.y*=2;tri[1].world.z*=2;tri[2].world.y*=2;tri[2].world.z*=2;tri[1].inverse_depth=tri[2].inverse_depth=.05f;
    assert(CMPRasterTriangle(&mask,tri));CMPPixel *p=&pixels[2*8+2];assert(fabsf(p->world.x-13.333333f)<.0001f);CMPVertex projected;assert(CMPProject(&view,p->world,&projected));assert(fabsf(projected.xy.x-2.5f)<.00001f&&fabsf(projected.xy.y-2.5f)<.00001f);
    CMPVertex near[3];for(unsigned i=0;i<3;i++)assert(CMPProject(&view,world[i],&near[i]));assert(CMPRasterTriangle(&mask,near));assert(fabsf(p->world.x-10)<.0001f);
    Queries=0;StopAfter=2;assert(!CMPClassify(&mask,view.position,Query,NULL,&white,&red)&&Queries==2);
    CMPVertex invalid[3]={near[0],near[1],near[2]};invalid[0].inverse_depth=0;assert(!CMPRasterTriangle(&mask,invalid));
    float bad=NAN;memcpy(raw+48,&bad,4);assert(!CMPMakeView(raw,8,8,&view));
    CMTransform t={{0,0,0,1,0,0,0,0,1,1,1}};CMVec3 out;assert(CMWorldPosition(&t,&t,&out));
    puts("PASS: exact pixel centres; mixed wall-edge red/white; perspective-correct world position; nearest surface; query failure remains incomplete; run-length spans; malformed camera and triangle rejection");return 0;
}
