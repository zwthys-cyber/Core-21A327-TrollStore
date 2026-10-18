// Actual libdispatch row work against the original sequential triangle loop.
// Run with ASan/UBSan and TSan in GitHub Actions only.
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../patch/CoreModelRasterWork.h"
static uint32_t RandomState=8127391;
static float Random(float size){RandomState=RandomState*1664525U+1013904223U;return ((float)(RandomState>>8)/16777216.f*2-1)*size;}
int main(void) {
    CMTransform identity={{0,0,0,1,0,0,0,0,1,1,1}};CMVec3 origin;assert(CMWorldPosition(&identity,&identity,&origin));
    enum {W=129,H=97,WORDS=3,MESHES=3,VERTICES=24,TRIANGLES=8};
    size_t bytes=(size_t)W*H*sizeof(CMPPixel),bits=(size_t)WORDS*H*8;
    CMPPixel *reference=calloc(W*H,sizeof(CMPPixel)),*work=calloc(W*H,sizeof(CMPPixel));uint64_t *rb=calloc(WORDS*H,8),*wb=calloc(WORDS*H,8);assert(reference&&work&&rb&&wb);
    CMPMask a={11,17,W,H,reference},b={11,17,W,H,work};CMPCoverage ac={rb,WORDS,0},bc={wb,WORDS,0};
    CMPVertex points[MESHES][VERTICES];uint8_t valid[MESHES][VERTICES];uint32_t indices[MESHES][TRIANGLES*3];CMPRasterMesh meshes[MESHES];
    for(unsigned run=0;run<1000;run++){
        if(run%4==0){memset(reference,0,bytes);memset(work,0,bytes);memset(rb,0,bits);memset(wb,0,bits);ac.count=bc.count=0;}
        uint64_t expected=0;
        for(unsigned j=0;j<MESHES;j++){
            for(unsigned v=0;v<VERTICES;v++){points[j][v]=(CMPVertex){{Random(100)+75,Random(100)+65},{origin.x+10,origin.y+2,origin.z+3},.01f+fabsf(Random(.05f))};valid[j][v]=(run+v)%17!=0;indices[j][v]=v;}
            if(run%7==0){points[j][1].xy=points[j][0].xy;points[j][2].xy=points[j][0].xy;}
            for(unsigned i=0;i<TRIANGLES;i++){
                unsigned x=i*3,y=x+1,z=x+2;if(!valid[j][x]||!valid[j][y]||!valid[j][z])continue;
                CMPVertex triangle[3]={points[j][x],points[j][y],points[j][z]};if(CMPRasterTriangleCovered(&a,triangle,&ac))expected++;
            }
            meshes[j]=(CMPRasterMesh){points[j],indices[j],valid[j],TRIANGLES,VERTICES};
        }
        assert(CMPRasterMeshWork(&b,&bc,meshes,MESHES,2)==expected);
        assert(ac.count==bc.count&&!memcmp(reference,work,bytes)&&!memcmp(rb,wb,bits));
    }
    // Production-size disjoint rows, triangle overdraw and 64-bit word tails.
    free(reference);free(work);free(rb);free(wb);
    const int width=1025,height=257;size_t count=(size_t)width*height;uint32_t words=(width+63)/64;
    reference=calloc(count,sizeof(CMPPixel));work=calloc(count,sizeof(CMPPixel));rb=calloc((size_t)words*height,8);wb=calloc((size_t)words*height,8);assert(reference&&work&&rb&&wb);
    a=(CMPMask){3,5,width,height,reference};b=(CMPMask){3,5,width,height,work};ac=(CMPCoverage){rb,words,0};bc=(CMPCoverage){wb,words,0};
    CMPVertex triangle[3]={{{0,0},{10,0,0},.1f},{{1028,6},{10,0,0},.1f},{{1028,262},{10,0,0},.1f}};uint8_t ok[3]={1,1,1};uint32_t index[3]={0,1,2};CMPRasterMesh mesh={triangle,index,ok,1,3};
    assert(CMPRasterTriangleCovered(&a,triangle,&ac));assert(CMPRasterMeshWork(&b,&bc,&mesh,1,2)==1&&ac.count==bc.count&&!memcmp(reference,work,count*sizeof(CMPPixel))&&!memcmp(rb,wb,(size_t)words*height*8));
    assert(CMPRasterMeshWork(&b,&bc,&mesh,1,2)==1&&ac.count==bc.count); // Existing coverage is not counted again.
    free(reference);free(work);free(rb);free(wb);
    puts("PASS: 1000 bit-identical parallel/sequential raster comparisons; unchanged triangle/depth order, overlap, invalid vertices, row splits, offsets, word tails and production-size disjoint pixel/coverage writes");return 0;
}
