#ifndef CORE_MODEL_MESH_EVIDENCE_H
#define CORE_MODEL_MESH_EVIDENCE_H
#include "CoreModelBinding.h"
#include "CoreModelNames.h"
#include "CoreModelCaptureMath.h"

#define CME_RESOURCE_LIMIT 12
#define CME_COMPONENT_LIMIT 16
#define CME_NATIVE_LIMIT 12
#define CME_READ_LIMIT 96
#define CME_REFERENCE_BYTE_LIMIT 32768
#define CME_POSE_BYTE_LIMIT 24576
#define CME_NATIVE_BYTES 1024
typedef struct {
    uint32_t asset_index,count,info_bytes,pose_bytes;
    bool skeletal_class_named,hierarchy_valid,reference_pose_valid;
    uint8_t info[4096],pose[12288];
} CMEResource;
typedef struct {
    uint64_t address;uint32_t offset,count,bytes,identity_matches,unmapped;
    bool index_range_valid,full_identity_match;
    int32_t indices[256];
} CMEMap;
typedef struct {
    uint64_t address;uint32_t offset,count,bytes;
    bool transform_shape_valid;
    uint8_t data[12288];
} CMEPose;
typedef struct {
    uint64_t address;uint32_t asset_index;
    int32_t master_index,master_serial;
    bool master_index_matches_primary,component_transform_valid;
    bool master_index_matches_captured_component;
    uint64_t resolved_master_address;
    uint32_t resolved_master_asset_index;
    CMTransform component_transform;
    CMEMap maps[2];CMEPose poses[2];
} CMEComponent;
typedef struct {
    uint64_t address;uint32_t source_asset_offset,parent_node,array_slot,bytes;
    bool header_shape_valid,class_header_shape_valid;
    CMRHeader header,class_header;
    uint8_t data[CME_NATIVE_BYTES];
} CMENative;
typedef struct {
    CMEResource resources[CME_RESOURCE_LIMIT];
    CMEComponent components[CME_COMPONENT_LIMIT];
    CMENative native[CME_NATIVE_LIMIT];
    uint32_t resource_count,component_count,native_count,read_attempts,read_failures;
    uint32_t reference_bytes,pose_bytes,rejected_arrays;
    bool truncated;
} CMEMeshEvidence;

