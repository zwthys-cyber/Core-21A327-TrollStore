#ifndef CORE_MODEL_GPU_DESCRIPTORS_H
#define CORE_MODEL_GPU_DESCRIPTORS_H
#include "CoreModelBinding.h"
#include "CoreModelNames.h"
#include <math.h>

#define CMG_DESCRIPTOR_LIMIT 36
#define CMG_READ_LIMIT 192
#define CMG_LOD_LIMIT 3
#define CMG_LOD_BYTES 1024
#define CMG_INDEX_BYTES 112
#define CMG_SECTION_BYTES 2048
#define CMG_VERTEX_LIMIT 16384
typedef struct {
    uint64_t asset_address,render_address,lod_address,vertex_address,weight_address,index_address,section_address;
    uint32_t name_index,name_number,internal_index,lod_count,lod_index,asset_index,vertex_count,vertex_stride,weight_stride,section_count;
    uint32_t index_bytes,section_bytes;
    uint8_t index_width;
    bool stream_headers_consistent;
    float bounds_origin[3],bounds_extent[3];bool bounds_shape_valid;
    uint8_t lod[CMG_LOD_BYTES],index[CMG_INDEX_BYTES],sections[CMG_SECTION_BYTES];
} CMGDescriptor;
typedef struct {
    CMGDescriptor descriptors[CMG_DESCRIPTOR_LIMIT];
    uint32_t count,read_attempts,read_failures,rejected;
    bool truncated;
} CMGDescriptors;
static inline bool CMGRange(uint64_t address,size_t size) {
    return address>=0x100000000ULL && address<(1ULL<<39) && !(address&7) && size && size<=1024*1024 && size<=(1ULL<<39)-address;
}
static inline bool CMGRead(CMGDescriptors *out,CMRReader reader,uint64_t address,void *buffer,size_t size) {
    if (!CMGRange(address,size) || size>16384) return false;
    if (out->read_attempts>=CMG_READ_LIMIT) { out->truncated=true;return false; }
    out->read_attempts++;
    if (reader(address,buffer,size)) return true;
    out->read_failures++;return false;
}
static inline void CMGCollect(const CMBBinding *binding,const CMNNames *names,CMRReader reader,CMGDescriptors *out) {
    memset(out,0,sizeof(*out));
    if (!binding || !names || !reader) return;
    for (uint32_t i=0;i<binding->assets.count;i++) {
        const CMRReference *asset=&binding->assets.references[i];
        const char *className=asset->class_header_valid ? CMNText(names,asset->class_header.name_index) : NULL;
        if (!className || strcmp(className,"SkeletalMesh") || asset->prefix_bytes<0x8C) continue;
        if (out->count>=CMG_DESCRIPTOR_LIMIT) { out->truncated=true;break; }
        uint64_t renderAddress=0;memcpy(&renderAddress,asset->prefix+0x38,8);uint8_t render[16]={0};
        if (!CMGRead(out,reader,renderAddress,render,sizeof(render))) continue;
        uint64_t array=0;int32_t count=0,capacity=0;
        memcpy(&array,render,8);memcpy(&count,render+8,4);memcpy(&capacity,render+12,4);
        if (count<1 || count>8 || capacity<count || capacity>8 || !CMGRange(array,(size_t)count*8)) { out->rejected++;continue; }
        uint64_t lods[8]={0};
        if (!CMGRead(out,reader,array,lods,(size_t)count*8)) continue;
        if (count>CMG_LOD_LIMIT) out->truncated=true;
        for (int32_t li=0;li<count && li<CMG_LOD_LIMIT;li++) {
        if (out->count>=CMG_DESCRIPTOR_LIMIT) { out->truncated=true;break; }
        CMGDescriptor *d=&out->descriptors[out->count];memset(d,0,sizeof(*d));
        d->asset_address=asset->address;d->name_index=asset->header.name_index;d->name_number=asset->header.name_number;d->internal_index=(uint32_t)asset->header.internal_index;
        d->asset_index=i;d->render_address=renderAddress;d->lod_count=(uint32_t)count;d->lod_index=(uint32_t)li;d->lod_address=lods[li];
        if (!CMGRead(out,reader,d->lod_address,d->lod,sizeof(d->lod))) continue;
        memcpy(&d->vertex_count,d->lod+0x60,4);memcpy(&d->vertex_stride,d->lod+0xD0,4);memcpy(&d->weight_stride,d->lod+0x150,4);
        uint32_t vertexN=0,weightN=0;memcpy(&vertexN,d->lod+0xD4,4);memcpy(&weightN,d->lod+0x154,4);
        memcpy(&d->vertex_address,d->lod+0xC8,8);memcpy(&d->weight_address,d->lod+0x148,8);
        d->stream_headers_consistent=d->vertex_count && d->vertex_count<=CMG_VERTEX_LIMIT && vertexN==d->vertex_count && weightN==d->vertex_count &&
            d->vertex_stride>=20 && d->vertex_stride<=64 && !(d->vertex_stride&3) &&
            d->weight_stride>=4 && d->weight_stride<=32 && !(d->weight_stride&3) &&
            CMGRange(d->vertex_address,(size_t)d->vertex_count*d->vertex_stride) && CMGRange(d->weight_address,(size_t)d->vertex_count*d->weight_stride);
        if (!d->stream_headers_consistent) { out->rejected++;continue; }
        d->index_width=d->lod[0x50];memcpy(&d->index_address,d->lod+0x58,8);
        if ((d->index_width==2 || d->index_width==4) && CMGRead(out,reader,d->index_address,d->index,sizeof(d->index))) d->index_bytes=sizeof(d->index);
        int32_t sections=0,sectionCap=0;memcpy(&d->section_address,d->lod,8);memcpy(&sections,d->lod+8,4);memcpy(&sectionCap,d->lod+12,4);
        if (sections>0 && sections<=64 && sectionCap>=sections && sectionCap<=128) {
            d->section_count=(uint32_t)sections;size_t bytes=(size_t)sections*256;
            if (bytes>CMG_SECTION_BYTES) bytes=CMG_SECTION_BYTES;
            if (CMGRead(out,reader,d->section_address,d->sections,bytes)) d->section_bytes=(uint32_t)bytes;
        }
        memcpy(d->bounds_origin,asset->prefix+0x70,12);memcpy(d->bounds_extent,asset->prefix+0x7C,12);d->bounds_shape_valid=true;
        for (unsigned k=0;k<3;k++) if (!isfinite(d->bounds_origin[k]) || !isfinite(d->bounds_extent[k]) || d->bounds_extent[k]<0 || fabsf(d->bounds_origin[k])>1e8f || d->bounds_extent[k]>1e8f) d->bounds_shape_valid=false;
        out->count++;
        }
    }
}
static inline bool CMGPositionHypothesis(const CMGDescriptor *d,const uint8_t *data,size_t size) {
    if (!data || !d->bounds_shape_valid || !d->vertex_count || d->vertex_stride<20 || size!=(size_t)d->vertex_count*d->vertex_stride) return false;
    for (uint32_t i=0;i<d->vertex_count;i++) for (uint32_t k=0;k<3;k++) {
        float v=0;memcpy(&v,data+(size_t)i*d->vertex_stride+8+k*4,4);
        float tolerance=fmaxf(0.02f,d->bounds_extent[k]*0.0001f);
        if (!isfinite(v) || fabsf(v-d->bounds_origin[k])>d->bounds_extent[k]+tolerance) return false;
    }
    return true;
}
static inline bool CMGWeightHypothesis(const CMGDescriptor *d,const uint8_t *data,size_t size) {
    if (d->weight_stride!=8 || size!=(size_t)d->vertex_count*8) return false;
    for (uint32_t i=0;i<d->vertex_count;i++) {
        unsigned total=0;for (uint32_t j=4;j<8;j++) total+=data[(size_t)i*8+j];
        if (total!=255) return false;
    }
    return d->vertex_count>0;
}
static inline bool CMGIndexHypothesis(const uint8_t *data,size_t size,uint32_t width,uint32_t vertices) {
    if ((width!=2 && width!=4) || !size || size%width || (size/width)%3) return false;
    for (size_t i=0;i<size;i+=width) {
        uint32_t index=0;memcpy(&index,data+i,width);if (index>=vertices) return false;
    }
    return true;
}
#endif
