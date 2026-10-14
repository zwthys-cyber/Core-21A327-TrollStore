#ifndef CORE_MODEL_BINDING_H
#define CORE_MODEL_BINDING_H
#include "CoreModelReferences.h"

#define CMB_LINK_LIMIT 9
#define CMB_BUFFER_LIMIT 5
#define CMB_BUFFER_BYTES 16384
#define CMB_BUFFER_READ_LIMIT 8
typedef struct { uint64_t component,address;uint32_t asset_index; } CMBLink;
typedef struct {
    uint64_t address;
    uint32_t offset,count,capacity,bytes,stride_hint;
    bool hierarchy_shape_valid;
    uint8_t data[CMB_BUFFER_BYTES];
} CMBBuffer;
typedef struct {
    CMRReferences assets;
    CMBLink links[CMB_LINK_LIMIT];
    CMBBuffer buffers[CMB_BUFFER_LIMIT];
    uint32_t link_count,buffer_count,read_attempts,read_failures,rejected_arrays;
    bool truncated;
} CMBBinding;

static inline bool CMBRange(uint64_t address,size_t size) {
    return address>=0x100000000ULL && address<(1ULL<<39) && !(address&7) &&
        size && size<=CMB_BUFFER_BYTES && size<=(1ULL<<39)-address;
}
static inline bool CMBRead(CMBBinding *out,CMRReader reader,uint64_t address,void *buffer,size_t size) {
    if (!CMBRange(address,size)) return false;
    if (out->read_attempts>=CMB_BUFFER_READ_LIMIT) { out->truncated=true;return false; }
    out->read_attempts++;
    if (reader(address,buffer,size)) return true;
    out->read_failures++;return false;
}
static inline bool CMBHierarchy(const uint8_t *data,uint32_t count,uint32_t stride) {
    if (!count || count>256 || (stride!=12 && stride!=16)) return false;
    for (uint32_t i=0;i<count;i++) {
        uint32_t name=0,number=0;int32_t parent=0;
        memcpy(&name,data+i*stride,4);memcpy(&number,data+i*stride+4,4);memcpy(&parent,data+i*stride+8,4);
        if (!name || name>=(1U<<27) || number>=65536 ||
            (i==0 ? parent!=-1 : parent<0 || (uint32_t)parent>=i)) return false;
    }
    return true;
}
static inline void CMBCollect(uint64_t component,const CMRReferences *refs,CMRReader reader,CMBBinding *out) {
    memset(out,0,sizeof(*out));
    if (!refs || !reader) return;
    const CMRReference *root=NULL;
    for (uint32_t i=0;i<refs->count;i++) if (refs->references[i].source_offset==0x828) root=&refs->references[i];
    if (!root) return;
    out->assets.references[0]=*root;out->assets.count=1;
    out->links[out->link_count++]=(CMBLink){component,root->address,0};
    // One additional fixed edge: candidate child component -> its 0x828 slot.
    // No arbitrary pointer recursion and no assumption that the child is a mesh.
    for (uint32_t i=0;i<refs->count && out->link_count<CMB_LINK_LIMIT;i++) {
        const CMRReference *child=&refs->references[i];
        if (child->source_offset!=0x170 || child->prefix_bytes<0x830) continue;
        uint64_t address=0;memcpy(&address,child->prefix+0x828,8);
        if (!CMRPointer(address,sizeof(CMRHeader))) continue;
        CMRAdd(&out->assets,reader,address,0x828,UINT32_MAX);
        uint32_t assetIndex=UINT32_MAX;
        for (uint32_t j=0;j<out->assets.count;j++) if (out->assets.references[j].address==address) assetIndex=j;
        out->links[out->link_count++]=(CMBLink){child->address,address,assetIndex};
    }
    // These paired array shapes were present in all three v4 resource prefixes.
    // They are hypotheses until names, hierarchy and reference poses agree.
    const uint32_t offsets[]={0x220,0x230,0x240,0x250,0x260};
    const uint32_t strides[]={12,48,12,48,64};
    for (uint32_t i=0;i<CMB_BUFFER_LIMIT;i++) {
        if (root->prefix_bytes<offsets[i]+16) continue;
        uint64_t address=0;int32_t count=0,capacity=0;
        memcpy(&address,root->prefix+offsets[i],8);memcpy(&count,root->prefix+offsets[i]+8,4);memcpy(&capacity,root->prefix+offsets[i]+12,4);
        if (!count && !capacity) continue;
        if (count<1 || count>256 || capacity<count || capacity>512 || !CMBRange(address,(size_t)count*strides[i])) {
            out->rejected_arrays++;continue;
        }
        CMBBuffer *buffer=&out->buffers[out->buffer_count];
        buffer->address=address;buffer->offset=offsets[i];buffer->count=(uint32_t)count;
        buffer->capacity=(uint32_t)capacity;buffer->stride_hint=strides[i];
        if (!CMBRead(out,reader,address,buffer->data,(size_t)count*strides[i])) continue;
        buffer->bytes=(uint32_t)count*strides[i];
        if (i==0 || i==2) {
            buffer->hierarchy_shape_valid=CMBHierarchy(buffer->data,buffer->count,12);
            if (!buffer->hierarchy_shape_valid && CMBRead(out,reader,address,buffer->data,(size_t)count*16)) {
                buffer->bytes=(uint32_t)count*16;buffer->stride_hint=16;
                buffer->hierarchy_shape_valid=CMBHierarchy(buffer->data,buffer->count,16);
            } else if (!buffer->hierarchy_shape_valid) {
                // A failed second read may clear/partially replace the data.
                buffer->bytes=0;
            }
        }
        out->buffer_count++;
    }
}
#endif
