// Included only by the opt-in live build, after the bounded capture helpers.
// All mesh reads use the existing Core reader; fills enter its current ImGui
// background list before Render/Metal submission. No executable runtime writes.
#include "CoreModelSkinning.h"
#include "CoreModelPixels.h"
#include "CoreModelReflection.h"
#include <pthread.h>
#include <unistd.h>

#define CML_TARGET_LIMIT 24
#define CML_CACHE_BYTES (24*1024*1024)
#define CML_CACHE_ENTRIES 512
#define CML_PIXEL_LIMIT (4*1024*1024)
#define CML_SPAN_LIMIT 4096
#define CML_RAY_SCRATCH_BYTES (8*1024*1024)
@interface CMLMesh : NSObject
@property(nonatomic) CMGKey key;
@property(nonatomic) uint32_t lod,bones,vertices,vertexStride,sectionCount,indexCount,indexWidth,byteCost;
@property(nonatomic,strong) NSData *vertexData,*weightData,*indexData,*triangleData,*sectionData,*paletteData,*inverseData,*boneInfo,*descriptor;
@end
@implementation CMLMesh
@end
@interface CMLDrawItem : NSObject
@property(nonatomic,strong) CMLMesh *mesh;
@property(nonatomic,strong) NSData *pose;
@property(nonatomic) CMSMatrix world;
@end
@implementation CMLDrawItem
@end
@interface CMLTarget : NSObject
@property(nonatomic) uint64_t component,actor,asset,poseTime,metadataTime,metadataRetryAfter;
@property(nonatomic) uint32_t count,stride;
@property(nonatomic) CMTransform transform;
@property(nonatomic) CMRHeader identity;
@property(nonatomic) bool metadataInvalid;
@property(nonatomic,strong) NSData *pose,*refs,*binding,*names,*evidence,*gpu,*topology;
@property(nonatomic,strong) NSArray<CMLMesh *> *meshes;
@property(nonatomic,strong) NSMutableDictionary<NSNumber *,CMLMesh *> *skeletons;
@end
@implementation CMLTarget
@end
@interface CMLSchemaSnapshot : NSObject
@property(atomic,copy) NSDictionary<NSNumber *,NSData *> *schemas;
@property(atomic,copy) NSArray<NSDictionary *> *meshFailures;
@end
@implementation CMLSchemaSnapshot
@end
@interface CMLSchemaDiscovery : NSObject
@property(nonatomic,strong) NSMutableData *scan,*names;
@property(nonatomic) uint64_t retryAfter;
@property(nonatomic) uint32_t attempts;
@end
@implementation CMLSchemaDiscovery
@end
typedef struct {
    atomic_uint_fast64_t frames,poseUpdates,metadataLoads,metadataFailures,meshLoads,cacheBytes,triangles,white,red,queryCalls,drawFrames,spans;
    atomic_uint_fast64_t staleTargets,unknownVisibility,unverifiedLOD,referenceFallbacks,unknownPose,badRays,budgetFrames,projectionMismatch,oversizedMasks;
    atomic_uint_fast64_t drawTimeTotal,drawTimeMaximum;
    atomic_uint_fast64_t batchPasses,batchParityChecks,batchParityFailures;
    atomic_uint_fast64_t cacheEvictions,cacheRejects,cacheEntries;
    atomic_uint_fast64_t metadataChecks,metadataReuses,metadataDeferred,actorSnapshotReads,queryBudgetFrames,rectangleBudgetFrames;
    atomic_uint_fast64_t eligibleFrames,meshFrames,maskBytesMaximum,metadataTimeTotal,metadataTimeMaximum;
    atomic_uint_fast64_t coveredPixels,gridPixels,coveredMaximum,rayScratchMaximum;
    atomic_uint_fast64_t meshUnavailable;
    atomic_uint_fast64_t meshTimeTotal,meshTimeMaximum,rasterTimeTotal,rasterTimeMaximum,queryTimeTotal,queryTimeMaximum,packTimeTotal,packTimeMaximum,submitTimeTotal,submitTimeMaximum;
    atomic_uint_fast64_t gatherTimeTotal,gatherTimeMaximum,skinTimeTotal,skinTimeMaximum,indexDecodes;
    atomic_uint_fast64_t stateReads,stateBytes,stateBytesMaximum;
    atomic_uint_fast64_t screenRejectedMeshes,lodDescriptorSkips;
    atomic_uint_fast64_t schemaSteps,schemaRetries,schemaTimeTotal,schemaTimeMaximum;
    atomic_uint_fast64_t disabledFrames,geometryUnavailableFrames,cameraUnavailableFrames,actorSnapshotUnavailableFrames;
} CMLCounters;
static CMLCounters CMLStats;
static atomic_bool CMLReady;
static pthread_mutex_t CMLMutex=PTHREAD_MUTEX_INITIALIZER;
static NSMutableArray<CMLTarget *> *CMLTargets;
static NSMutableArray<CMLMesh *> *CMLCache;
static NSArray<CMLMesh *> *CMLLoadingMeshes;
static NSMutableDictionary<NSNumber *,NSData *> *CMLSchemas;
static CMLSchemaSnapshot *CMLSchemaStore;
static NSMutableDictionary<NSNumber *,CMLSchemaDiscovery *> *CMLDiscoveries;
static bool CMLSchemaStepUsed;
static uint64_t CMLReadDeadline;
static uint32_t CMLReads,CMLCachedBytes;
static uintptr_t CMLExpectedContext;
static void (*CMLOriginalDraw)(void *);
static void *(*CMLGetDrawList)(void);
static void (*CMLPolygon)(void *,const CMVec2 *,int,uint32_t);
static void (*CMLIntersect)(void *,CMRayHit *,void *);
static atomic_bool CMLBatchDisabled;
static dispatch_source_t CMLStatusTimer;
static void CMLMaximum(atomic_uint_fast64_t *counter,uint64_t value) {
    uint_fast64_t maximum=atomic_load(counter);while(value>maximum&&!atomic_compare_exchange_weak(counter,&maximum,value)){}
}
static void CMLTime(atomic_uint_fast64_t *total,atomic_uint_fast64_t *maximum,uint64_t start) {
    uint64_t elapsed=CMNow()-start;atomic_fetch_add(total,elapsed);CMLMaximum(maximum,elapsed);
}

