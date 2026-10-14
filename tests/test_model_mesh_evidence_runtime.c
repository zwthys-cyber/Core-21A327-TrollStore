// Independent synthetic evidence; run only in GitHub Actions.
#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelMeshEvidence.h"

static const uint64_t Primary=0x200000000ULL,Child=0x200010000ULL,Class=0x200020000ULL;
static const uint64_t InfoA=0x300000000ULL,PoseA=0x300010000ULL,InfoB=0x300020000ULL,PoseB=0x300030000ULL;
static const uint64_t Map=0x300040000ULL,OwnPose=0x300050000ULL;
static const uint64_t Native=0x400000000ULL,Entries=0x400010000ULL,LOD=0x400020000ULL;
static unsigned Calls;
static bool WrongNumber,BadIndex,Unmapped,FailInfo;
static CMRHeader Header(uint32_t name,int32_t index) {
    CMRHeader h={0};h.vtable=0x100010000ULL;h.class_address=Class;h.internal_index=index;h.name_index=name;return h;
}
static bool Reader(uint64_t address,void *out,size_t size) {
    Calls++;memset(out,0,size);
    if ((address==InfoA || address==InfoB) && size==32) {
        if (FailInfo && address==InfoA) return false;
        uint32_t records[]={100,0,UINT32_MAX,0,101,(address==InfoB && WrongNumber)?3U:2U,0,0};
        memcpy(out,records,sizeof(records));return true;
    }
    if ((address==PoseA || address==PoseB || address==OwnPose) && size==96) {
        for (unsigned i=0;i<2;i++) {
            CMTransform t={{0,0,0,1,(float)i*10,0,0,0,1,1,1}};
            memcpy((uint8_t *)out+i*48,&t,sizeof(t));
        }
        return true;
    }
    if (address==Map && size==8) {
        int32_t indices[]={0,BadIndex?99:Unmapped?-1:1};memcpy(out,indices,sizeof(indices));return true;
    }
    if (address==Native && size==1024) {
        memcpy(out,&Entries,8);int32_t count=1;memcpy((uint8_t *)out+8,&count,4);memcpy((uint8_t *)out+12,&count,4);return true;
    }
    if (address==Entries && size==8) { memcpy(out,&LOD,8);return true; }
    if (address==LOD && size==1024) { memset(out,0xAB,size);return true; }
    return false;
}
static void Array(uint8_t *prefix,uint32_t offset,uint64_t address,int32_t count) {
    memcpy(prefix+offset,&address,8);memcpy(prefix+offset+8,&count,4);memcpy(prefix+offset+12,&count,4);
}
int main(void) {
    CMRReferences refs;uint8_t layout[4096]={0};CMRCollect(layout,sizeof(layout),Reader,&refs);
    CMRHeader primary=Header(200,42);memcpy(layout,&primary,sizeof(primary));
    refs.count=1;CMRReference *child=&refs.references[0];child->address=Child;child->source_offset=0x170;child->prefix_bytes=4096;
    int32_t master=42,serial=8;memcpy(child->prefix+0x830,&master,4);memcpy(child->prefix+0x834,&serial,4);
    CMTransform transform={{0,0,0,1,10,20,30,0,1,1,1}};memcpy(child->prefix+0x1F0,&transform,sizeof(transform));
    child->header=Header(201,43);
    Array(child->prefix,0x8B8,Map,2);Array(child->prefix,0x848,OwnPose,2);
    CMBBinding binding={0};binding.assets.count=2;binding.link_count=2;
    binding.links[0]=(CMBLink){Primary,0x500000000ULL,0};binding.links[1]=(CMBLink){Child,0x500010000ULL,1};
    for (unsigned i=0;i<2;i++) {
        CMRReference *asset=&binding.assets.references[i];asset->prefix_bytes=1048;
        asset->class_header_valid=true;asset->class_header=Header(80275,5);
        Array(asset->prefix,0x220,i?InfoB:InfoA,2);Array(asset->prefix,0x230,i?PoseB:PoseA,2);
    }
    memcpy(binding.assets.references[0].prefix+0x38,&Native,8);
    CMNNames names={0};names.count=1;names.names[0].index=80275;names.names[0].valid=true;memcpy(names.names[0].text,"SkeletalMesh",13);
    CMEMeshEvidence evidence;Calls=0;
    CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(evidence.resource_count==2 && evidence.component_count==1 && evidence.native_count==2);
    assert(evidence.read_attempts==9 && Calls==9 && !evidence.read_failures && !evidence.truncated);
    assert(evidence.reference_bytes==256 && evidence.pose_bytes==96);
    assert(evidence.resources[0].hierarchy_valid && evidence.resources[1].reference_pose_valid);
    assert(evidence.components[0].master_index_matches_primary && evidence.components[0].component_transform_valid);
    assert(evidence.components[0].maps[0].full_identity_match && evidence.components[0].maps[0].offset==0x8B8);
    assert(evidence.components[0].maps[1].full_identity_match && evidence.components[0].maps[1].identity_matches==2);
    assert(evidence.components[0].poses[0].transform_shape_valid && evidence.native[1].parent_node==0);

    WrongNumber=true;CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(evidence.components[0].maps[1].index_range_valid && evidence.components[0].maps[1].identity_matches==1 && !evidence.components[0].maps[1].full_identity_match);
    WrongNumber=false;BadIndex=true;CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(!evidence.components[0].maps[0].index_range_valid && evidence.components[0].maps[1].full_identity_match);
    BadIndex=false;Unmapped=true;CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(evidence.components[0].maps[0].unmapped==1 && !evidence.components[0].maps[0].full_identity_match);
    Unmapped=false;FailInfo=true;CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(evidence.read_failures==1 && !evidence.resources[0].hierarchy_valid && !evidence.components[0].maps[1].full_identity_match);
    FailInfo=false;master=43;memcpy(child->prefix+0x830,&master,4);
    CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(!evidence.components[0].master_index_matches_primary && !evidence.components[0].maps[1].bytes);

    // Hair follows the jacket component, which itself follows the primary.
    master=42;memcpy(child->prefix+0x830,&master,4);refs.count=2;
    refs.references[1]=*child;refs.references[1].address=Child+0x10000;
    refs.references[1].header=Header(202,44);master=43;memcpy(refs.references[1].prefix+0x830,&master,4);
    binding.assets.count=3;binding.assets.references[2]=binding.assets.references[1];binding.link_count=3;
    binding.links[2]=(CMBLink){Child+0x10000,0x500020000ULL,2};
    CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(evidence.component_count==2 && !evidence.components[1].master_index_matches_primary);
    assert(evidence.components[1].master_index_matches_captured_component && evidence.components[1].resolved_master_address==Child);
    assert(evidence.components[1].resolved_master_asset_index==1 && evidence.components[1].maps[0].full_identity_match);
    assert(evidence.components[1].maps[1].full_identity_match);

    binding.assets.count=CMR_REFERENCE_LIMIT;
    for (unsigned i=2;i<CMR_REFERENCE_LIMIT;i++) binding.assets.references[i]=binding.assets.references[1];
    CMECollect(Primary,layout,sizeof(layout),2,&refs,&binding,&names,Reader,&evidence);
    assert(evidence.resource_count==CME_RESOURCE_LIMIT && evidence.truncated);
    memset(&evidence,0,sizeof(evidence));evidence.read_attempts=CME_READ_LIMIT-1;uint8_t data[32];Calls=0;
    assert(CMERead(&evidence,Reader,InfoA,data,32));assert(!CMERead(&evidence,Reader,InfoA,data,32));
    assert(evidence.read_attempts==CME_READ_LIMIT && evidence.truncated && Calls==1);
    assert(!CMERead(&evidence,Reader,(1ULL<<39)-8,data,16));
    CMTransform bone={{0,0,0,1,1,2,3,0,1,1,1}};CMVec3 world;
    assert(CMWorldPosition(&transform,&bone,&world) && world.x==11 && world.y==22 && world.z==33);
    puts("PASS: per-resource reference skeletons; full FName identities; primary and attachment master resolution; native and derived maps; matching, missing and invalid bones; bounded child poses and native array edges; failed reads and exact budgets");
    return 0;
}