static inline bool CMERead(CMEMeshEvidence *out,CMRReader reader,uint64_t address,void *data,size_t size) {
    if (!CMBRange(address,size)) return false;
    if (out->read_attempts>=CME_READ_LIMIT) { out->truncated=true;return false; }
    out->read_attempts++;
    if (reader(address,data,size)) return true;
    out->read_failures++;return false;
}
static inline bool CMEArray(const uint8_t *prefix,size_t length,uint32_t offset,uint32_t stride,uint64_t *address,uint32_t *count) {
    if (length<offset+16) return false;
    int32_t n=0,capacity=0;
    memcpy(address,prefix+offset,8);memcpy(&n,prefix+offset+8,4);memcpy(&capacity,prefix+offset+12,4);
    if (n<1 || n>256 || capacity<n || capacity>512 || !CMBRange(*address,(size_t)n*stride)) return false;
    *count=(uint32_t)n;return true;
}
static inline bool CMETransforms(const uint8_t *data,uint32_t count) {
    for (uint32_t i=0;i<count;i++) {
        CMTransform t;memcpy(&t,data+i*48,sizeof(t));
        if (!CMValidTransform(&t)) return false;
    }
    return count>0;
}
static inline const CMEResource *CMEResourceAt(const CMEMeshEvidence *out,uint32_t assetIndex) {
    for (uint32_t i=0;i<out->resource_count;i++) if (out->resources[i].asset_index==assetIndex) return &out->resources[i];
    return NULL;
}
static inline bool CMEBoneIdentity(const CMEResource *a,uint32_t ai,const CMEResource *b,uint32_t bi) {
    // FName number is part of identity. Bag with numbers 2, 3 and 4 are
    // different bones even though the name table returns the same base text.
    return a && b && a->hierarchy_valid && b->hierarchy_valid && ai<a->count && bi<b->count &&
        !memcmp(a->info+ai*16,b->info+bi*16,8);
}
static inline void CMENativeAdd(CMEMeshEvidence *out,CMRReader reader,uint64_t address,uint32_t offset,uint32_t parent,uint32_t slot) {
    if (!CMBRange(address,CME_NATIVE_BYTES)) return;
    for (uint32_t i=0;i<out->native_count;i++) if (out->native[i].address==address) return;
    if (out->native_count>=CME_NATIVE_LIMIT) { out->truncated=true;return; }
    CMENative *node=&out->native[out->native_count];
    memset(node,0,sizeof(*node));
    if (!CMERead(out,reader,address,node->data,sizeof(node->data))) return;
    node->address=address;node->source_asset_offset=offset;node->parent_node=parent;node->array_slot=slot;node->bytes=sizeof(node->data);
    memcpy(&node->header,node->data,sizeof(node->header));node->header_shape_valid=CMRHeaderCandidate(&node->header);
    if (node->header_shape_valid && CMERead(out,reader,node->header.class_address,&node->class_header,sizeof(node->class_header)))
        node->class_header_shape_valid=CMRHeaderCandidate(&node->class_header);
    out->native_count++;
}
static inline void CMECollect(uint64_t primary,const uint8_t *layout,size_t layoutLength,uint32_t poseCount,
                              const CMRReferences *refs,const CMBBinding *binding,const CMNNames *names,
                              CMRReader reader,CMEMeshEvidence *out) {
    (void)poseCount; // A master's reference skeleton need not equal the primary pose count.
    memset(out,0,sizeof(*out));
    if (!refs || !binding || !names || !reader) return;
    const CMEResource *primaryResource=NULL;
    for (uint32_t i=0;i<binding->assets.count;i++) {
        if (out->resource_count>=CME_RESOURCE_LIMIT) { out->truncated=true;break; }
        const CMRReference *asset=&binding->assets.references[i];
        CMEResource *resource=&out->resources[out->resource_count++];resource->asset_index=i;
        const char *className=asset->class_header_valid ? CMNText(names,asset->class_header.name_index) : NULL;
        resource->skeletal_class_named=className && !strcmp(className,"SkeletalMesh");
        if (!resource->skeletal_class_named) continue;
        uint64_t address=0;uint32_t count=0;
        if (!CMEArray(asset->prefix,asset->prefix_bytes,0x220,16,&address,&count)) { out->rejected_arrays++;continue; }
        resource->count=count;
        if (count*16>CME_REFERENCE_BYTE_LIMIT-out->reference_bytes) { out->truncated=true;continue; }
        if (!CMERead(out,reader,address,resource->info,count*16)) continue;
        resource->info_bytes=count*16;out->reference_bytes+=resource->info_bytes;
        resource->hierarchy_valid=CMBHierarchy(resource->info,count,16);
        uint32_t poseN=0;
        if (CMEArray(asset->prefix,asset->prefix_bytes,0x230,48,&address,&poseN) && poseN==count) {
            if (count*48>CME_REFERENCE_BYTE_LIMIT-out->reference_bytes) out->truncated=true;
            else if (CMERead(out,reader,address,resource->pose,count*48)) {
                resource->pose_bytes=count*48;out->reference_bytes+=resource->pose_bytes;
                resource->reference_pose_valid=CMETransforms(resource->pose,count);
            }
        }
        if (i==0) primaryResource=resource;
    }
    CMRHeader primaryHeader={0};
    bool primaryHeaderValid=layout && layoutLength>=sizeof(primaryHeader);
    if (primaryHeaderValid) { memcpy(&primaryHeader,layout,sizeof(primaryHeader));primaryHeaderValid=CMRHeaderCandidate(&primaryHeader); }
    for (uint32_t i=0;i<binding->link_count;i++) {
        const CMBLink *link=&binding->links[i];
        if (link->component==primary || link->asset_index==UINT32_MAX) continue;
        const CMRReference *child=NULL;
        for (uint32_t j=0;j<refs->count;j++) if (refs->references[j].address==link->component) child=&refs->references[j];
        if (!child || child->prefix_bytes<0x838) continue;
        if (out->component_count>=CME_COMPONENT_LIMIT) { out->truncated=true;break; }
        CMEComponent *component=&out->components[out->component_count++];component->address=link->component;component->asset_index=link->asset_index;
        memcpy(&component->master_index,child->prefix+0x830,4);memcpy(&component->master_serial,child->prefix+0x834,4);
        component->master_index_matches_primary=primaryHeaderValid && component->master_index==primaryHeader.internal_index && component->master_serial>0;
        const CMEResource *masterResource=NULL;
        component->resolved_master_asset_index=UINT32_MAX;
        if (component->master_index_matches_primary) {
            component->resolved_master_address=primary;component->resolved_master_asset_index=0;masterResource=primaryResource;
        } else if (component->master_serial>0) {
            for (uint32_t k=0;k<refs->count;k++) {
                const CMRReference *candidate=&refs->references[k];
                if (candidate->address==child->address || candidate->header.internal_index!=component->master_index) continue;
                for (uint32_t m=0;m<binding->link_count;m++) if (binding->links[m].component==candidate->address && binding->links[m].asset_index!=UINT32_MAX) {
                    component->resolved_master_address=candidate->address;component->resolved_master_asset_index=binding->links[m].asset_index;
                    masterResource=CMEResourceAt(out,component->resolved_master_asset_index);
                }
            }
        }
        component->master_index_matches_captured_component=component->resolved_master_address!=0;
        memcpy(&component->component_transform,child->prefix+0x1F0,sizeof(CMTransform));
        component->component_transform_valid=CMValidTransform(&component->component_transform);
        const CMEResource *resource=CMEResourceAt(out,link->asset_index);
        // v6 disproved the old 0x958/0xCD0 int32 map hypotheses. The newly
        // observed 0x8B8 array is tested against full bone identities; the
        // second map is derived independently from those identities.
        const uint32_t mapOffsets[]={0x8B8,UINT32_MAX},poseOffsets[]={0x848,0xBD0};
        for (uint32_t j=0;j<2;j++) {
            CMEMap *map=&component->maps[j];map->offset=mapOffsets[j];
            if (j==0 && component->master_index_matches_captured_component && CMEArray(child->prefix,child->prefix_bytes,map->offset,4,&map->address,&map->count) &&
                CMERead(out,reader,map->address,map->indices,map->count*4)) {
                map->bytes=map->count*4;map->index_range_valid=true;
                for (uint32_t k=0;k<map->count;k++) {
                    int32_t index=map->indices[k];
                    if (index==-1) map->unmapped++;
                    else if (index<0 || !masterResource || (uint32_t)index>=masterResource->count) map->index_range_valid=false;
                    else if (CMEBoneIdentity(resource,k,masterResource,(uint32_t)index)) map->identity_matches++;
                }
                map->full_identity_match=map->index_range_valid && !map->unmapped && resource &&
                    resource->count==map->count && masterResource && map->identity_matches==map->count;
            } else if (j==1 && component->master_index_matches_captured_component && resource && masterResource && resource->hierarchy_valid && masterResource->hierarchy_valid) {
                map->count=resource->count;map->bytes=map->count*4;map->index_range_valid=true;
                for (uint32_t k=0;k<map->count;k++) {
                    int32_t match=-1;uint32_t matches=0;
                    for (uint32_t m=0;m<masterResource->count;m++) if (CMEBoneIdentity(resource,k,masterResource,m)) { match=(int32_t)m;matches++; }
                    map->indices[k]=matches==1 ? match : -1;
                    if (matches==1) map->identity_matches++;else map->unmapped++;
                }
                map->full_identity_match=map->identity_matches==map->count;
            }
            CMEPose *pose=&component->poses[j];pose->offset=poseOffsets[j];
            if (CMEArray(child->prefix,child->prefix_bytes,pose->offset,48,&pose->address,&pose->count) && resource && pose->count==resource->count) {
                if (pose->count*48>CME_POSE_BYTE_LIMIT-out->pose_bytes) out->truncated=true;
                else if (CMERead(out,reader,pose->address,pose->data,pose->count*48)) {
                    pose->bytes=pose->count*48;out->pose_bytes+=pose->bytes;
                    pose->transform_shape_valid=CMETransforms(pose->data,pose->count);
                }
            }
        }
    }
    // Fixed native pointer candidates in the now identified primary SkeletalMesh.
    // Only one bounded array edge is followed for non-UObject native prefixes.
    if (!binding->assets.count) return;
    const CMRReference *root=&binding->assets.references[0];const uint32_t nativeOffsets[]={0x38,0x40,0x1D8};
    for (uint32_t i=0;i<3;i++) if (root->prefix_bytes>=nativeOffsets[i]+8) {
        uint64_t address=0;memcpy(&address,root->prefix+nativeOffsets[i],8);
        CMENativeAdd(out,reader,address,nativeOffsets[i],UINT32_MAX,UINT32_MAX);
    }
    uint32_t rootNodes=out->native_count;
    for (uint32_t i=0;i<rootNodes;i++) {
        const CMENative *node=&out->native[i];if (node->header_shape_valid) continue;
        for (uint32_t offset=0;offset<=16;offset+=8) {
            uint64_t address=0;int32_t count=0,capacity=0;
            memcpy(&address,node->data+offset,8);memcpy(&count,node->data+offset+8,4);memcpy(&capacity,node->data+offset+12,4);
            if (count<1 || count>8 || capacity<count || capacity>16 || !CMBRange(address,(size_t)count*8)) continue;
            uint64_t entries[8]={0};if (!CMERead(out,reader,address,entries,(size_t)count*8)) continue;
            for (int32_t j=0;j<count;j++) CMENativeAdd(out,reader,entries[j],offset,i,(uint32_t)j);
            break;
        }
    }
}
#endif
