// Analytical fixtures; all builds/tests run in GitHub Actions.
#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelSkinning.h"
static bool Reader(uint64_t address,void *out,size_t size) { (void)address;memset(out,0,size);return false; }
static void Close(float a,float b) { assert(fabsf(a-b)<0.0001f); }
int main(void) {
    CMRReferences refs;uint8_t empty[16]={0};CMRCollect(empty,sizeof(empty),Reader,&refs);
    uint32_t info[]={100,0,UINT32_MAX,0,101,2,0,0};uint8_t reference[96]={0},pose[96]={0};
    CMTransform root={{0,0,0,1,10,0,0,0,1,1,1}},child={{0,0,0,1,0,5,0,0,1,1,1}};
    memcpy(reference,&root,44);memcpy(reference+48,&child,44);
    CMSMatrix inverse[2],skin[2];uint8_t known[2];
    assert(CMSReferenceInverse((uint8_t *)info,reference,2,inverse));
    root.values[4]=20;child.values[4]=20;child.values[5]=5;
    memcpy(pose,&root,44);memcpy(pose+48,&child,44);
    CMTransform component={{0,0,0,1,100,0,0,0,1,1,1}};
    assert(CMSBuildSkinMatrices(inverse,2,pose,2,48,NULL,&component,skin,known));
    uint8_t vertex[24]={0},weights[]={1,0,0,0,255,0,0,0};uint16_t palette[]={0,1};CMVec3 p={10,5,0},out;
    memcpy(vertex+8,&p,12);assert(CMSSkinVertex(vertex,weights,palette,2,skin,known,2,&out)==CMS_OK);
    Close(out.x,120);Close(out.y,5);
    int32_t missing[]={0,-1};assert(CMSBuildSkinMatrices(inverse,2,pose,2,48,missing,&component,skin,known));
    assert(CMSSkinVertex(vertex,weights,palette,2,skin,known,2,&out)==CMS_UNKNOWN_BONE);
    weights[4]=128;weights[5]=127;assert(CMSSkinVertex(vertex,weights,palette,2,skin,known,2,&out)==CMS_UNKNOWN_BONE);
    missing[1]=99;assert(!CMSBuildSkinMatrices(inverse,2,pose,2,48,missing,&component,skin,known));
    assert(CMSBuildSkinMatrices(inverse,2,pose,2,48,NULL,&component,skin,known));
    weights[4]=254;weights[5]=0;assert(CMSSkinVertex(vertex,weights,palette,2,skin,known,2,&out)==CMS_INVALID);
    weights[4]=255;weights[0]=2;assert(CMSSkinVertex(vertex,weights,palette,2,skin,known,2,&out)==CMS_INVALID);
    weights[0]=1;float bad=NAN;memcpy(vertex+8,&bad,4);assert(CMSSkinVertex(vertex,weights,palette,2,skin,known,2,&out)==CMS_INVALID);
    CMTransform turn={{0,0,0.70710678f,0.70710678f,10,20,30,0,2,3,4}};CMSMatrix matrix,inv;
    assert(CMSFromTransform(&turn,&matrix) && CMSInverse(matrix,&inv));p=(CMVec3){1,2,3};out=CMSPoint(matrix,p);
    Close(out.x,4);Close(out.y,22);Close(out.z,42);out=CMSPoint(inv,out);Close(out.x,1);Close(out.y,2);Close(out.z,3);
    turn.values[8]=0;assert(CMSFromTransform(&turn,&matrix) && !CMSInverse(matrix,&inv));
    turn.values[8]=1;turn.values[3]=0.9f;assert(!CMSFromTransform(&turn,&matrix));
    uint8_t sections[448]={0};uint64_t array=0x300000000ULL;uint32_t n=2,tri=1,verts=3;
    memcpy(sections+0x60,&array,8);memcpy(sections+0x68,&n,4);memcpy(sections+0x6C,&n,4);memcpy(sections+8,&tri,4);memcpy(sections+0x70,&verts,4);
    CMSSection section;assert(CMSParseSection(sections,sizeof(sections),0,3,3,&section));
    assert(section.bone_count==2 && section.vertex_count==3 && section.bone_address==array);
    assert(!CMSParseSection(sections,223,0,3,3,&section));assert(!CMSParseSection(sections,sizeof(sections),1,3,3,&section));
    n=UINT32_MAX;memcpy(sections+4,&n,4);assert(!CMSParseSection(sections,sizeof(sections),0,3,3,&section));
    assert(CMWorldPosition(&component,&root,&out));Close(out.x,120);
    puts("PASS: hierarchical inverse bind; current pose and world composition; palette-weighted skinning; strict missing bones; rotation and nonuniform scale; singular rejection; 224-byte section bounds and overflow checks");
    return 0;
}