static bool CMLRead(uint64_t address,void *out,size_t size) {
    if (!CMRemoteRange(address,size)||CMLReads>=1024||CMNow()>=CMLReadDeadline) return false;
    CMLReads++;return CMReadRemote(address,out,size);
}
static NSData *CMLBytes(uint64_t address,size_t size) {
    if (!size||size>1024*1024||address<0x100000000ULL||address>=(1ULL<<39)||size>(1ULL<<39)-address) return nil;
    NSMutableData *data=[NSMutableData dataWithLength:size];
    for (size_t offset=0;offset<size;) {size_t n=size-offset;if(n>16384)n=16384;if(!CMLRead(address+offset,(uint8_t *)data.mutableBytes+offset,n))return nil;offset+=n;}
    return data;
}
static bool CMLSameObject(CMRHeader a,CMRHeader b) {
    return CMRHeaderCandidate(&a)&&CMRHeaderCandidate(&b)&&a.internal_index==b.internal_index&&a.class_address==b.class_address&&
        a.name_index==b.name_index&&a.name_number==b.name_number&&a.outer_address==b.outer_address;
}
static NSData *CMLTopology(const uint8_t *layout) {
    NSMutableData *stamp=[NSMutableData data];const uint32_t offsets[]={0x170,0x7D0},limits[]={16,4};
    for(unsigned i=0;i<2;i++){
        uint64_t address=0;int32_t count=0,capacity=0;memcpy(&address,layout+offsets[i],8);memcpy(&count,layout+offsets[i]+8,4);memcpy(&capacity,layout+offsets[i]+12,4);
        if(count<0||capacity<count||capacity>128||(uint32_t)count>limits[i])return nil;
        [stamp appendBytes:layout+offsets[i] length:16];
        if(count){NSData *entries=CMLBytes(address,(size_t)count*8);if(!entries)return nil;[stamp appendData:entries];}
    }
    return stamp;
}
static bool CMLSameMeshDescriptor(const CMGDescriptor *a,const CMGDescriptor *b) {
    CMGKey ka=CMGKeyFor(a),kb=CMGKeyFor(b);size_t sections=(size_t)a->section_count*224;
    return CMGKeyEqual(&ka,&kb)&&a->lod_count==b->lod_count&&a->lod_index==b->lod_index&&
        a->index_width==b->index_width&&a->index_address==b->index_address&&a->index_bytes>=0x58&&b->index_bytes>=0x58&&
        !memcmp(a->index+0x48,b->index+0x48,16)&&a->section_address==b->section_address&&a->section_count==b->section_count&&
        sections<=a->section_bytes&&sections<=b->section_bytes&&!memcmp(a->sections,b->sections,sections);
}
static bool CMLSameDescriptor(const CMGDescriptor *a,const CMGDescriptor *b) {return a->asset_index==b->asset_index&&CMLSameMeshDescriptor(a,b);}
// Validate cached topology/resource/stream identities without re-reading bone
// names, reference poses or vertex buffers every second. Animation stays live.
static bool CMLReuseMetadata(CMLTarget *target) {
    if(!target.topology||target.binding.length!=sizeof(CMBBinding)||target.gpu.length!=sizeof(CMGDescriptors)||target.refs.length!=sizeof(CMRReferences)||target.names.length!=sizeof(CMNNames))return false;
    CMLReadDeadline=CMNow()+12000000ULL;CMLReads=0;NSData *layout=CMLBytes(target.component,0x830);if(!layout)return false;
    CMRHeader root;memcpy(&root,layout.bytes,sizeof(root));if(!CMLSameObject(root,target.identity))return false;
    NSData *topology=CMLTopology(layout.bytes);if(!topology||![topology isEqualToData:target.topology])return false;
    const CMBBinding *binding=target.binding.bytes;const CMRReferences *refs=target.refs.bytes;
    for(uint32_t i=0;i<binding->link_count;i++){
        const CMBLink *link=&binding->links[i];if(link->asset_index==UINT32_MAX)continue;uint64_t asset=0;
        if(link->component==target.component)memcpy(&asset,(const uint8_t *)layout.bytes+0x828,8);
        else {const CMRReference *old=NULL;for(uint32_t j=0;j<refs->count;j++)if(refs->references[j].address==link->component)old=&refs->references[j];
            CMRHeader header;if(!old||!CMLRead(link->component,&header,sizeof(header))||!CMLSameObject(header,old->header)||!CMLRead(link->component+0x828,&asset,8))return false;}
        if(asset!=link->address)return false;
    }
    for(uint32_t i=0;i<binding->assets.count;i++){
        const CMRReference *old=&binding->assets.references[i];uint8_t current[64];CMRHeader header;
        if(old->prefix_bytes<sizeof(current)||!CMLRead(old->address,current,sizeof(current)))return false;memcpy(&header,current,sizeof(header));
        if(!CMLSameObject(header,old->header)||memcmp(current+0x38,old->prefix+0x38,8))return false;
    }
    NSMutableData *storage=[NSMutableData dataWithLength:sizeof(CMGDescriptors)];CMGCollect(binding,target.names.bytes,CMLRead,storage.mutableBytes);
    const CMGDescriptors *fresh=storage.bytes,*old=target.gpu.bytes;
    if(fresh->read_failures||fresh->count!=old->count||fresh->rejected!=old->rejected||fresh->truncated!=old->truncated)return false;
    for(uint32_t i=0;i<fresh->count;i++)if(!CMLSameDescriptor(&fresh->descriptors[i],&old->descriptors[i]))return false;
    target.gpu=storage;target.metadataTime=CMNow();return true;
}
static void CMLObserve(uint64_t component,uint64_t actor,uint64_t transformAddress,uint64_t poseAddress,uint32_t count,uint32_t stride) {
    if (!atomic_load(&CMLReady)||!CMRemoteRange(actor,8)) return;
    uint64_t now=CMNow();pthread_mutex_lock(&CMLMutex);
    CMLTarget *target=nil;
    for (CMLTarget *t in CMLTargets) if(t.component==component&&t.actor==actor)target=t;
    if (!target) {
        if (CMLTargets.count>=CML_TARGET_LIMIT) {CMLTarget *old=CMLTargets.firstObject;for(CMLTarget *t in CMLTargets)if(t.poseTime<old.poseTime)old=t;[CMLTargets removeObject:old];}
        target=[CMLTarget new];target.component=component;target.actor=actor;[CMLTargets addObject:target];
    }
    if (target.poseTime&&now-target.poseTime<50000000ULL) {pthread_mutex_unlock(&CMLMutex);return;}
    pthread_mutex_unlock(&CMLMutex);
    CMTransform transform;uint64_t asset=0;NSMutableData *pose=[NSMutableData dataWithLength:(size_t)count*stride];
    if (!CMReadRemote(transformAddress,&transform,sizeof(transform))||!CMValidTransform(&transform)||
        !CMReadRemote(component+0x828,&asset,8)||!CMRPointer(asset,40)||!CMReadRemote(poseAddress,pose.mutableBytes,pose.length)) return;
    for(uint32_t i=0;i<count;i++){CMTransform t;memcpy(&t,(uint8_t *)pose.bytes+i*stride,sizeof(t));if(!CMValidTransform(&t))return;}
    pthread_mutex_lock(&CMLMutex);
    if(target.asset!=asset||target.count!=count){target.meshes=nil;target.metadataTime=0;target.metadataRetryAfter=0;}
    target.asset=asset;target.count=count;target.stride=stride;target.pose=pose;target.transform=transform;target.poseTime=CMNow();atomic_fetch_add(&CMLStats.poseUpdates,1);
    pthread_mutex_unlock(&CMLMutex);
}
static CMFSchema CMLSchemaFor(const CMRReference *component,const CMNNames *source) {
    NSNumber *key=@(component->header.class_address);NSData *cached=CMLSchemas[key];CMFSchema schema={0};if(cached.length==sizeof(schema))memcpy(&schema,cached.bytes,sizeof(schema));
    CMLSchemaDiscovery *discovery=CMLDiscoveries[key];if((cached&&!discovery)||CMLSchemaStepUsed)return schema;
    if(!discovery){if(CMLSchemas.count>=32)return schema;discovery=[CMLSchemaDiscovery new];CMLDiscoveries[key]=discovery;}
    uint64_t now=CMNow();if(now<discovery.retryAfter)return schema;CMLSchemaStepUsed=true;
    if(!discovery.scan){discovery.scan=[NSMutableData dataWithLength:sizeof(CMFScan)];CMFBegin(discovery.scan.mutableBytes,component->header.class_address);
        discovery.names=[NSMutableData dataWithBytes:source length:sizeof(*source)];CMNNames *names=discovery.names.mutableBytes;
        names->count=0;names->read_attempts=0;names->read_failures=0;names->rejected_text=0;names->truncated=false;discovery.attempts++;}
    uint64_t savedDeadline=CMLReadDeadline,start=CMNow();uint32_t savedReads=CMLReads;
    CMLReadDeadline=start+2000000ULL;CMLReads=0;CMFScan *scan=discovery.scan.mutableBytes;
    bool finished=CMFStep(scan,CMLRead,discovery.names.mutableBytes,16);uint64_t elapsed=CMNow()-start;
    // Discovery has its own small window. Preserve the remaining data-read
    // budget instead of resetting it, or charging earlier/later part reads.
    CMLReadDeadline=savedDeadline+elapsed;CMLReads=savedReads;
    atomic_fetch_add(&CMLStats.schemaSteps,1);atomic_fetch_add(&CMLStats.schemaTimeTotal,elapsed);CMLMaximum(&CMLStats.schemaTimeMaximum,elapsed);
    CMFSchema fresh=scan->schema;
    if(!fresh.visible.found)fresh.visible=schema.visible;if(!fresh.hidden.found)fresh.hidden=schema.hidden;if(!fresh.hide_skin.found)fresh.hide_skin=schema.hide_skin;
    if(!fresh.forced_found&&schema.forced_found){fresh.forced_found=true;fresh.forced_lod=schema.forced_lod;}
    if(!fresh.predicted_found&&schema.predicted_found){fresh.predicted_found=true;fresh.predicted_lod=schema.predicted_lod;fresh.predicted_source=schema.predicted_source;fresh.predicted_getter=schema.predicted_getter;}
    if(!fresh.min_found&&schema.min_found){fresh.min_found=true;fresh.min_lod=schema.min_lod;}
    CMLSchemas[key]=[NSData dataWithBytes:&fresh length:sizeof(fresh)];CMLSchemaStore.schemas=CMLSchemas;
    if(finished){if((fresh.failures||fresh.name_failures)&&discovery.attempts<3){discovery.scan=nil;discovery.names=nil;discovery.retryAfter=CMNow()+1000000000ULL;atomic_fetch_add(&CMLStats.schemaRetries,1);}
        else [CMLDiscoveries removeObjectForKey:key];}
    return fresh;
}
static void CMLTrimStaleTargets(uint64_t now) {
    for(CMLTarget *target in CMLTargets)if(target.poseTime&&now-target.poseTime>2000000000ULL){
        target.meshes=nil;target.skeletons=nil;target.refs=nil;target.binding=nil;target.names=nil;target.evidence=nil;target.gpu=nil;target.topology=nil;target.metadataTime=0;
    }
}
static bool CMLMakeCacheRoom(size_t bytes) {
    if(bytes>CML_CACHE_BYTES)return false;
    while(CMLCachedBytes>CML_CACHE_BYTES-bytes||CMLCache.count>=CML_CACHE_ENTRIES){
        CMLMesh *oldest=nil;for(CMLMesh *mesh in CMLCache){bool pinned=[CMLLoadingMeshes containsObject:mesh];
            for(CMLTarget *target in CMLTargets)if([target.meshes containsObject:mesh]){pinned=true;break;}
            if(!pinned){oldest=mesh;break;}}
        if(!oldest){atomic_fetch_add(&CMLStats.cacheRejects,1);return false;}
        CMLCachedBytes-=oldest.byteCost;[CMLCache removeObjectIdenticalTo:oldest];atomic_fetch_add(&CMLStats.cacheEvictions,1);atomic_store(&CMLStats.cacheBytes,CMLCachedBytes);atomic_store(&CMLStats.cacheEntries,CMLCache.count);
    }
    atomic_store(&CMLStats.cacheBytes,CMLCachedBytes);atomic_store(&CMLStats.cacheEntries,CMLCache.count);return true;
}
typedef enum {CML_LOAD_REFERENCE,CML_LOAD_LAYOUT,CML_LOAD_INDEX_HEADER,CML_LOAD_CACHE,
    CML_LOAD_VERTEX_READ,CML_LOAD_WEIGHT_READ,CML_LOAD_INDEX_READ,CML_LOAD_WEIGHT_SHAPE,CML_LOAD_INDEX_SHAPE,
    CML_LOAD_SECTION,CML_LOAD_PALETTE_READ,CML_LOAD_PALETTE_RANGE,CML_LOAD_WEIGHT_RANGE,CML_LOAD_SECTION_INDICES,
    CML_LOAD_COVERAGE,CML_LOAD_INVERSE,CML_LOAD_READ_BUDGET,CML_LOAD_POSITION_SHAPE,CML_LOAD_REASON_COUNT} CMLLoadFailure;
