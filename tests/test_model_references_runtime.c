// Synthetic memory fixtures; no game, process bootstrap or kernel calls.
#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelReferences.h"

static const uint64_t ObjectA=0x200000000ULL,ObjectB=0x200002000ULL;
static const uint64_t ClassObject=0x200004000ULL,ArrayData=0x200006000ULL;
static unsigned Calls;
static bool GenerateAny;

static CMRHeader Header(uint32_t name) {
    CMRHeader h={0};h.vtable=0x100010000ULL;h.flags=0x40008;h.internal_index=602161;
    h.class_address=ClassObject;h.name_index=name;return h;
}
static bool Reader(uint64_t address,void *out,size_t size) {
    Calls++;
    if (address==ClassObject && size==40) { CMRHeader h=Header(71);memcpy(out,&h,size);return true; }
    if (address==ArrayData && size==24) {
        const uint64_t a[]={ObjectB,ObjectA,0x200008000ULL};memcpy(out,a,size);return true;
    }
    if (size==1024 && (address==ObjectA || address==ObjectB || GenerateAny)) {
        memset(out,0,size);CMRHeader h=Header(address==ObjectA?100:101);memcpy(out,&h,sizeof(h));return true;
    }
    if (address==0x200008000ULL && size==1024) { memset(out,0,size);return true; }
    return false;
}
static void Pointer(uint8_t *layout,unsigned offset,uint64_t address) { memcpy(layout+offset,&address,8); }

int main(void) {
    uint8_t layout[4096]={0};CMRReferences result;
    Pointer(layout,0x88,ObjectA);Pointer(layout,0x130,ObjectA);
    Pointer(layout,0xA0,ArrayData);int32_t count=3;memcpy(layout+0xA8,&count,4);memcpy(layout+0xAC,&count,4);
    CMRCollect(layout,sizeof(layout),Reader,&result);
    assert(result.count==2 && Calls==6 && result.rejected_headers==1);
    assert(result.references[0].source_offset==0x88 && result.references[0].array_index==UINT32_MAX);
    assert(result.references[1].source_offset==0xA0 && result.references[1].array_index==0);
    assert(result.references[0].header.name_index==100 && result.references[1].header.name_index==101);
    assert(result.references[0].class_header_valid && result.references[1].class_header_valid);

    memset(layout,0,sizeof(layout));Pointer(layout,0xA0,ArrayData);
    count=-1;memcpy(layout+0xA8,&count,4);count=3;memcpy(layout+0xAC,&count,4);Calls=0;
    CMRCollect(layout,sizeof(layout),Reader,&result);assert(!Calls && !result.count && result.rejected_arrays==1);
    Calls=0;CMRCollect(layout,2047,Reader,&result);assert(!Calls && !result.count);
    CMRCollect(layout,8192,Reader,&result);assert(!Calls && !result.count);

    memset(layout,0,sizeof(layout));Pointer(layout,0x88,ObjectA+1);Calls=0;
    CMRCollect(layout,sizeof(layout),Reader,&result);assert(!Calls && !result.count);
    Pointer(layout,0x88,0x20000E000ULL);CMRCollect(layout,sizeof(layout),Reader,&result);
    assert(Calls==1 && result.read_failures==1 && !result.count);

    memset(&result,0,sizeof(result));result.read_attempts=39;Calls=0;
    CMRAdd(&result,Reader,ObjectA,0x88,UINT32_MAX);
    assert(Calls==1 && result.read_attempts==40 && result.truncated && result.count==1 && !result.references[0].class_header_valid);
    CMRAdd(&result,Reader,ObjectB,0x90,UINT32_MAX);assert(Calls==1 && result.count==1);

    memset(&result,0,sizeof(result));Calls=0;GenerateAny=true;
    for (unsigned i=0;i<20;i++) CMRAdd(&result,Reader,0x300000000ULL+(uint64_t)i*4096,0x88,UINT32_MAX);
    assert(result.count==12 && Calls==24 && result.truncated);
    CMRHeader invalid=Header(1);invalid.name_index=0;assert(!CMRHeaderCandidate(&invalid));
    invalid=Header(1);invalid.internal_index=-1;assert(!CMRHeaderCandidate(&invalid));
    invalid=Header(1);invalid.class_address=(1ULL<<39)-16;assert(!CMRHeaderCandidate(&invalid));
    puts("PASS: bounded candidate headers/classes, deduplication, array limits, bad headers, failed reads, pointer bounds and exact read/reference budgets");
    return 0;
}
