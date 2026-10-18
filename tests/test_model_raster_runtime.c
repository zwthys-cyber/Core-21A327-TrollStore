// Compare the optimized candidate scan against v10's full bounding-box scan.
// Build and run only in GitHub Actions.
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../patch/CoreModelPixels.h"
static bool Reference(CMPMask *mask,const CMPVertex t[3],CMPCoverage *coverage) {
    float area=CMPEdge(t[0].xy,t[1].xy,t[2].xy.x,t[2].xy.y);
    if(!isfinite(area)||fabsf(area)<1e-7f)return false;
    for(unsigned i=0;i<3;i++)if(!isfinite(t[i].inverse_depth)||t[i].inverse_depth<=0||!isfinite(t[i].world.x)||!isfinite(t[i].world.y)||!isfinite(t[i].world.z))return false;
    float minX=fminf(t[0].xy.x,fminf(t[1].xy.x,t[2].xy.x)),maxX=fmaxf(t[0].xy.x,fmaxf(t[1].xy.x,t[2].xy.x));
    float minY=fminf(t[0].xy.y,fminf(t[1].xy.y,t[2].xy.y)),maxY=fmaxf(t[0].xy.y,fmaxf(t[1].xy.y,t[2].xy.y));
    int x0=(int)fmaxf(mask->x,ceilf(minX-.5f)),x1=(int)fminf(mask->x+mask->width-1,floorf(maxX-.5f));
    int y0=(int)fmaxf(mask->y,ceilf(minY-.5f)),y1=(int)fminf(mask->y+mask->height-1,floorf(maxY-.5f));
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        float a=CMPEdge(t[1].xy,t[2].xy,x+.5f,y+.5f)/area,b=CMPEdge(t[2].xy,t[0].xy,x+.5f,y+.5f)/area,c=1-a-b;
        if(a<-1e-6f||b<-1e-6f||c<-1e-6f)continue;
        float depth=a*t[0].inverse_depth+b*t[1].inverse_depth+c*t[2].inverse_depth;
        CMPPixel *p=&mask->pixels[(size_t)(y-mask->y)*mask->width+(x-mask->x)];
        if(!isfinite(depth)||depth<=p->inverse_depth)continue;
        if(!p->inverse_depth){coverage->words[(size_t)(y-mask->y)*coverage->words_per_row+(x-mask->x)/64]|=1ULL<<((x-mask->x)%64);coverage->count++;}
        p->inverse_depth=depth;p->colour=0;
    }
    return true;
}
static uint32_t RandomState=123456789;
static float Random(float range){RandomState=RandomState*1664525U+1013904223U;return ((float)(RandomState>>8)/16777216.f*2-1)*range;}
int main(void) {
    CMTransform identity={{0,0,0,1,0,0,0,0,1,1,1}};CMVec3 origin;assert(CMWorldPosition(&identity,&identity,&origin));
    enum {W=129,H=97,WORDS=3};size_t count=W*H,bytes=count*sizeof(CMPPixel),wordBytes=WORDS*H*8;
    CMPPixel *a=calloc(count,sizeof(*a)),*b=calloc(count,sizeof(*b));uint64_t *ab=calloc(WORDS*H,8),*bb=calloc(WORDS*H,8);assert(a&&b&&ab&&bb);
    CMPMask old={11,17,W,H,a},fast={11,17,W,H,b};CMPCoverage ac={ab,WORDS,0},bc={bb,WORDS,0};
    for(unsigned n=0;n<8000;n++){
        if(n%8==0){memset(a,0,bytes);memset(b,0,bytes);memset(ab,0,wordBytes);memset(bb,0,wordBytes);ac.count=bc.count=0;}
        float range=n%11==0?65535:250;CMPVertex t[3]={0};
        for(unsigned i=0;i<3;i++){t[i].xy=(CMVec2){Random(range)+75,Random(range)+65};t[i].world=(CMVec3){origin.x+10+i,origin.y+2,origin.z+3};t[i].inverse_depth=.01f+(float)(i+1)*.02f;}
        if(n%5==0){t[0].xy=(CMVec2){.5f,17.5f};t[1].xy=(CMVec2){140.5f,113.5f};t[2].xy=(CMVec2){139.5f+Random(.01f),113.5f+Random(.01f)};}
        if(n%17==0){t[2].xy=t[1].xy;t[2].xy.y=nextafterf(t[2].xy.y,INFINITY);}
        assert(Reference(&old,t,&ac)==CMPRasterTriangleCovered(&fast,t,&bc));
        if(ac.count!=bc.count||memcmp(a,b,bytes)||memcmp(ab,bb,wordBytes)){fprintf(stderr,"raster mismatch n=%u counts=%u/%u\n",n,ac.count,bc.count);return 1;}
    }
    // A long diagonal triangle has a large empty bounding box. Its exact row
    // scan should visit fewer candidates without changing coverage or depth.
    CMPVertex diagonal[3]={{{0,0},{10,0,0},.1f},{{1024,256},{10,0,0},.1f},{{1024,252},{10,0,0},.1f}};
    float area=CMPEdge(diagonal[0].xy,diagonal[1].xy,diagonal[2].xy.x,diagonal[2].xy.y);int padding=CMPScanPadding(diagonal,area,1024,256);assert(padding>=0);
    uint64_t candidates=0;for(int y=0;y<256;y++){int first=0,last=1023;CMPScanRow(diagonal,y,padding,&first,&last);if(last>=first)candidates+=(unsigned)(last-first+1);}
    assert(candidates<1024*256/4);
    printf("PASS: 8000 bit-identical full-resolution raster comparisons, including overlap, skinny/degenerate triangles, large offscreen coordinates, pixel edges and bitsets; diagonal candidates %llu/262144\n",(unsigned long long)candidates);
    free(a);free(b);free(ab);free(bb);return 0;
}
