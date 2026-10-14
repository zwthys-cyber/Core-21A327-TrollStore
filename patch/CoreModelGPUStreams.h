#ifndef CORE_MODEL_GPU_STREAMS_H
#define CORE_MODEL_GPU_STREAMS_H
#include "CoreModelGPUDescriptors.h"

#define CMG_STREAM_READ_LIMIT 512
#define CMG_STREAM_BYTE_LIMIT (3*1024*1024)
#define CMG_CACHE_LIMIT 96
typedef struct {
    uint32_t read_attempts,read_failures,attempted_bytes,completed_bytes;
    bool truncated;
} CMGStreamBudget;
typedef struct {
    uint64_t asset,render,lod,vertices,weights;
    uint32_t name,number,internal_index,vertex_count,vertex_stride,weight_stride;
} CMGKey;
static inline CMGKey CMGKeyFor(const CMGDescriptor *d) {
    return (CMGKey){d->asset_address,d->render_address,d->lod_address,d->vertex_address,d->weight_address,
        d->name_index,d->name_number,d->internal_index,d->vertex_count,d->vertex_stride,d->weight_stride};
}
static inline bool CMGKeyEqual(const CMGKey *a,const CMGKey *b) {
    return a->asset==b->asset && a->render==b->render && a->lod==b->lod && a->vertices==b->vertices && a->weights==b->weights &&
        a->name==b->name && a->number==b->number && a->internal_index==b->internal_index && a->vertex_count==b->vertex_count &&
        a->vertex_stride==b->vertex_stride && a->weight_stride==b->weight_stride;
}
// Called under the capture lock. Budgets count failed attempts as well, so a
// failed stream cannot bypass the session's byte/read limits by retrying.
static inline bool CMGStreamRead(CMGStreamBudget *b,CMRReader reader,uint64_t address,void *out,size_t size) {
    if (!out || !reader || !CMGRange(address,size)) return false;
    if (size>CMG_STREAM_BYTE_LIMIT-b->attempted_bytes || (size+16383)/16384>CMG_STREAM_READ_LIMIT-b->read_attempts) {
        b->truncated=true;return false;
    }
    for (size_t offset=0;offset<size;) {
        size_t chunk=size-offset;if (chunk>16384) chunk=16384;
        b->read_attempts++;b->attempted_bytes+=(uint32_t)chunk;
        if (!reader(address+offset,(uint8_t *)out+offset,chunk)) { b->read_failures++;return false; }
        offset+=chunk;
    }
    b->completed_bytes+=(uint32_t)size;return true;
}
static inline bool CMGArrayCandidate(const uint8_t *bytes,size_t size,uint32_t offset,uint32_t stride,uint32_t maxCount,
                                     uint64_t *address,uint32_t *count) {
    if (size<offset+16 || !stride) return false;
    int32_t n=0,cap=0;memcpy(address,bytes+offset,8);memcpy(&n,bytes+offset+8,4);memcpy(&cap,bytes+offset+12,4);
    if (n<=0 || (uint32_t)n>maxCount || cap<n || (uint64_t)cap>(uint64_t)maxCount*2 || !CMGRange(*address,(size_t)n*stride)) return false;
    *count=(uint32_t)n;return true;
}
#endif