static atomic_uint_fast64_t CMLLoadFailures[CML_LOAD_REASON_COUNT];
static atomic_uint_fast64_t CMLLoadedStrides[3];
static const char *CMLFailureNames[]={"reference","layout","index_header","cache_limit","vertex_read","weight_read","index_read","weight_shape","index_shape","section","palette_read","palette_range","weight_range","section_indices","range_coverage","inverse_reference","read_budget","position_shape"};
_Static_assert(sizeof(CMLFailureNames)/sizeof(CMLFailureNames[0])==CML_LOAD_REASON_COUNT,"Failure names match counters");
static CMLLoadFailure CMLLastLoadFailure;
static NSMutableArray<NSDictionary *> *CMLFailureRecords;
static uint64_t CMLFailureKeys[16][9];static uint32_t CMLFailureKeyCount;
static atomic_bool CMLFailureSamplesTruncated;
static CMLMesh *CMLLoadFailed(CMLLoadFailure reason) {CMLLastLoadFailure=reason;atomic_fetch_add(&CMLLoadFailures[reason],1);return nil;}
static CMLMesh *CMLLoadReadFailed(CMLLoadFailure reason) {return CMLLoadFailed(CMLReads>=1024||CMNow()>=CMLReadDeadline?CML_LOAD_READ_BUDGET:reason);}
static void CMLRecordLoadFailure(CMLTarget *target,const CMGDescriptor *d) {
    uint64_t key[9]={d->asset_address,d->render_address,d->lod_address,(uint64_t)d->name_index|((uint64_t)d->name_number<<32),
        (uint64_t)d->internal_index|((uint64_t)d->lod_index<<32),(uint64_t)CMLLastLoadFailure|((uint64_t)d->index_width<<32),
        (uint64_t)d->vertex_stride|((uint64_t)d->weight_stride<<32),(uint64_t)d->section_count|((uint64_t)d->index_bytes<<32),(uint64_t)d->section_bytes|((uint64_t)d->vertex_count<<32)};
    for(uint32_t i=0;i<CMLFailureKeyCount;i++)if(!memcmp(key,CMLFailureKeys[i],sizeof(key)))return;
    if(CMLFailureKeyCount==16){atomic_store(&CMLFailureSamplesTruncated,true);return;}
    memcpy(CMLFailureKeys[CMLFailureKeyCount++],key,sizeof(key));const char *name=target.names.length==sizeof(CMNNames)?CMNText(target.names.bytes,d->name_index):NULL;
    NSString *reason=[NSString stringWithUTF8String:CMLFailureNames[CMLLastLoadFailure]],*detail=reason;
    if(CMLLastLoadFailure==CML_LOAD_LAYOUT){
        if(d->vertex_stride!=24&&d->vertex_stride!=28&&d->vertex_stride!=32)detail=@"vertex_stride";
        else if(d->weight_stride!=8)detail=@"weight_stride";
        else if(!d->section_count)detail=@"section_count_zero";
        else if(d->section_count>8)detail=@"section_count_limit";
        else if(d->index_bytes<88)detail=@"index_prefix_incomplete";}
    [CMLFailureRecords addObject:@{@"reason":reason,@"detail":detail,@"asset_name":name?[NSString stringWithUTF8String:name]:@"unresolved",@"asset_name_index":@(d->name_index),@"asset_name_number":@(d->name_number),@"asset_internal_index":@(d->internal_index),
        @"target_component":@(target.component),@"asset_address":@(d->asset_address),@"render_address":@(d->render_address),@"lod_address":@(d->lod_address),@"lod_index":@(d->lod_index),@"asset_index":@(d->asset_index),
        @"vertex_stride":@(d->vertex_stride),@"weight_stride":@(d->weight_stride),@"vertex_count":@(d->vertex_count),@"section_count":@(d->section_count),@"index_width":@(d->index_width),@"index_prefix_bytes":@(d->index_bytes),@"section_prefix_bytes":@(d->section_bytes),
        @"vertex_address":@(d->vertex_address),@"weight_address":@(d->weight_address),@"index_descriptor_address":@(d->index_address),@"section_address":@(d->section_address)}];
    CMLSchemaStore.meshFailures=CMLFailureRecords;
}
static CMLMesh *CMLLoadMesh(const CMGDescriptor *d,const CMEResource *resource) {
    CMGKey key=CMGKeyFor(d);CMLMesh *cached=nil;for(CMLMesh *mesh in CMLCache){CMGKey existing=mesh.key;if(CMGKeyEqual(&existing,&key)&&mesh.descriptor.length==sizeof(*d)&&CMLSameMeshDescriptor(mesh.descriptor.bytes,d)){cached=mesh;break;}}
    if(cached){[CMLCache removeObjectIdenticalTo:cached];[CMLCache addObject:cached];return cached;}
    if(!resource||!resource->hierarchy_valid||!resource->reference_pose_valid)return CMLLoadFailed(CML_LOAD_REFERENCE);
    if((d->vertex_stride!=24&&d->vertex_stride!=28&&d->vertex_stride!=32)||d->weight_stride!=8||!d->section_count||d->section_count>8||d->index_bytes<88)return CMLLoadFailed(CML_LOAD_LAYOUT);
    uint64_t indexAddress=0;uint32_t indexCount=0;
    if(!CMGArrayCandidate(d->index,d->index_bytes,0x48,d->index_width,d->vertex_count*18,&indexAddress,&indexCount)||indexCount%3)return CMLLoadFailed(CML_LOAD_INDEX_HEADER);
    size_t bytes=(size_t)d->vertex_count*(d->vertex_stride+8)+(size_t)indexCount*(d->index_width+4)+(size_t)resource->count*sizeof(CMSMatrix)+resource->info_bytes+8192+sizeof(*d);
    if(!CMLMakeCacheRoom(bytes))return CMLLoadFailed(CML_LOAD_CACHE);
    NSData *vertices=CMLBytes(d->vertex_address,(size_t)d->vertex_count*d->vertex_stride);if(!vertices)return CMLLoadReadFailed(CML_LOAD_VERTEX_READ);
    // The newly observed 28/32-byte streams keep float3 position at +8.
    // Accept them only when every decoded position agrees with asset bounds.
    if(d->vertex_stride!=24&&!CMGPositionHypothesis(d,vertices.bytes,vertices.length))return CMLLoadFailed(CML_LOAD_POSITION_SHAPE);
    NSData *weights=CMLBytes(d->weight_address,(size_t)d->vertex_count*8);if(!weights)return CMLLoadReadFailed(CML_LOAD_WEIGHT_READ);
    NSData *indices=CMLBytes(indexAddress,(size_t)indexCount*d->index_width);if(!indices)return CMLLoadReadFailed(CML_LOAD_INDEX_READ);
    if(!CMGWeightHypothesis(d,weights.bytes,weights.length))return CMLLoadFailed(CML_LOAD_WEIGHT_SHAPE);
    if(!CMGIndexHypothesis(indices.bytes,indices.length,d->index_width,d->vertex_count))return CMLLoadFailed(CML_LOAD_INDEX_SHAPE);
    NSMutableData *triangles=[NSMutableData dataWithLength:(size_t)indexCount*4];uint32_t *decoded=triangles.mutableBytes;const uint8_t *rawIndices=indices.bytes;
    for(uint32_t i=0;i<indexCount;i++)memcpy(&decoded[i],rawIndices+(size_t)i*d->index_width,d->index_width);atomic_fetch_add(&CMLStats.indexDecodes,indexCount);
    NSMutableData *sections=[NSMutableData dataWithLength:d->section_count*sizeof(CMSSection)],*palettes=[NSMutableData dataWithLength:d->section_count*512],*inverse=[NSMutableData dataWithLength:resource->count*sizeof(CMSMatrix)];
    CMSSection *sec=sections.mutableBytes;uint32_t nextV=0,nextI=0;
    for(uint32_t j=0;j<d->section_count;j++) {
        if(!CMSParseSection(d->sections,d->section_bytes,j,d->vertex_count,indexCount,&sec[j])||sec[j].first_vertex!=nextV||sec[j].first_index!=nextI)return CMLLoadFailed(CML_LOAD_SECTION);
        NSData *map=CMLBytes(sec[j].bone_address,sec[j].bone_count*2);if(!map)return CMLLoadReadFailed(CML_LOAD_PALETTE_READ);uint16_t *palette=(uint16_t *)((uint8_t *)palettes.mutableBytes+j*512);memcpy(palette,map.bytes,map.length);
        for(uint32_t k=0;k<sec[j].bone_count;k++)if(palette[k]>=resource->count)return CMLLoadFailed(CML_LOAD_PALETTE_RANGE);
        for(uint32_t k=sec[j].first_vertex;k<sec[j].first_vertex+sec[j].vertex_count;k++)for(unsigned a=0;a<4;a++) {
            const uint8_t *w=(const uint8_t *)weights.bytes+k*8;if(w[a+4]&&w[a]>=sec[j].bone_count)return CMLLoadFailed(CML_LOAD_WEIGHT_RANGE);
        }
        for(uint32_t k=sec[j].first_index;k<sec[j].first_index+sec[j].triangle_count*3;k++){uint32_t v=decoded[k];if(v<sec[j].first_vertex||v>=sec[j].first_vertex+sec[j].vertex_count)return CMLLoadFailed(CML_LOAD_SECTION_INDICES);}
        nextV+=sec[j].vertex_count;nextI+=sec[j].triangle_count*3;
    }
    if(nextV!=d->vertex_count||nextI!=indexCount)return CMLLoadFailed(CML_LOAD_COVERAGE);
    if(!CMSReferenceInverse(resource->info,resource->pose,resource->count,inverse.mutableBytes))return CMLLoadFailed(CML_LOAD_INVERSE);
    CMLMesh *mesh=[CMLMesh new];mesh.key=key;mesh.lod=d->lod_index;mesh.bones=resource->count;mesh.vertices=d->vertex_count;mesh.vertexStride=d->vertex_stride;mesh.sectionCount=d->section_count;mesh.indexCount=indexCount;mesh.indexWidth=d->index_width;
    mesh.vertexData=vertices;mesh.weightData=weights;mesh.indexData=indices;mesh.triangleData=triangles;mesh.sectionData=sections;mesh.paletteData=palettes;mesh.inverseData=inverse;mesh.boneInfo=[NSData dataWithBytes:resource->info length:resource->info_bytes];
    mesh.descriptor=[NSData dataWithBytes:d length:sizeof(*d)];
    mesh.byteCost=(uint32_t)bytes;CMLCachedBytes+=(uint32_t)bytes;[CMLCache addObject:mesh];atomic_fetch_add(&CMLStats.meshLoads,1);atomic_fetch_add(&CMLLoadedStrides[(d->vertex_stride-24)/4],1);atomic_store(&CMLStats.cacheBytes,CMLCachedBytes);atomic_store(&CMLStats.cacheEntries,CMLCache.count);return mesh;
}
static bool CMLRefresh(CMLTarget *target) {
    CMLReadDeadline=CMNow()+100000000ULL;CMLReads=0;NSData *layout=CMLBytes(target.component,4096);if(!layout)return false;
    NSData *topology=CMLTopology(layout.bytes);
    NSMutableData *refs=[NSMutableData dataWithLength:sizeof(CMRReferences)],*binding=[NSMutableData dataWithLength:sizeof(CMBBinding)],*names=[NSMutableData dataWithLength:sizeof(CMNNames)],*evidence=[NSMutableData dataWithLength:sizeof(CMEMeshEvidence)],*gpu=[NSMutableData dataWithLength:sizeof(CMGDescriptors)];
    CMRCollect(layout.bytes,layout.length,CMLRead,refs.mutableBytes);CMBCollect(target.component,refs.bytes,CMLRead,binding.mutableBytes);CMCollectNames(refs.bytes,binding.bytes,names.mutableBytes);
    CMECollect(target.component,layout.bytes,layout.length,target.count,refs.bytes,binding.bytes,names.bytes,CMLRead,evidence.mutableBytes);
    CMGCollect(binding.bytes,names.bytes,CMLRead,gpu.mutableBytes);const CMGDescriptors *descriptors=gpu.bytes;const CMBBinding *b=binding.bytes;
    const CMEResource *root=CMEResourceAt(evidence.bytes,0);
    if(!b->assets.count||b->assets.references[0].address!=target.asset||!descriptors->count||!root||!root->hierarchy_valid||!root->reference_pose_valid)return false;
    CMRHeader identity;memcpy(&identity,layout.bytes,sizeof(identity));
    target.identity=identity;target.topology=topology;target.refs=refs;target.binding=binding;target.names=names;target.evidence=evidence;target.gpu=gpu;target.meshes=@[];target.skeletons=[NSMutableDictionary dictionary];target.metadataTime=CMNow();atomic_fetch_add(&CMLStats.metadataLoads,1);return true;
}
static bool CMLHasMetadata(CMLTarget *target) {return target.gpu.length==sizeof(CMGDescriptors)&&((const CMGDescriptors *)target.gpu.bytes)->count>0&&!target.metadataInvalid;}
static bool CMLPrepareMetadata(CMLTarget *target,uint64_t now,bool *checked) {
    if(target.metadataTime&&now-target.metadataTime<=1000000000ULL)return CMLHasMetadata(target);
    if(*checked||now<target.metadataRetryAfter){atomic_fetch_add(&CMLStats.metadataDeferred,1);return CMLHasMetadata(target);}
    *checked=true;uint64_t start=CMNow();atomic_fetch_add(&CMLStats.metadataChecks,1);
    bool ready=CMLReuseMetadata(target);
    if(ready)atomic_fetch_add(&CMLStats.metadataReuses,1);else ready=CMLRefresh(target);
    target.metadataInvalid=!ready;target.metadataRetryAfter=ready?0:CMNow()+1000000000ULL;
    if(!ready)atomic_fetch_add(&CMLStats.metadataFailures,1);
    uint64_t elapsed=CMNow()-start;atomic_fetch_add(&CMLStats.metadataTimeTotal,elapsed);uint_fast64_t maximum=atomic_load(&CMLStats.metadataTimeMaximum);
    while(elapsed>maximum&&!atomic_compare_exchange_weak(&CMLStats.metadataTimeMaximum,&maximum,elapsed)){}
    return ready;
}
// Select visibility/LOD before reading static buffers. Keep other LODs in the
// bounded LRU cache only if they have actually been used, rather than eagerly
// loading every LOD of every hidden outfit part on each metadata refresh.
static CMLMesh *CMLLiveMesh(CMLTarget *target,const CMGDescriptor *descriptor) {
    for(CMLMesh *mesh in target.meshes)if(mesh.lod==descriptor->lod_index&&mesh.key.asset==descriptor->asset_address)return mesh;
    if(target.evidence.length!=sizeof(CMEMeshEvidence))return nil;
    CMLMesh *mesh=CMLLoadMesh(descriptor,CMEResourceAt(target.evidence.bytes,descriptor->asset_index));
    if(!mesh){atomic_fetch_add(&CMLStats.meshUnavailable,1);CMLRecordLoadFailure(target,descriptor);return nil;}
    NSMutableArray *selected=[NSMutableArray array];for(CMLMesh *old in target.meshes)if(old.key.asset!=descriptor->asset_address)[selected addObject:old];[selected addObject:mesh];target.meshes=selected;return mesh;
}

