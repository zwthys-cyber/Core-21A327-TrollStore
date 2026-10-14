// Independent synthetic streams; compile and run only in GitHub Actions.
#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelGPUStreams.h"
static const uint64_t Render=0x200000000ULL,LODs=0x200010000ULL,LOD=0x200020000ULL;
static const uint64_t Vertices=0x300000000ULL,Weights=0x300100000ULL,Index=0x300200000ULL,Sections=0x300300000ULL;
static unsigned Calls;static bool BadCount,FailLOD,FailSecondChunk;
static void Array(void *buffer,uint32_t offset,uint64_t pointer,int32_t count) {
    memcpy((uint8_t *)buffer+offset,&pointer,8);memcpy((uint8_t *)buffer+offset+8,&count,4);memcpy((uint8_t *)buffer+offset+12,&count,4);
}
static bool Reader(uint64_t address,void *out,size_t size) {
    Calls++;memset(out,0,size);
    if (address==Render && size==16) { Array(out,0,LODs,3);return true; }
    if (address==LODs && size==24) { uint64_t pointers[]={LOD,LOD+0x10000,LOD+0x20000};memcpy(out,pointers,24);return true; }
    if ((address==LOD || address==LOD+0x10000 || address==LOD+0x20000) && size==1024) {
        if (FailLOD) return false;
        Array(out,0,Sections,1);uint32_t n=3,stride=24,weights=8,indexWidth=0x602;
        memcpy((uint8_t *)out+0x50,&indexWidth,4);memcpy((uint8_t *)out+0x58,&Index,8);
        memcpy((uint8_t *)out+0x60,&n,4);memcpy((uint8_t *)out+0xC8,&Vertices,8);
        memcpy((uint8_t *)out+0xD0,&stride,4);memcpy((uint8_t *)out+0xD4,&n,4);
        memcpy((uint8_t *)out+0x148,&Weights,8);memcpy((uint8_t *)out+0x150,&weights,4);
        if (BadCount) n=4;memcpy((uint8_t *)out+0x154,&n,4);return true;
    }
    if ((address==Index && size==112) || (address==Sections && size==256)) return true;
    if (address>=Vertices && address<Vertices+40960 && size<=16384) {
        if (FailSecondChunk && address==Vertices+16384) return false;
        memset(out,0xAB,size);return true;
    }
    return false;
}
int main(void) {
    CMRReferences refs;uint8_t empty[16]={0};CMRCollect(empty,sizeof(empty),Reader,&refs);
    CMBBinding binding={0};CMNNames names={0};binding.assets.count=1;
    CMRReference *asset=&binding.assets.references[0];asset->address=0x400000000ULL;asset->prefix_bytes=1048;asset->class_header_valid=true;
    asset->class_header.name_index=99;asset->header.name_index=100;asset->header.internal_index=42;
    names.count=1;names.names[0].index=99;names.names[0].valid=true;memcpy(names.names[0].text,"SkeletalMesh",13);
    memcpy(asset->prefix+0x38,&Render,8);float extent[3]={10,10,10};memcpy(asset->prefix+0x7C,extent,12);
    CMGDescriptors d;Calls=0;CMGCollect(&binding,&names,Reader,&d);
    assert(d.count==3 && d.read_attempts==11 && Calls==11 && !d.read_failures && !d.rejected && !d.truncated);
    assert(d.descriptors[0].vertex_count==3 && d.descriptors[2].lod_index==2 && d.descriptors[0].index_width==2);
    assert(d.descriptors[0].index_bytes==112 && d.descriptors[0].section_bytes==256);
    CMGKey a=CMGKeyFor(&d.descriptors[0]),b=a;assert(CMGKeyEqual(&a,&b));
    b.internal_index++;assert(!CMGKeyEqual(&a,&b));b=a;b.weights+=8;assert(!CMGKeyEqual(&a,&b));
    BadCount=true;CMGCollect(&binding,&names,Reader,&d);assert(!d.count && d.rejected==3);BadCount=false;
    FailLOD=true;CMGCollect(&binding,&names,Reader,&d);assert(!d.count && d.read_failures==3);FailLOD=false;
    CMGCollect(&binding,&names,Reader,&d);CMGDescriptor *v=&d.descriptors[0];uint8_t vertices[72]={0},weights[24]={0};
    for (unsigned i=0;i<3;i++) { float xyz[3]={(float)i,2,3};memcpy(vertices+i*24+8,xyz,12);weights[i*8+4]=255; }
    assert(CMGPositionHypothesis(v,vertices,sizeof(vertices)) && CMGWeightHypothesis(v,weights,sizeof(weights)));
    float bad=INFINITY;memcpy(vertices+8,&bad,4);assert(!CMGPositionHypothesis(v,vertices,sizeof(vertices)));
    bad=11;memcpy(vertices+8,&bad,4);assert(!CMGPositionHypothesis(v,vertices,sizeof(vertices)));
    weights[4]=254;assert(!CMGWeightHypothesis(v,weights,sizeof(weights)));
    uint16_t indices[]={0,1,2};assert(CMGIndexHypothesis((uint8_t *)indices,sizeof(indices),2,3));
    indices[2]=3;assert(!CMGIndexHypothesis((uint8_t *)indices,sizeof(indices),2,3));
    assert(!CMGIndexHypothesis((uint8_t *)indices,4,2,3));
    uint8_t prefix[16]={0};Array(prefix,0,Vertices,3);uint64_t address=0;uint32_t count=0;
    assert(CMGArrayCandidate(prefix,16,0,2,3,&address,&count) && address==Vertices && count==3);
    assert(!CMGArrayCandidate(prefix,15,0,2,3,&address,&count));
    assert(!CMGArrayCandidate(prefix,16,0,2,2,&address,&count));
    CMGStreamBudget budget={0};uint8_t bytes[40960];Calls=0;
    assert(CMGStreamRead(&budget,Reader,Vertices,bytes,sizeof(bytes)) && Calls==3 && budget.completed_bytes==40960);
    FailSecondChunk=true;memset(&budget,0,sizeof(budget));Calls=0;
    assert(!CMGStreamRead(&budget,Reader,Vertices,bytes,sizeof(bytes)) && Calls==2 && budget.read_failures==1 && !budget.completed_bytes);
    assert(budget.attempted_bytes==32768);FailSecondChunk=false;
    budget.read_attempts=CMG_STREAM_READ_LIMIT-1;Calls=0;
    assert(!CMGStreamRead(&budget,Reader,Vertices,bytes,sizeof(bytes)) && !Calls && budget.truncated);
    memset(&budget,0,sizeof(budget));budget.attempted_bytes=CMG_STREAM_BYTE_LIMIT-8;
    assert(!CMGStreamRead(&budget,Reader,Vertices,bytes,16) && budget.truncated && !Calls);
    assert(CMGStreamRead(&budget,Reader,Vertices,bytes,8) && budget.attempted_bytes==CMG_STREAM_BYTE_LIMIT);
    assert(!CMGRange((1ULL<<39)-8,16));
    puts("PASS: three LODs; matching stream counts; low-byte index width; reused-resource cache identity; position/weight/index hypotheses; bounded array candidates; 16 KiB chunks; failed partial reads; exact global budgets");
    return 0;
}
