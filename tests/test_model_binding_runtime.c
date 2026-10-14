// Synthetic evidence fixtures; run only in GitHub Actions.
#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelBinding.h"
#include "../patch/CoreModelNames.h"

static const uint64_t TableSlot=0x300000000ULL,Table=0x300010000ULL,Chunk=0x300020000ULL,Entry=0x300040000ULL;
static const uint64_t Asset=0x200000000ULL,ChildAsset=0x200010000ULL,Class=0x200020000ULL;
static unsigned Calls;
static bool BadText,FailText,Stride16,FailWide;
static CMRHeader Header(uint32_t name) {
    CMRHeader h={0};h.vtable=0x100010000ULL;h.internal_index=5;h.class_address=Class;h.name_index=name;return h;
}
static bool Reader(uint64_t address,void *out,size_t size) {
    Calls++;memset(out,0,size);
    if (address==TableSlot && size==8) { memcpy(out,&Table,8);return true; }
    if (address>=Table && address<Table+64*8 && size==8) { memcpy(out,&Chunk,8);return true; }
    if (address>=Chunk && address<Chunk+16384*8 && size==8) { memcpy(out,&Entry,8);return true; }
    if (address==Entry+12 && size==CMN_TEXT_BYTES) {
        if (FailText) return false;
        if (BadText) memset(out,'x',size);else memcpy(out,"SkeletalMesh",13);
        return true;
    }
    if (address==Class && size==80) {
        CMRHeader h=Header(80275);memcpy(out,&h,sizeof(h));uint32_t bytes=1048,align=1;
        memcpy((uint8_t *)out+0x40,&bytes,4);memcpy((uint8_t *)out+0x44,&align,4);return true;
    }
    if (address==ChildAsset && (size==40 || size==1048)) {
        CMRHeader h=Header(550084);memcpy(out,&h,sizeof(h));return true;
    }
    if (address>=0x400000000ULL && address<0x400010000ULL) {
        bool info=address==0x400000000ULL || address==0x400004000ULL;
        if (info) {
            if (FailWide && size==48) return false;
            unsigned stride=Stride16?16:12;
            for (unsigned i=0;i<3;i++) {
                uint32_t name=100+i;int32_t parent=(int32_t)i-1;
                if (i*stride+12<=size) { memcpy((uint8_t *)out+i*stride,&name,4);memcpy((uint8_t *)out+i*stride+8,&parent,4); }
            }
        }
        return true;
    }
    return false;
}
static void Array(uint8_t *prefix,uint32_t offset,uint64_t address,int32_t count,int32_t capacity) {
    memcpy(prefix+offset,&address,8);memcpy(prefix+offset+8,&count,4);memcpy(prefix+offset+12,&capacity,4);
}

int main(void) {
    CMNNames names;CMNBegin(&names,Reader,TableSlot,12);assert(names.available && Calls==1);
    CMNAdd(&names,Reader,80275);assert(names.count==1 && names.names[0].valid && Calls==4);
    assert(!strcmp(names.names[0].text,"SkeletalMesh"));
    CMNAdd(&names,Reader,80275);assert(Calls==4); // same ID uses the catalog
    CMNAdd(&names,Reader,80276);assert(Calls==6); // same chunk uses its pointer cache
    CMNAdd(&names,Reader,548897);assert(Calls==9 && names.chunk_count==2);
    BadText=true;CMNAdd(&names,Reader,80277);assert(!names.names[3].valid && names.rejected_text==1);
    BadText=false;FailText=true;CMNAdd(&names,Reader,80278);assert(!names.names[4].valid && names.read_failures==1);
    FailText=false;unsigned before=Calls;CMNAdd(&names,Reader,1U<<27);assert(Calls==before);
    CMNBegin(&names,Reader,TableSlot,513);assert(!names.available && !names.read_attempts);
    CMNBegin(&names,Reader,TableSlot,12);names.read_attempts=319;
    CMNAdd(&names,Reader,100);assert(names.read_attempts==320 && names.truncated && !names.names[0].valid);
    CMNBegin(&names,Reader,TableSlot,12);
    for (unsigned i=0;i<130;i++) CMNAdd(&names,Reader,i+100);
    assert(names.count==128 && names.truncated && names.read_attempts<=320);
    CMNBegin(&names,Reader,TableSlot,12);
    for (unsigned i=0;i<33;i++) CMNAdd(&names,Reader,(i<<14)+100);
    assert(names.chunk_count==32 && names.truncated);

    // Use the production bounded collector once, then provide independent root
    // and child evidence to verify the additional fixed 0x828 pointer edge.
    CMRReferences refs;uint8_t layout[4096]={0};CMRCollect(layout,sizeof(layout),Reader,&refs);
    refs.count=3;CMRReference *root=&refs.references[0];root->address=Asset;root->source_offset=0x828;root->prefix_bytes=1048;
    root->header=Header(548897);root->class_header=Header(80275);root->class_header_valid=true;
    const uint32_t offsets[]={0x220,0x230,0x240,0x250,0x260};
    for (unsigned i=0;i<5;i++) Array(root->prefix,offsets[i],0x400000000ULL+i*0x2000,3,3);
    refs.references[1].address=0x500000000ULL;refs.references[1].source_offset=0x170;refs.references[1].prefix_bytes=4096;
    memcpy(refs.references[1].prefix+0x828,&ChildAsset,8);
    refs.references[2]=refs.references[1];refs.references[2].address+=0x2000;
    CMBBinding binding;Calls=0;CMBCollect(0x500010000ULL,&refs,Reader,&binding);
    assert(binding.assets.count==2 && binding.link_count==3 && binding.assets.read_attempts==3);
    assert(binding.links[1].asset_index==1 && binding.links[2].asset_index==1);
    assert(binding.buffer_count==5 && binding.read_attempts==5 && Calls==8);
    assert(binding.buffers[0].hierarchy_shape_valid && binding.buffers[2].hierarchy_shape_valid);
    assert(binding.buffers[0].bytes==36 && binding.buffers[1].bytes==144 && binding.buffers[4].bytes==192);

    Stride16=true;CMBCollect(0x500010000ULL,&refs,Reader,&binding);
    assert(binding.read_attempts==7 && binding.buffers[0].stride_hint==16 && binding.buffers[0].hierarchy_shape_valid);
    FailWide=true;CMBCollect(0x500010000ULL,&refs,Reader,&binding);
    assert(binding.read_failures==2 && binding.buffers[0].bytes==0 && !binding.buffers[0].hierarchy_shape_valid);
    Stride16=FailWide=false;Array(root->prefix,0x220,0x400000000ULL,257,257);
    CMBCollect(0x500010000ULL,&refs,Reader,&binding);assert(binding.rejected_arrays==1 && binding.buffer_count==4);
    Array(root->prefix,0x220,0x400000000ULL,3,2);
    CMBCollect(0x500010000ULL,&refs,Reader,&binding);assert(binding.rejected_arrays==1);
    uint8_t invalid[36]={0};assert(!CMBHierarchy(invalid,3,12));
    assert(!CMBRange((1ULL<<39)-8,16) && !CMNRange((1ULL<<39)-8,16));
    puts("PASS: pinned chunked names, caches, ASCII/termination, failed reads and exact budgets; fixed component/resource edges, deduplication, reference array bounds, hierarchy strides and failed reread isolation");
    return 0;
}