// A per-frame state cache. Raw component prefixes and own pose arrays are read
// again; geometry caching does not freeze animation, attachment transforms or
// master maps. Recursive masters are checked by live component internal index.
static const CMRReference *CMLComponent(CMLTarget *target,uint32_t assetIndex) {
    const CMBBinding *b=target.binding.bytes;const CMRReferences *r=target.refs.bytes;
    for(uint32_t i=0;i<b->link_count;i++)if(b->links[i].asset_index==assetIndex) {
        for(uint32_t j=0;j<r->count;j++)if(r->references[j].address==b->links[i].component)return &r->references[j];
    }
    return NULL;
}
static size_t CMLStateBytes(uint64_t classAddress,size_t maximum) {
    NSData *data=CMLSchemas[@(classAddress)];if(data.length!=sizeof(CMFSchema))return maximum;
    CMFSchema schema;memcpy(&schema,data.bytes,sizeof(schema));size_t needed=0x8C8;CMFBool flags[]={schema.visible,schema.hidden,schema.hide_skin};
    for(unsigned i=0;i<3;i++)if(flags[i].found&&needed<(size_t)flags[i].offset+1)needed=(size_t)flags[i].offset+1;
    const uint32_t offsets[]={schema.forced_lod,schema.predicted_lod,schema.min_lod};const bool found[]={schema.forced_found,schema.predicted_found,schema.min_found};
    for(unsigned i=0;i<3;i++)if(found[i]&&needed<(size_t)offsets[i]+4)needed=(size_t)offsets[i]+4;
    needed=(needed+15)&~(size_t)15;return needed<maximum?needed:maximum;
}
static NSData *CMLState(CMLTarget *target,uint32_t id,NSMutableDictionary *states) {
    NSData *cached=states[@(id)];if(cached)return cached;uint64_t address=target.component,classAddress=target.identity.class_address;size_t bytes=4096;
    const CMRReference *child=NULL;
    if(id){child=CMLComponent(target,id);if(!child||child->prefix_bytes<0x8C8)return nil;address=child->address;bytes=child->prefix_bytes;classAddress=child->header.class_address;}
    bytes=CMLStateBytes(classAddress,bytes);
    NSData *state=CMLBytes(address,bytes);if(!state)return nil;const CMBBinding *b=target.binding.bytes;
    atomic_fetch_add(&CMLStats.stateReads,1);atomic_fetch_add(&CMLStats.stateBytes,bytes);CMLMaximum(&CMLStats.stateBytesMaximum,bytes);
    if(child){CMRHeader header;memcpy(&header,state.bytes,sizeof(header));if(!CMLSameObject(header,child->header)){target.metadataTime=0;return nil;}}
    uint64_t asset=0;memcpy(&asset,(const uint8_t *)state.bytes+0x828,8);
    if(id>=b->assets.count||asset!=b->assets.references[id].address){target.metadataTime=0;return nil;}
    if(!id){CMRHeader header;memcpy(&header,state.bytes,sizeof(header));NSData *topology=CMLTopology(state.bytes);
        if(!CMLSameObject(header,target.identity)||(target.topology&&(!topology||![topology isEqualToData:target.topology]))){target.metadataTime=0;return nil;}}
    states[@(id)]=state;return state;
}
static CMLMesh *CMLMeshFor(CMLTarget *target,uint32_t id) {
    const CMBBinding *b=target.binding.bytes;if(id>=b->assets.count)return nil;
    for(CMLMesh *m in target.meshes)if(m.key.asset==b->assets.references[id].address)return m;
    CMLMesh *cached=target.skeletons[@(id)];if(cached)return cached;
    if(target.evidence.length!=sizeof(CMEMeshEvidence))return nil;const CMEResource *resource=CMEResourceAt(target.evidence.bytes,id);
    if(!resource||!resource->hierarchy_valid||!resource->reference_pose_valid||!resource->count||resource->count>256||resource->info_bytes!=resource->count*16||resource->pose_bytes!=resource->count*48)return nil;
    NSMutableData *inverse=[NSMutableData dataWithLength:resource->count*sizeof(CMSMatrix)];if(!CMSReferenceInverse(resource->info,resource->pose,resource->count,inverse.mutableBytes))return nil;
    CMLMesh *skeleton=[CMLMesh new];skeleton.bones=resource->count;skeleton.inverseData=inverse;skeleton.boneInfo=[NSData dataWithBytes:resource->info length:resource->info_bytes];
    if(!target.skeletons)target.skeletons=[NSMutableDictionary dictionary];target.skeletons[@(id)]=skeleton;return skeleton;
}
static NSData *CMLPoseMatrices(CMLTarget *target,uint32_t id,NSMutableDictionary *states,NSMutableDictionary *poses,uint32_t visiting) {
    NSData *cached=poses[@(id)];if(cached)return cached;if(id>=16||(visiting&(1U<<id)))return nil;
    CMLMesh *mesh=CMLMeshFor(target,id);if(!mesh)return nil;
    NSMutableData *data=[NSMutableData dataWithLength:mesh.bones*sizeof(CMSMatrix)];CMSMatrix *out=data.mutableBytes;
    if(!id){
        NSData *state=CMLState(target,0,states);uint64_t address=0;uint32_t count=0;
        if(!state||target.stride!=48||!CMEArray(state.bytes,state.length,0x848,48,&address,&count)||count!=mesh.bones)return nil;
        NSData *current=CMLBytes(address,count*48);if(!current)return nil;
        for(uint32_t i=0;i<count;i++){CMTransform t;memcpy(&t,(const uint8_t *)current.bytes+(size_t)i*48,sizeof(t));if(!CMSFromTransform(&t,&out[i]))return nil;}
    }
    else {
        NSData *state=CMLState(target,id,states);if(!state)return nil;const uint8_t *raw=state.bytes;int32_t master=-1,serial=0;memcpy(&master,raw+0x830,4);memcpy(&serial,raw+0x834,4);
        int32_t masterAsset=-1;const CMBBinding *b=target.binding.bytes;const CMRReferences *r=target.refs.bytes;
        if(serial>0){NSData *root=CMLState(target,0,states);CMRHeader h;if(root){memcpy(&h,root.bytes,sizeof(h));if(h.internal_index==master)masterAsset=0;}
            for(uint32_t j=0;j<r->count&&masterAsset<0;j++)if(r->references[j].header.internal_index==master)for(uint32_t k=0;k<b->link_count;k++)if(b->links[k].component==r->references[j].address&&b->links[k].asset_index!=UINT32_MAX)masterAsset=(int32_t)b->links[k].asset_index;
        }
        if(masterAsset>=0){NSData *source=CMLPoseMatrices(target,(uint32_t)masterAsset,states,poses,visiting|(1U<<id));CMLMesh *parent=CMLMeshFor(target,(uint32_t)masterAsset);uint64_t address=0;uint32_t count=0;
            if(!source||!parent||!CMEArray(raw,state.length,0x8B8,4,&address,&count)||count!=mesh.bones)return nil;NSData *map=CMLBytes(address,count*4);if(!map)return nil;
            const int32_t *indices=map.bytes;const CMSMatrix *parentPose=source.bytes;
            for(uint32_t i=0;i<count;i++){int32_t k=indices[i];if(k==-1){if(!CMSInverse(((const CMSMatrix *)mesh.inverseData.bytes)[i],&out[i]))return nil;atomic_fetch_add(&CMLStats.referenceFallbacks,1);}
                else if(k<0||(uint32_t)k>=parent.bones||memcmp((const uint8_t *)mesh.boneInfo.bytes+i*16,(const uint8_t *)parent.boneInfo.bytes+k*16,8))return nil;else out[i]=parentPose[k];}
        } else {uint64_t address=0;uint32_t count=0;if(!CMEArray(raw,state.length,0x848,48,&address,&count)||count!=mesh.bones)return nil;NSData *own=CMLBytes(address,count*48);if(!own)return nil;for(uint32_t i=0;i<count;i++){CMTransform t;memcpy(&t,(const uint8_t *)own.bytes+i*48,sizeof(t));if(!CMSFromTransform(&t,&out[i]))return nil;}}
    }
    poses[@(id)]=data;return data;
}
typedef struct {void *scenes[3];uint64_t deadline;} CMLQueryContext;
static bool CMLQueryPixel(void *context,const CMVec3 *camera,const CMVec3 *world,bool *blocked) {
    CMLQueryContext *q=context;CMVec3 delta={world->x-camera->x,world->y-camera->y,world->z-camera->z};float distance=sqrtf(CMPDot(delta,delta));
    if(!isfinite(distance)||distance<0.001f)return false;*blocked=false;
    for(unsigned i=0;i<3;i++){if(CMNow()>=q->deadline)return false;CMRayHit hit=CMRayFunctions[i](q->scenes[i],camera,world);atomic_fetch_add(&CMLStats.queryCalls,1);
        if(!isfinite(hit.far_distance)||hit.far_distance<0||hit.far_distance>distance+1){atomic_fetch_add(&CMLStats.badRays,1);return false;}
        if(hit.geometry_id!=UINT32_MAX){*blocked=true;return true;}}
    return true;
}
// E0BE8/E0CF0 both take scene+0x58's shared lock, use a 96-byte
// ray and call 2AD2A0(scene->handle at +8, &ray, NULL). Keep the same
// operations, but take each scene lock once for its complete pixel pass.
// Scene locks are never held together. The outer original scene-list lock
// remains held by the caller throughout, exactly as in D33F4.
typedef struct {uint32_t pixel;CMVec3 direction;float distance;} CMLPixelRay;
static bool CMLPreparePixelRay(CMPMask *mask,const CMPView *view,uint32_t pixel,int x,int y,CMLPixelRay *ray) {
    CMVec3 world;if(!CMPWorldAtPixel(view,x,y,mask->pixels[pixel].inverse_depth,&world))return false;
    CMVec3 delta={world.x-view->position.x,world.y-view->position.y,world.z-view->position.z};float distance=sqrtf(CMPDot(delta,delta));
    if(!isfinite(distance)||distance<.001f)return false;*ray=(CMLPixelRay){pixel,{delta.x/distance,delta.y/distance,delta.z/distance},distance};return true;
}
static bool CMLClassifyPixels(CMPMask *mask,const CMPView *view,const CMPCoverage *coverage,CMLQueryContext *q,uint32_t *white,uint32_t *red) {
    if(!CMLIntersect||atomic_load(&CMLBatchDisabled))return CMPClassifyCovered(mask,view,coverage,CMLQueryPixel,q,white,red);
    CMVec3 camera=view->position;
    *white=*red=0;
    // Build only covered rays, once per frame. Larger silhouettes retain the
    // same queries without a scratch allocation above this fixed byte cap.
    size_t rayBytes=(size_t)coverage->count*sizeof(CMLPixelRay);NSMutableData *storage=rayBytes&&rayBytes<=CML_RAY_SCRATCH_BYTES?[NSMutableData dataWithLength:rayBytes]:nil;CMLPixelRay *rays=storage.mutableBytes;
    if(rays){CMPCursor cursor={0};uint32_t p;int x,y;uint32_t n=0;
        while(CMPNextCovered(mask,coverage,&cursor,&p,&x,&y)){if(n>=coverage->count||(!(n&63)&&CMNow()>=q->deadline)||!CMLPreparePixelRay(mask,view,p,x,y,&rays[n]))return false;n++;}
        if(n!=coverage->count)return false;CMLMaximum(&CMLStats.rayScratchMaximum,rayBytes);}
    for(unsigned scene=0;scene<3;scene++){
        CMPCursor firstCursor={0};uint32_t first=UINT32_MAX,p;int x,y;
        while(CMPNextCovered(mask,coverage,&firstCursor,&p,&x,&y))if(mask->pixels[p].colour!=2){first=p;break;}
        if(first==UINT32_MAX)break;if(CMNow()>=q->deadline)return false;
        // One comparison per scene/frame against the unchanged original
        // wrapper also checks this ABI on device. A discrepancy falls back to
        // the original pixel path and never treats unknown pixels as white.
        CMVec3 firstWorld;if(!CMPWorldAtPixel(view,x,y,mask->pixels[first].inverse_depth,&firstWorld))return false;
        CMRayHit reference=CMRayFunctions[scene](q->scenes[scene],&camera,&firstWorld);
        atomic_fetch_add(&CMLStats.queryCalls,1);atomic_fetch_add(&CMLStats.batchParityChecks,1);
        uintptr_t address=(uintptr_t)q->scenes[scene];if(address>UINTPTR_MAX-0x60)return false;
        void *lock=(void *)(address+0x58),*handle=NULL;CMLock(lock);
        bool complete=CMSelfRead(address+8,&handle,sizeof(handle)),mismatch=false;uint64_t calls=0;
        CMPCursor cursor={0};uint32_t n=0;
        while(complete&&n<coverage->count){CMLPixelRay prepared;uint32_t pixelIndex;
            if(rays){prepared=rays[n++];pixelIndex=prepared.pixel;}
            else {if(!CMPNextCovered(mask,coverage,&cursor,&pixelIndex,&x,&y)){complete=false;break;}n++;}
            CMPPixel *pixel=&mask->pixels[pixelIndex];if(pixel->colour==2)continue;
            if(!(calls&63)&&CMNow()>=q->deadline){complete=false;break;}
            if(!rays&&!CMLPreparePixelRay(mask,view,pixelIndex,x,y,&prepared)){complete=false;break;}float distance=prepared.distance;
            CMRayHit hit __attribute__((aligned(16)))={0};
            hit.origin[0]=camera.x;hit.origin[1]=camera.y;hit.origin[2]=camera.z;
            hit.direction[0]=prepared.direction.x;hit.direction[1]=prepared.direction.y;hit.direction[2]=prepared.direction.z;
            hit.far_distance=distance;hit.mask=UINT32_MAX;hit.geometry_id=UINT32_MAX;
            if(handle){CMLIntersect(handle,&hit,NULL);calls++;}
            if(!isfinite(hit.far_distance)||hit.far_distance<0||hit.far_distance>distance+1){atomic_fetch_add(&CMLStats.badRays,1);complete=false;break;}
            if(pixelIndex==first&&(reference.geometry_id!=hit.geometry_id||!isfinite(reference.far_distance)||fabsf(reference.far_distance-hit.far_distance)>fmaxf(.001f,distance*1e-5f))){mismatch=true;complete=false;break;}
            if(hit.geometry_id!=UINT32_MAX)pixel->colour=2;
        }
        CMUnlock(lock);atomic_fetch_add(&CMLStats.queryCalls,calls);atomic_fetch_add(&CMLStats.batchPasses,1);
        if(mismatch){atomic_fetch_add(&CMLStats.batchParityFailures,1);atomic_store(&CMLBatchDisabled,true);
            return CMPClassifyCovered(mask,view,coverage,CMLQueryPixel,q,white,red);}
        if(!complete)return false;
    }
    CMPCursor cursor={0};uint32_t p;int x,y;while(CMPNextCovered(mask,coverage,&cursor,&p,&x,&y)){CMPPixel *pixel=&mask->pixels[p];if(pixel->colour==2)(*red)++;else{pixel->colour=1;(*white)++;}}
    return (uint64_t)*white+*red==coverage->count;
}
static void CMLDrawRectangle(void *list,CMPRectangle r) {
    CMVec2 p[4]={{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}};CMLPolygon(list,p,4,r.colour==2 ? 0xFF0000FFU : 0xFFFFFFFFU);
}
static NSData *CMLReadActors(uintptr_t context,uint32_t *localTeam) {
    uint64_t begin=0,end=0;
    if(!CMSelfRead(context+0x3F0,&begin,8)||!CMSelfRead(context+0x3F8,&end,8)||!CMSelfRead(context+0x3A8,localTeam,4)||end<begin||(end-begin)%0x1F8||(end-begin)/0x1F8>512)return nil;
    NSMutableData *data=[NSMutableData dataWithLength:end-begin];
    for(size_t offset=0;offset<data.length;){size_t chunk=data.length-offset;if(chunk>16384)chunk=16384;if(!CMSelfRead(begin+offset,(uint8_t *)data.mutableBytes+offset,chunk))return nil;offset+=chunk;atomic_fetch_add(&CMLStats.actorSnapshotReads,1);}
    uint64_t final[2]={0};if(!CMSelfRead(context+0x3F0,final,sizeof(final))||final[0]!=begin||final[1]!=end)return nil;return data;
}
static bool CMLActorEligible(NSData *actors,uint32_t localTeam,uint64_t actor) {
    const uint8_t *data=actors.bytes;for(size_t p=0;p<actors.length;p+=0x1F8){uint64_t id=0;uint32_t team=0;float distance=0;
        memcpy(&id,data+p,8);if(id!=actor)continue;memcpy(&team,data+p+8,4);memcpy(&distance,data+p+0x40,4);
        return team!=localTeam&&isfinite(distance)&&distance>0&&distance<=600;}
    return false;
}
__attribute__((noinline)) static void CMLDraw(uintptr_t context) {
    atomic_fetch_add(&CMLStats.frames,1);if(!atomic_load(&CMLReady)||context!=CMLExpectedContext||CMInside)return;
    uint8_t enabled=0,ready=0;float size[2];uint8_t rawView[52];
    if(!CMSelfRead(context+0x23,&enabled,1)||!enabled){atomic_fetch_add(&CMLStats.disabledFrames,1);return;}
    if(!CMSelfRead(CMBase+0xC54459,&ready,1)||ready!=1){atomic_fetch_add(&CMLStats.geometryUnavailableFrames,1);return;}
    CMPView view;if(!CMSelfRead(context+4,size,8)||!CMSelfRead(context+0x1F4,rawView,52)||!CMPMakeView(rawView,size[0],size[1],&view)){atomic_fetch_add(&CMLStats.cameraUnavailableFrames,1);return;}
    uint64_t drawStart=CMNow();atomic_fetch_add(&CMLStats.eligibleFrames,1);
    CMInside=true;pthread_mutex_lock(&CMLMutex);
    CMLSchemaStepUsed=false;
    @autoreleasepool {
        NSMutableArray<NSData *> *projected=[NSMutableArray array],*triangleArrays=[NSMutableArray array],*validArrays=[NSMutableArray array];NSMutableArray<CMLDrawItem *> *items=[NSMutableArray array];bool refreshed=false;uint64_t now=CMNow();uint32_t localTeam=0;NSData *actors=CMLReadActors(context,&localTeam);
        if(!actors)atomic_fetch_add(&CMLStats.actorSnapshotUnavailableFrames,1);
        float minX=view.width,minY=view.height,maxX=0,maxY=0;CMLTrimStaleTargets(now);uint64_t meshStart=CMNow();
        for(CMLTarget *target in CMLTargets){
            if(!target.poseTime||now-target.poseTime>250000000ULL){atomic_fetch_add(&CMLStats.staleTargets,1);continue;}
            if(!actors||!CMLActorEligible(actors,localTeam,target.actor))continue;
            if(!CMLPrepareMetadata(target,now,&refreshed))continue;
            CMLReadDeadline=CMNow()+12000000ULL;CMLReads=0;
            const CMGDescriptors *gpu=target.gpu.bytes;const CMNNames *names=target.names.bytes;
            NSMutableDictionary *states=[NSMutableDictionary dictionary],*poses=[NSMutableDictionary dictionary];
            uint32_t selectedLOD[16]={0},selectionReady=0,selectionUnverified=0;
            if(!CMLState(target,0,states))continue;
            for(uint32_t di=0;di<gpu->count;di++){
                const CMGDescriptor *d=&gpu->descriptors[di];if(d->asset_index>=16)continue;uint32_t bit=1U<<d->asset_index;
                if((selectionReady&bit)&&selectedLOD[d->asset_index]!=d->lod_index){atomic_fetch_add(&CMLStats.lodDescriptorSkips,1);continue;}
                NSData *state=CMLState(target,d->asset_index,states);if(!state)continue;
                if(!(selectionReady&bit)){
                    const CMRReference *component=CMLComponent(target,d->asset_index);CMRReference root={0};
                    if(!d->asset_index){memcpy(&root.header,state.bytes,sizeof(root.header));root.address=target.component;component=&root;}if(!component)continue;
                    CMFSchema schema=CMLSchemaFor(component,names);bool visible=true,visibilityKnown=CMFVisibility(&schema,state.bytes,state.length,&visible);
                    selectionReady|=bit;selectedLOD[d->asset_index]=UINT32_MAX;if(visibilityKnown&&!visible)continue;
                    if(!visibilityKnown){atomic_fetch_add(&CMLStats.unknownVisibility,1);const char *name=CMNText(names,d->name_index);if(name&&(strstr(name,"Parachute")||strstr(name,"Wingsuit")||strstr(name,"Glide")||strstr(name,"Smoke")))continue;}
                    bool lodKnown=false;selectedLOD[d->asset_index]=CMFLOD(&schema,state.bytes,state.length,d->lod_count,&lodKnown);if(!lodKnown)selectionUnverified|=bit;
                    if(selectedLOD[d->asset_index]!=d->lod_index){atomic_fetch_add(&CMLStats.lodDescriptorSkips,1);continue;}
                }
                if(selectionUnverified&bit)atomic_fetch_add(&CMLStats.unverifiedLOD,1);
                CMLMesh *mesh=CMLLiveMesh(target,d);if(!mesh)continue;
                NSData *pose=CMLPoseMatrices(target,d->asset_index,states,poses,0);if(!pose){atomic_fetch_add(&CMLStats.unknownPose,1);continue;}
                CMTransform componentTransform;memcpy(&componentTransform,(const uint8_t *)state.bytes+0x1F0,sizeof(componentTransform));CMSMatrix world;if(!CMSFromTransform(&componentTransform,&world))continue;
                CMLDrawItem *item=[CMLDrawItem new];item.mesh=mesh;item.pose=pose;item.world=world;[items addObject:item];
            }
        }
        // Remote state/poses for every selected part are collected before CPU
        // skinning. CPU work on an early part must not expire later parts'
        // remote-read deadline and make their meshes disappear.
        CMLTime(&CMLStats.gatherTimeTotal,&CMLStats.gatherTimeMaximum,meshStart);uint64_t skinStart=CMNow();
        for(CMLDrawItem *item in items){
            CMLMesh *mesh=item.mesh;uint32_t bones=mesh.bones,vertices=mesh.vertices,vertexStride=mesh.vertexStride,sections=mesh.sectionCount,indexCount=mesh.indexCount;
            NSData *triangles=mesh.triangleData;if(triangles.length!=(size_t)indexCount*4)continue;
            const CMSMatrix *pose=item.pose.bytes,*inverse=mesh.inverseData.bytes;const uint8_t *vertex=mesh.vertexData.bytes,*weight=mesh.weightData.bytes,*palette=mesh.paletteData.bytes;const CMSSection *sec=mesh.sectionData.bytes;CMSMatrix world=item.world;
            NSMutableData *skinData=[NSMutableData dataWithLength:bones*sizeof(CMSMatrix)];CMSMatrix *skin=skinData.mutableBytes;
            for(uint32_t i=0;i<bones;i++)skin[i]=CMSMultiply(CMSMultiply(world,pose[i]),inverse[i]);
            uint8_t known[256];memset(known,1,bones);NSMutableData *points=[NSMutableData dataWithLength:vertices*sizeof(CMPVertex)],*valid=[NSMutableData dataWithLength:vertices];CMPVertex *p=points.mutableBytes;uint8_t *ok=valid.mutableBytes;bool projectionOK=true;
            float meshMinX=view.width,meshMinY=view.height,meshMaxX=0,meshMaxY=0;
            for(uint32_t j=0;j<sections&&projectionOK;j++)for(uint32_t i=sec[j].first_vertex;i<sec[j].first_vertex+sec[j].vertex_count;i++){
                CMVec3 position;CMSResult result=CMSSkinVertex(vertex+(size_t)i*vertexStride,weight+(size_t)i*8,(const uint16_t *)(palette+(size_t)j*512),sec[j].bone_count,skin,known,bones,&position);
                if(result!=CMS_OK||!CMPProject(&view,position,&p[i]))continue;
                if(i%128==0){CMVec2 native=CMProject(&position,(const void *)(context+0x1F4),(int)size[0],(int)size[1]);if(!isfinite(native.x)||!isfinite(native.y)||fabsf(native.x-p[i].xy.x)>0.75f||fabsf(native.y-p[i].xy.y)>0.75f){projectionOK=false;break;}}
                ok[i]=1;meshMinX=fminf(meshMinX,p[i].xy.x);meshMinY=fminf(meshMinY,p[i].xy.y);meshMaxX=fmaxf(meshMaxX,p[i].xy.x);meshMaxY=fmaxf(meshMaxY,p[i].xy.y);
            }
            if(!projectionOK){atomic_fetch_add(&CMLStats.projectionMismatch,1);continue;}
            // Bounds come from all of this frame's successfully projected
            // vertices. A complete mesh outside one viewport half-plane
            // cannot cover a screen pixel; straddling meshes remain intact.
            if(meshMaxX<=0||meshMinX>=view.width||meshMaxY<=0||meshMinY>=view.height){atomic_fetch_add(&CMLStats.screenRejectedMeshes,1);continue;}
            minX=fminf(minX,meshMinX);minY=fminf(minY,meshMinY);maxX=fmaxf(maxX,meshMaxX);maxY=fmaxf(maxY,meshMaxY);
            [projected addObject:points];[triangleArrays addObject:triangles];[validArrays addObject:valid];atomic_fetch_add(&CMLStats.meshFrames,1);
        }
        CMLTime(&CMLStats.skinTimeTotal,&CMLStats.skinTimeMaximum,skinStart);
        CMLTime(&CMLStats.meshTimeTotal,&CMLStats.meshTimeMaximum,meshStart);
        int x=(int)fmaxf(0,floorf(minX)),y=(int)fmaxf(0,floorf(minY)),right=(int)fminf(view.width,ceilf(maxX)),bottom=(int)fminf(view.height,ceilf(maxY));
        if(right>x&&bottom>y&&projected.count){size_t count=(size_t)(right-x)*(bottom-y);if(count>CML_PIXEL_LIMIT)atomic_fetch_add(&CMLStats.oversizedMasks,1);else {
            uint64_t rasterStart=CMNow();
            uint64_t bytes=count*sizeof(CMPPixel);uint_fast64_t maximum=atomic_load(&CMLStats.maskBytesMaximum);while(bytes>maximum&&!atomic_compare_exchange_weak(&CMLStats.maskBytesMaximum,&maximum,bytes)){}
            NSMutableData *pixels=[NSMutableData dataWithLength:bytes];CMPMask mask={x,y,right-x,bottom-y,pixels.mutableBytes};uint64_t triangles=0;
            uint32_t rowWords=((uint32_t)mask.width+63)/64;NSMutableData *coverageData=[NSMutableData dataWithLength:(size_t)rowWords*mask.height*8];CMPCoverage coverage={coverageData.mutableBytes,rowWords,0};
            for(NSUInteger j=0;j<projected.count;j++){const CMPVertex *points=projected[j].bytes;NSData *data=triangleArrays[j];const uint32_t *tris=data.bytes;size_t triangleCount=data.length/12;const uint8_t *valid=validArrays[j].bytes;
                for(size_t i=0;i<triangleCount;i++){uint32_t a=tris[i*3],b=tris[i*3+1],c=tris[i*3+2];if(!valid[a]||!valid[b]||!valid[c])continue;CMPVertex t[3]={points[a],points[b],points[c]};if(CMPRasterTriangleCovered(&mask,t,&coverage))triangles++;}}atomic_fetch_add(&CMLStats.triangles,triangles);
            atomic_fetch_add(&CMLStats.gridPixels,count);atomic_fetch_add(&CMLStats.coveredPixels,coverage.count);CMLMaximum(&CMLStats.coveredMaximum,coverage.count);CMLTime(&CMLStats.rasterTimeTotal,&CMLStats.rasterTimeMaximum,rasterStart);
            uint64_t queryStart=CMNow();
            CMLQueryContext query={{NULL,NULL,NULL},CMNow()+12000000ULL};bool complete=false;uint32_t white=0,red=0;
            if(CMLock&&CMUnlock){CMLock((void *)(CMBase+0xBD4F78));if(CMSelfRead(CMBase+0xC15EC8,query.scenes,sizeof(query.scenes))&&query.scenes[0]&&query.scenes[1]&&query.scenes[2])complete=CMLClassifyPixels(&mask,&view,&coverage,&query,&white,&red);CMUnlock((void *)(CMBase+0xBD4F78));}
            CMLTime(&CMLStats.queryTimeTotal,&CMLStats.queryTimeMaximum,queryStart);uint64_t packStart=CMNow();
            NSMutableData *rectangles=[NSMutableData dataWithLength:CML_SPAN_LIMIT*sizeof(CMPRectangle)];uint32_t spans=complete ? CMPRectanglesCovered(&mask,&coverage,rectangles.mutableBytes,CML_SPAN_LIMIT) : 0;
            CMLTime(&CMLStats.packTimeTotal,&CMLStats.packTimeMaximum,packStart);uint64_t submitStart=CMNow();
            if(complete&&spans&&spans<=CML_SPAN_LIMIT){void *list=CMLGetDrawList();uint8_t flags=0;if(list&&CMSelfRead((uintptr_t)list+0x30,&flags,1)){
                *(uint8_t *)((uintptr_t)list+0x30)=flags&~4U;for(uint32_t i=0;i<spans;i++)CMLDrawRectangle(list,((const CMPRectangle *)rectangles.bytes)[i]);*(uint8_t *)((uintptr_t)list+0x30)=flags;
                atomic_fetch_add(&CMLStats.drawFrames,1);atomic_fetch_add(&CMLStats.white,white);atomic_fetch_add(&CMLStats.red,red);atomic_fetch_add(&CMLStats.spans,spans);}}
            else if(!complete||spans>CML_SPAN_LIMIT){atomic_fetch_add(&CMLStats.budgetFrames,1);if(!complete)atomic_fetch_add(&CMLStats.queryBudgetFrames,1);else atomic_fetch_add(&CMLStats.rectangleBudgetFrames,1);}
            CMLTime(&CMLStats.submitTimeTotal,&CMLStats.submitTimeMaximum,submitStart);
        }}
    }
    pthread_mutex_unlock(&CMLMutex);CMInside=false;
    uint64_t elapsed=CMNow()-drawStart;atomic_fetch_add(&CMLStats.drawTimeTotal,elapsed);uint_fast64_t maximum=atomic_load(&CMLStats.drawTimeMaximum);
    while(elapsed>maximum&&!atomic_compare_exchange_weak(&CMLStats.drawTimeMaximum,&maximum,elapsed)){}
}
__attribute__((noinline)) uintptr_t CoreModelRenderDispatch(void *context,uintptr_t entryReturn) {
    int saved=errno;uintptr_t caller=CMStripReturn((void *)entryReturn),base=CMBase;
    if(!base)base=(uintptr_t)_dyld_get_image_header(0);
    if(caller==base+0x21BC4||caller==base+0x2263C){
        void (*original)(void *)=CMLOriginalDraw ?: (void (*)(void *))CMCallable(base+0xDB144);original(context);CMLDraw((uintptr_t)context);errno=saved;return 0;
    }
    uintptr_t result=(uintptr_t)getpid();errno=saved;return result;
}
#if defined(__arm64__) && !defined(CORE_MODEL_CAPTURE_TESTING)
__attribute__((naked)) pid_t cmdraw(void) { __asm__ volatile("mov x1, x30\nb _CoreModelRenderDispatch\n"); }
#endif
static void CMLWriteStatus(NSURL *url) {
#define CML_VALUE(field) @(atomic_load(&CMLStats.field))
    NSMutableDictionary *loadFailures=[NSMutableDictionary dictionary];for(unsigned i=0;i<CML_LOAD_REASON_COUNT;i++)loadFailures[[NSString stringWithUTF8String:CMLFailureNames[i]]]=@(atomic_load(&CMLLoadFailures[i]));
    NSMutableArray *schemas=[NSMutableArray array];NSDictionary *published=CMLSchemaStore.schemas ?: @{};
    for(NSNumber *address in published){CMFSchema s;memcpy(&s,((NSData *)published[address]).bytes,sizeof(s));
        [schemas addObject:@{@"class_address":address,@"classes_walked":@(s.classes),@"fields_walked":@(s.fields),@"read_attempts":@(s.reads),@"read_failures":@(s.failures),@"truncated":@(s.truncated),
            @"scan_finished":@(s.scan_finished),@"scan_steps":@(s.steps),@"field_limit_reached":@(s.field_limit_reached),@"read_limit_reached":@(s.read_limit_reached),@"name_reads":@(s.name_reads),@"name_read_failures":@(s.name_failures),@"name_limit_reached":@(s.name_limit_reached),
            @"visible_found":@(s.visible.found),@"visible_offset":@(s.visible.offset),@"visible_mask":@(s.visible.mask),@"hidden_found":@(s.hidden.found),@"hidden_offset":@(s.hidden.offset),@"hidden_mask":@(s.hidden.mask),
            @"hide_skin_found":@(s.hide_skin.found),@"hide_skin_offset":@(s.hide_skin.offset),@"hide_skin_mask":@(s.hide_skin.mask),
            @"forced_lod_found":@(s.forced_found),@"forced_lod_offset":@(s.forced_lod),@"predicted_lod_found":@(s.predicted_found),@"predicted_lod_offset":@(s.predicted_lod),@"predicted_lod_source":@(s.predicted_source),@"predicted_lod_getter":@(s.predicted_getter),
            @"class_names":({NSMutableArray *a=[NSMutableArray array];for(uint32_t j=0;j<s.classes&&j<12;j++)[a addObject:[NSString stringWithUTF8String:s.class_names[j]]];a;}),
            @"class_fields_walked":({NSMutableArray *a=[NSMutableArray array];for(uint32_t j=0;j<s.classes&&j<12;j++)[a addObject:@(s.class_fields[j])];a;})}];}
    NSDictionary *status=@{ @"render_version":@9,@"date":NSDate.date.description,@"frames_observed":CML_VALUE(frames),@"pose_updates":CML_VALUE(poseUpdates),
        @"eligible_draw_frames":CML_VALUE(eligibleFrames),@"projected_mesh_frames":CML_VALUE(meshFrames),@"actor_snapshot_chunks":CML_VALUE(actorSnapshotReads),
        @"toggle_disabled_frames":CML_VALUE(disabledFrames),@"geometry_unavailable_frames":CML_VALUE(geometryUnavailableFrames),@"camera_unavailable_frames":CML_VALUE(cameraUnavailableFrames),@"actor_snapshot_unavailable_frames":CML_VALUE(actorSnapshotUnavailableFrames),
        @"metadata_loads":CML_VALUE(metadataLoads),@"metadata_failures":CML_VALUE(metadataFailures),@"mesh_loads":CML_VALUE(meshLoads),@"mesh_cache_bytes":CML_VALUE(cacheBytes),
        @"metadata_checks":CML_VALUE(metadataChecks),@"metadata_reuses":CML_VALUE(metadataReuses),@"metadata_deferred_targets":CML_VALUE(metadataDeferred),@"metadata_time_total_ns":CML_VALUE(metadataTimeTotal),@"metadata_time_maximum_ns":CML_VALUE(metadataTimeMaximum),
        @"rasterized_triangles":CML_VALUE(triangles),@"pixel_query_calls":CML_VALUE(queryCalls),@"filled_frames":CML_VALUE(drawFrames),@"white_pixels":CML_VALUE(white),@"red_pixels":CML_VALUE(red),@"filled_spans":CML_VALUE(spans),
        @"filled_rectangles":CML_VALUE(spans),@"fill_packing":@"exact-identical-row-runs",@"mask_pixel_bytes":@(sizeof(CMPPixel)),@"mask_bytes_maximum":CML_VALUE(maskBytesMaximum),
        @"mask_grid_pixels":CML_VALUE(gridPixels),@"mask_covered_pixels":CML_VALUE(coveredPixels),@"mask_covered_pixels_maximum":CML_VALUE(coveredMaximum),@"coverage_index":@"exact-row-bitset",@"prepared_ray_bytes_maximum":CML_VALUE(rayScratchMaximum),@"prepared_ray_byte_limit":@CML_RAY_SCRATCH_BYTES,
        @"mesh_loading_policy":@"visible-selected-lod-only",@"mesh_loads_unavailable":CML_VALUE(meshUnavailable),
        @"mesh_load_failure_reasons":loadFailures,@"static_index_values_decoded":CML_VALUE(indexDecodes),@"geometry_loop_policy":@"cached-pointers-and-static-triangle-indices",@"pose_read_policy":@"gather-before-skinning",
        @"supported_vertex_strides":@[@24,@28,@32],@"mesh_vertex_stride_loads":@{@"24":@(atomic_load(&CMLLoadedStrides[0])),@"28":@(atomic_load(&CMLLoadedStrides[1])),@"32":@(atomic_load(&CMLLoadedStrides[2]))},
        @"component_state_reads":CML_VALUE(stateReads),@"component_state_bytes":CML_VALUE(stateBytes),@"component_state_bytes_maximum":CML_VALUE(stateBytesMaximum),@"component_state_policy":@"reflected-required-prefix-with-full-prefix-fallback",
        @"mesh_load_failure_samples":CMLSchemaStore.meshFailures?:@[],@"mesh_load_failure_sample_limit":@16,@"mesh_load_failure_samples_truncated":@(atomic_load(&CMLFailureSamplesTruncated)),
        @"empty_or_offscreen_meshes_skipped":CML_VALUE(screenRejectedMeshes),@"unselected_lod_descriptors_skipped":CML_VALUE(lodDescriptorSkips),@"screen_cull_source":@"current-projected-vertex-bounds",@"lod_selection_scope":@"once-per-asset-per-frame",
        @"schema_discovery_policy":@"one-16-node-step-per-frame-with-independent-2ms-window",@"schema_discovery_steps":CML_VALUE(schemaSteps),@"schema_discovery_retries":CML_VALUE(schemaRetries),@"schema_discovery_time_total_ns":CML_VALUE(schemaTimeTotal),@"schema_discovery_time_maximum_ns":CML_VALUE(schemaTimeMaximum),
        @"stale_targets_skipped":CML_VALUE(staleTargets),@"unknown_visibility_samples":CML_VALUE(unknownVisibility),@"unverified_lod_samples":CML_VALUE(unverifiedLOD),@"master_reference_pose_fallback_bones":CML_VALUE(referenceFallbacks),
        @"unknown_pose_meshes":CML_VALUE(unknownPose),@"malformed_pixel_queries":CML_VALUE(badRays),@"budget_frames_skipped":CML_VALUE(budgetFrames),@"projection_mismatch_meshes":CML_VALUE(projectionMismatch),@"oversized_masks_skipped":CML_VALUE(oversizedMasks),
        @"query_frames_incomplete":CML_VALUE(queryBudgetFrames),@"rectangle_budget_frames_skipped":CML_VALUE(rectangleBudgetFrames),
        @"draw_time_total_ns":CML_VALUE(drawTimeTotal),@"draw_time_maximum_ns":CML_VALUE(drawTimeMaximum),@"reflected_component_schemas":schemas,@"schema_snapshot_busy":@NO,@"schema_snapshot_source":@"immutable-discovery-snapshot",
        @"mesh_stage_time_total_ns":CML_VALUE(meshTimeTotal),@"mesh_stage_time_maximum_ns":CML_VALUE(meshTimeMaximum),@"raster_stage_time_total_ns":CML_VALUE(rasterTimeTotal),@"raster_stage_time_maximum_ns":CML_VALUE(rasterTimeMaximum),
        @"gather_stage_time_total_ns":CML_VALUE(gatherTimeTotal),@"gather_stage_time_maximum_ns":CML_VALUE(gatherTimeMaximum),@"skinning_stage_time_total_ns":CML_VALUE(skinTimeTotal),@"skinning_stage_time_maximum_ns":CML_VALUE(skinTimeMaximum),
        @"query_stage_time_total_ns":CML_VALUE(queryTimeTotal),@"query_stage_time_maximum_ns":CML_VALUE(queryTimeMaximum),@"packing_stage_time_total_ns":CML_VALUE(packTimeTotal),@"packing_stage_time_maximum_ns":CML_VALUE(packTimeMaximum),@"submit_stage_time_total_ns":CML_VALUE(submitTimeTotal),@"submit_stage_time_maximum_ns":CML_VALUE(submitTimeMaximum),
        @"native_batch_passes":CML_VALUE(batchPasses),@"native_batch_parity_checks":CML_VALUE(batchParityChecks),@"native_batch_parity_failures":CML_VALUE(batchParityFailures),@"native_batch_disabled":@(atomic_load(&CMLBatchDisabled)),
        @"native_batch_enabled":@(CMLIntersect&&!atomic_load(&CMLBatchDisabled)),@"native_ray_result_bytes":@(sizeof(CMRayHit)),@"native_ray_result_alignment":@(_Alignof(CMRayHit)),
        @"mesh_cache_entries":CML_VALUE(cacheEntries),@"mesh_cache_entry_limit":@CML_CACHE_ENTRIES,@"mesh_cache_evictions":CML_VALUE(cacheEvictions),@"mesh_cache_capacity_rejections":CML_VALUE(cacheRejects),
        @"pixel_grid_step":@1,@"occlusion_source":@"Core-local-geometry-per-pixel-rays",@"continuous_pose_sampling":@YES,@"silhouette_renderer_connected":@YES,
        @"game_current_lod_verified":@NO,@"material_alpha_clip_verified":@NO,@"cloth_morph_verified":@NO,@"master_reference_fallback_game_verified":@NO,@"gpu_scene_depth_available":@NO,@"full_requested_effect_verified":@NO };
#undef CML_VALUE
    NSData *data=[NSJSONSerialization dataWithJSONObject:status options:NSJSONWritingSortedKeys error:NULL];if(data.length&&data.length<65536)[data writeToURL:url options:NSDataWritingAtomic error:NULL];
}
static void CMLStartup(void) {
    CMLTargets=[NSMutableArray array];CMLCache=[NSMutableArray array];CMLSchemas=[NSMutableDictionary dictionary];CMLSchemaStore=[CMLSchemaSnapshot new];CMLDiscoveries=[NSMutableDictionary dictionary];CMLFailureRecords=[NSMutableArray array];CMLExpectedContext=CMBase+0xC54270;
    CMLOriginalDraw=(void (*)(void *))CMCallable(CMBase+0xDB144);CMLGetDrawList=(void *(*)(void))CMCallable(CMBase+0x123E30);CMLPolygon=(void (*)(void *,const CMVec2 *,int,uint32_t))CMCallable(CMBase+0x13C8AC);
    // The full 96-byte result ABI and independent tail-write regression now
    // cover this path. Per-frame comparisons still fall back to the wrappers.
    CMLIntersect=(void (*)(void *,CMRayHit *,void *))CMCallable(CMBase+0x2AD2A0);atomic_store(&CMLBatchDisabled,false);atomic_store(&CMLReady,true);
    dispatch_async(CMReportQueue, ^{NSURL *url=[CMReportURL.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Core-model-render-status.json"];CMLWriteStatus(url);
        CMLStatusTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,CMReportQueue);dispatch_source_set_timer(CMLStatusTimer,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),5*NSEC_PER_SEC,NSEC_PER_SEC);
        dispatch_source_set_event_handler(CMLStatusTimer, ^{CMLWriteStatus(url);});dispatch_resume(CMLStatusTimer);});
}
