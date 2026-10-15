#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelPixels.h"
static unsigned Queries,Spans,StopAfter;
static bool Query(void *context,const CMVec3 *camera,const CMVec3 *world,bool *blocked) {
    (void)context;(void)camera;if(StopAfter&&Queries>=StopAfter)return false;Queries++;*blocked=world->y>=0;return true;
}
static void Span(void *context,int x,int y,int width,uint8_t colour) {(void)context;assert(x>=1&&y>=1&&width>0&&(colour==1||colour==2));Spans++;}
static uint32_t CheckRectangles(const CMPMask *mask) {
    CMPRectangle rectangles[256];uint32_t count=CMPRectangles(mask,rectangles,256);assert(count<=256);
    uint8_t *coverage=calloc((size_t)mask->width*mask->height,1);assert(coverage);
    for(uint32_t i=0;i<count;i++){CMPRectangle r=rectangles[i];assert(r.width>0&&r.height>0&&r.x>=mask->x&&r.y>=mask->y&&r.x+r.width<=mask->x+mask->width&&r.y+r.height<=mask->y+mask->height);
        for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++){size_t p=(size_t)(y-mask->y)*mask->width+x-mask->x;assert(!coverage[p]);coverage[p]=r.colour;}}
    for(size_t p=0;p<(size_t)mask->width*mask->height;p++)assert(coverage[p]==mask->pixels[p].colour);
    free(coverage);return count;
}
static void CheckRotatedCamera(void) {
    float camera[13]={123456,-234567,345678,0,0,0,17,63,-21,0,0,0,87};CMPView view;assert(CMPMakeView((const uint8_t *)camera,800,600,&view));
    const float depths[3]={1000,2200,1500};const int xs[3]={100,650,150},ys[3]={100,120,550};CMPVertex tri[3];
    for(unsigned i=0;i<3;i++){CMVec3 world;assert(CMPWorldAtPixel(&view,xs[i],ys[i],1/depths[i],&world)&&CMPProject(&view,world,&tri[i]));}
    CMPPixel *pixels=calloc(800*600,sizeof(CMPPixel));assert(pixels);CMPMask mask={0,0,800,600,pixels};assert(CMPRasterTriangle(&mask,tri));
    const int x=220,y=230;CMPPixel pixel=pixels[y*800+x];assert(pixel.inverse_depth>0);
    float area=CMPEdge(tri[0].xy,tri[1].xy,tri[2].xy.x,tri[2].xy.y),a=CMPEdge(tri[1].xy,tri[2].xy,x+.5f,y+.5f)/area,b=CMPEdge(tri[2].xy,tri[0].xy,x+.5f,y+.5f)/area;
    const float bary[3]={a,b,1-a-b};CMVec3 expected={0},actual;
    for(unsigned i=0;i<3;i++){float w=bary[i]*tri[i].inverse_depth/pixel.inverse_depth;expected.x+=w*tri[i].world.x;expected.y+=w*tri[i].world.y;expected.z+=w*tri[i].world.z;}
    assert(CMPWorldAtPixel(&view,x,y,pixel.inverse_depth,&actual));assert(fabsf(actual.x-expected.x)<.1f&&fabsf(actual.y-expected.y)<.1f&&fabsf(actual.z-expected.z)<.1f);
    CMPVertex projected;assert(CMPProject(&view,actual,&projected));assert(fabsf(projected.xy.x-x-.5f)<.02f&&fabsf(projected.xy.y-y-.5f)<.02f);free(pixels);
}
int main(void) {
    uint8_t raw[52]={0};float fov=90;memcpy(raw+48,&fov,4);CMPView view;assert(CMPMakeView(raw,8,8,&view));
    CMPVertex tri[3];CMVec3 world[3]={{10,-7.5f,7.5f},{10,7.5f,7.5f},{10,-7.5f,-7.5f}};
    for(unsigned i=0;i<3;i++)assert(CMPProject(&view,world[i],&tri[i]));
    CMPPixel pixels[64]={0};CMPMask mask={0,0,8,8,pixels};assert(CMPRasterTriangle(&mask,tri));uint32_t white=0,red=0;
    assert(CMPClassify(&mask,&view,Query,NULL,&white,&red));assert(white==15&&red==6&&Queries==21);
    assert(CMPSpans(&mask,NULL,NULL,100)==9);assert(CMPSpans(&mask,Span,NULL,100)==9&&Spans==9);
    assert(CheckRectangles(&mask)==6);CMPRectangle small[5];assert(CMPRectangles(&mask,small,5)==6);
    memset(pixels,0,sizeof(pixels));tri[1].world.x=tri[2].world.x=20;tri[1].world.y*=2;tri[1].world.z*=2;tri[2].world.y*=2;tri[2].world.z*=2;tri[1].inverse_depth=tri[2].inverse_depth=.05f;
    assert(CMPRasterTriangle(&mask,tri));CMPPixel *p=&pixels[2*8+2];CMVec3 surface;assert(CMPWorldAtPixel(&view,2,2,p->inverse_depth,&surface)&&fabsf(surface.x-13.333333f)<.0001f);CMPVertex projected;assert(CMPProject(&view,surface,&projected));assert(fabsf(projected.xy.x-2.5f)<.00001f&&fabsf(projected.xy.y-2.5f)<.00001f);
    CMPVertex near[3];for(unsigned i=0;i<3;i++)assert(CMPProject(&view,world[i],&near[i]));assert(CMPRasterTriangle(&mask,near));assert(CMPWorldAtPixel(&view,2,2,p->inverse_depth,&surface)&&fabsf(surface.x-10)<.0001f);
    Queries=0;StopAfter=2;assert(!CMPClassify(&mask,&view,Query,NULL,&white,&red)&&Queries==2);
    CMPVertex invalid[3]={near[0],near[1],near[2]};invalid[0].inverse_depth=0;assert(!CMPRasterTriangle(&mask,invalid));
    float bad=NAN;memcpy(raw+48,&bad,4);assert(!CMPMakeView(raw,8,8,&view));
    CMTransform t={{0,0,0,1,0,0,0,0,1,1,1}};CMVec3 out;assert(CMWorldPosition(&t,&t,&out));
    CheckRotatedCamera();
    CMPPixel *solid=calloc(5000,sizeof(CMPPixel));assert(solid);for(unsigned i=0;i<5000;i++)solid[i].colour=1;CMPMask tall={20,30,1,5000,solid};CMPRectangle rectangle;
    assert(CMPSpans(&tall,NULL,NULL,4096)==4097&&CMPRectangles(&tall,&rectangle,1)==1&&rectangle.x==20&&rectangle.y==30&&rectangle.height==5000);free(solid);
    for(unsigned pattern=0;pattern<40;pattern++){for(unsigned i=0;i<64;i++)pixels[i].colour=(uint8_t)(((i*13+pattern*19)>>(pattern%5))%3);CheckRectangles(&mask);}
    memset(pixels,0,sizeof(pixels));pixels[0].colour=pixels[16].colour=1;assert(CheckRectangles(&mask)==2); // An empty row must break a rectangle.
    puts("PASS: exact pixel centres; mixed wall-edge red/white; 8-byte perspective depth with rotated large-origin cameras; nearest surface; incomplete queries; exact nonoverlapping rectangle coverage, colour boundaries, empty rows, offsets and limits; malformed inputs");return 0;
}
