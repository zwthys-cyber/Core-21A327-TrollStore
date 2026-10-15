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
@interface CMLMesh : NSObject
@property(nonatomic) CMGKey key;
@property(nonatomic) uint32_t lod,bones,vertices,sectionCount,indexCount,indexWidth,byteCost;
@property(nonatomic,strong) NSData *vertexData,*weightData,*indexData,*sectionData,*paletteData,*inverseData,*boneInfo;
@end
@implementation CMLMesh
@end
@interface CMLTarget : NSObject
@property(nonatomic) uint64_t component,actor,asset,poseTime,metadataTime,metadataRetryAfter;
@property(nonatomic) uint32_t count,stride;
@property(nonatomic) CMTransform transform;
@property(nonatomic,strong) NSData *pose,*refs,*binding,*names,*evidence,*gpu;
@property(nonatomic,strong) NSArray<CMLMesh *> *meshes;
@end
@implementation CMLTarget
@end
@interface CMLSchemaSnapshot : NSObject
@property(atomic,copy) NSDictionary<NSNumber *,NSData *> *schemas;
@end
@implementation CMLSchemaSnapshot
@end
typedef struct {
    atomic_uint_fast64_t frames,poseUpdates,metadataLoads,metadataFailures,meshLoads,cacheBytes,triangles,white,red,queryCalls,drawFrames,spans;
    atomic_uint_fast64_t staleTargets,unknownVisibility,unverifiedLOD,referenceFallbacks,unknownPose,badRays,budgetFrames,projectionMismatch,oversizedMasks;
    atomic_uint_fast64_t drawTimeTotal,drawTimeMaximum;
    atomic_uint_fast64_t batchPasses,batchParityChecks,batchParityFailures;
    atomic_uint_fast64_t cacheEvictions,cacheRejects,cacheEntries;
} CMLCounters;
static CMLCounters CMLStats;
static atomic_bool CMLReady;
static pthread_mutex_t CMLMutex=PTHREAD_MUTEX_INITIALIZER;
static NSMutableArray<CMLTarget *> *CMLTargets;
static NSMutableArray<CMLMesh *> *CMLCache;
static NSMutableDictionary<NSNumber *,NSData *> *CMLSchemas;
static CMLSchemaSnapshot *CMLSchemaStore;
static uint64_t CMLReadDeadline;
static uint32_t CMLReads,CMLCachedBytes;
static uintptr_t CMLExpectedContext;
static void (*CMLOriginalDraw)(void *);
static void *(*CMLGetDrawList)(void);
static void (*CMLPolygon)(void *,const CMVec2 *,int,uint32_t);
static void (*CMLIntersect)(void *,CMRayHit *,void *);
static atomic_bool CMLBatchDisabled;
static dispatch_source_t CMLStatusTimer;

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
    NSData *cached=CMLSchemas[@(component->header.class_address)];CMFSchema schema={0};
    if(cached){memcpy(&schema,cached.bytes,sizeof(schema));return schema;}
    NSMutableData *storage=[NSMutableData dataWithBytes:source length:sizeof(*source)];CMNNames *names=storage.mutableBytes;
    names->count=0;names->read_attempts=0;names->read_failures=0;names->truncated=false;
    CMFCollect(component->header.class_address,CMLRead,names,&schema);
    if(CMLSchemas.count<32){CMLSchemas[@(component->header.class_address)]=[NSData dataWithBytes:&schema length:sizeof(schema)];CMLSchemaStore.schemas=CMLSchemas;}
    CMLReadDeadline=CMNow()+12000000ULL;CMLReads=0;
    return schema;
}
static void CMLTrimStaleTargets(uint64_t now) {
    for(CMLTarget *target in CMLTargets)if(target.poseTime&&now-target.poseTime>2000000000ULL){
        target.meshes=nil;target.refs=nil;target.binding=nil;target.names=nil;target.evidence=nil;target.gpu=nil;target.metadataTime=0;
    }
}
static bool CMLMakeCacheRoom(size_t bytes) {
    if(bytes>CML_CACHE_BYTES)return false;
    while(CMLCachedBytes>CML_CACHE_BYTES-bytes||CMLCache.count>=CML_CACHE_ENTRIES){
        CMLMesh *oldest=nil;for(CMLMesh *mesh in CMLCache){bool pinned=false;
            for(CMLTarget *target in CMLTargets)if([target.meshes containsObject:mesh]){pinned=true;break;}
            if(!pinned){oldest=mesh;break;}}
        if(!oldest){atomic_fetch_add(&CMLStats.cacheRejects,1);return false;}
        CMLCachedBytes-=oldest.byteCost;[CMLCache removeObjectIdenticalTo:oldest];atomic_fetch_add(&CMLStats.cacheEvictions,1);
    }
    atomic_store(&CMLStats.cacheBytes,CMLCachedBytes);atomic_store(&CMLStats.cacheEntries,CMLCache.count);return true;
}
static CMLMesh *CMLLoadMesh(const CMGDescriptor *d,const CMEResource *resource) {
    CMGKey key=CMGKeyFor(d);CMLMesh *cached=nil;for(CMLMesh *mesh in CMLCache){CMGKey existing=mesh.key;if(CMGKeyEqual(&existing,&key)){cached=mesh;break;}}
    if(cached){[CMLCache removeObjectIdenticalTo:cached];[CMLCache addObject:cached];return cached;}
    if (!resource||!resource->hierarchy_valid||!resource->reference_pose_valid||d->vertex_stride!=24||d->weight_stride!=8||!d->section_count||d->section_count>8||d->index_bytes<88) return nil;
    uint64_t indexAddress=0;uint32_t indexCount=0;
    if(!CMGArrayCandidate(d->index,d->index_bytes,0x48,d->index_width,d->vertex_count*18,&indexAddress,&indexCount)||indexCount%3) return nil;
    size_t bytes=(size_t)d->vertex_count*32+(size_t)indexCount*d->index_width+(size_t)resource->count*sizeof(CMSMatrix)+resource->info_bytes+8192;
    if(!CMLMakeCacheRoom(bytes))return nil;
    NSData *vertices=CMLBytes(d->vertex_address,(size_t)d->vertex_count*24),*weights=CMLBytes(d->weight_address,(size_t)d->vertex_count*8),*indices=CMLBytes(indexAddress,(size_t)indexCount*d->index_width);
    if(!vertices||!weights||!indices||!CMGWeightHypothesis(d,weights.bytes,weights.length)||!CMGIndexHypothesis(indices.bytes,indices.length,d->index_width,d->vertex_count))return nil;
    NSMutableData *sections=[NSMutableData dataWithLength:d->section_count*sizeof(CMSSection)],*palettes=[NSMutableData dataWithLength:d->section_count*512],*inverse=[NSMutableData dataWithLength:resource->count*sizeof(CMSMatrix)];
    CMSSection *sec=sections.mutableBytes;uint32_t nextV=0,nextI=0;
    for(uint32_t j=0;j<d->section_count;j++) {
        if(!CMSParseSection(d->sections,d->section_bytes,j,d->vertex_count,indexCount,&sec[j])||sec[j].first_vertex!=nextV||sec[j].first_index!=nextI) return nil;
        NSData *map=CMLBytes(sec[j].bone_address,sec[j].bone_count*2);if(!map)return nil;uint16_t *palette=(uint16_t *)((uint8_t *)palettes.mutableBytes+j*512);memcpy(palette,map.bytes,map.length);
        for(uint32_t k=0;k<sec[j].bone_count;k++)if(palette[k]>=resource->count)return nil;
        for(uint32_t k=sec[j].first_vertex;k<sec[j].first_vertex+sec[j].vertex_count;k++)for(unsigned a=0;a<4;a++) {
            const uint8_t *w=(const uint8_t *)weights.bytes+k*8;if(w[a+4]&&w[a]>=sec[j].bone_count)return nil;
        }
        for(uint32_t k=sec[j].first_index;k<sec[j].first_index+sec[j].triangle_count*3;k++) {uint32_t v=0;memcpy(&v,(const uint8_t *)indices.bytes+(size_t)k*d->index_width,d->index_width);if(v<sec[j].first_vertex||v>=sec[j].first_vertex+sec[j].vertex_count)return nil;}
        nextV+=sec[j].vertex_count;nextI+=sec[j].triangle_count*3;
    }
    if(nextV!=d->vertex_count||nextI!=indexCount||!CMSReferenceInverse(resource->info,resource->pose,resource->count,inverse.mutableBytes))return nil;
    CMLMesh *mesh=[CMLMesh new];mesh.key=key;mesh.lod=d->lod_index;mesh.bones=resource->count;mesh.vertices=d->vertex_count;mesh.sectionCount=d->section_count;mesh.indexCount=indexCount;mesh.indexWidth=d->index_width;
    mesh.vertexData=vertices;mesh.weightData=weights;mesh.indexData=indices;mesh.sectionData=sections;mesh.paletteData=palettes;mesh.inverseData=inverse;mesh.boneInfo=[NSData dataWithBytes:resource->info length:resource->info_bytes];
    mesh.byteCost=(uint32_t)bytes;CMLCachedBytes+=(uint32_t)bytes;[CMLCache addObject:mesh];atomic_fetch_add(&CMLStats.meshLoads,1);atomic_store(&CMLStats.cacheBytes,CMLCachedBytes);atomic_store(&CMLStats.cacheEntries,CMLCache.count);return mesh;
}
static bool CMLRefresh(CMLTarget *target) {
    CMLReadDeadline=CMNow()+100000000ULL;CMLReads=0;NSData *layout=CMLBytes(target.component,4096);if(!layout)return false;
    NSMutableData *refs=[NSMutableData dataWithLength:sizeof(CMRReferences)],*binding=[NSMutableData dataWithLength:sizeof(CMBBinding)],*names=[NSMutableData dataWithLength:sizeof(CMNNames)],*evidence=[NSMutableData dataWithLength:sizeof(CMEMeshEvidence)],*gpu=[NSMutableData dataWithLength:sizeof(CMGDescriptors)];
    CMRCollect(layout.bytes,layout.length,CMLRead,refs.mutableBytes);CMBCollect(target.component,refs.bytes,CMLRead,binding.mutableBytes);CMCollectNames(refs.bytes,binding.bytes,names.mutableBytes);
    CMECollect(target.component,layout.bytes,layout.length,target.count,refs.bytes,binding.bytes,names.bytes,CMLRead,evidence.mutableBytes);
    CMGCollect(binding.bytes,names.bytes,CMLRead,gpu.mutableBytes);const CMGDescriptors *descriptors=gpu.bytes;const CMBBinding *b=binding.bytes;
    if(!b->assets.count||b->assets.references[0].address!=target.asset)return false;
    NSMutableArray *meshes=[NSMutableArray array];target.meshes=meshes; // Pin partial loads during cache eviction.
    for(uint32_t i=0;i<descriptors->count;i++){const CMGDescriptor *d=&descriptors->descriptors[i];CMLMesh *mesh=CMLLoadMesh(d,CMEResourceAt(evidence.bytes,d->asset_index));if(mesh)[meshes addObject:mesh];}
    if(!meshes.count){target.meshes=nil;return false;}
    target.refs=refs;target.binding=binding;target.names=names;target.evidence=evidence;target.gpu=gpu;target.meshes=meshes;target.metadataTime=CMNow();atomic_fetch_add(&CMLStats.metadataLoads,1);return true;
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
static NSData *CMLState(CMLTarget *target,uint32_t id,NSMutableDictionary *states) {
    NSData *cached=states[@(id)];if(cached)return cached;uint64_t address=target.component;size_t bytes=4096;
    if(id){const CMRReference *c=CMLComponent(target,id);if(!c||c->prefix_bytes<0x8C8)return nil;address=c->address;bytes=c->prefix_bytes;}
    NSData *state=CMLBytes(address,bytes);if(!state)return nil;const CMBBinding *b=target.binding.bytes;
    uint64_t asset=0;memcpy(&asset,(const uint8_t *)state.bytes+0x828,8);
    if(id>=b->assets.count||asset!=b->assets.references[id].address){target.metadataTime=0;return nil;}
    states[@(id)]=state;return state;
}
static CMLMesh *CMLMeshFor(CMLTarget *target,uint32_t id) {
    const CMBBinding *b=target.binding.bytes;if(id>=b->assets.count)return nil;
    for(CMLMesh *m in target.meshes)if(m.key.asset==b->assets.references[id].address)return m;return nil;
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
static bool CMLClassifyPixels(CMPMask *mask,CMVec3 camera,CMLQueryContext *q,uint32_t *white,uint32_t *red) {
    if(!CMLIntersect||atomic_load(&CMLBatchDisabled))return CMPClassify(mask,camera,CMLQueryPixel,q,white,red);
    *white=*red=0;size_t count=(size_t)mask->width*mask->height;
    for(unsigned scene=0;scene<3;scene++){
        size_t first=0;while(first<count&&(!mask->pixels[first].inverse_depth||mask->pixels[first].colour==2))first++;
        if(first==count)break;if(CMNow()>=q->deadline)return false;
        // One comparison per scene/frame against the unchanged original
        // wrapper also checks this ABI on device. A discrepancy falls back to
        // the original pixel path and never treats unknown pixels as white.
        CMRayHit reference=CMRayFunctions[scene](q->scenes[scene],&camera,&mask->pixels[first].world);
        atomic_fetch_add(&CMLStats.queryCalls,1);atomic_fetch_add(&CMLStats.batchParityChecks,1);
        uintptr_t address=(uintptr_t)q->scenes[scene];if(address>UINTPTR_MAX-0x60)return false;
        void *lock=(void *)(address+0x58),*handle=NULL;CMLock(lock);
        bool complete=CMSelfRead(address+8,&handle,sizeof(handle)),mismatch=false;uint64_t calls=0;
        for(size_t p=first;complete&&p<count;p++){
            CMPPixel *pixel=&mask->pixels[p];if(!pixel->inverse_depth||pixel->colour==2)continue;
            if(!(calls&63)&&CMNow()>=q->deadline){complete=false;break;}
            CMVec3 delta={pixel->world.x-camera.x,pixel->world.y-camera.y,pixel->world.z-camera.z};float distance=sqrtf(CMPDot(delta,delta));
            if(!isfinite(distance)||distance<0.001f){complete=false;break;}
            CMRayHit hit __attribute__((aligned(16)))={0};
            hit.origin[0]=camera.x;hit.origin[1]=camera.y;hit.origin[2]=camera.z;
            hit.direction[0]=delta.x/distance;hit.direction[1]=delta.y/distance;hit.direction[2]=delta.z/distance;
            hit.far_distance=distance;hit.mask=UINT32_MAX;hit.geometry_id=UINT32_MAX;
            if(handle){CMLIntersect(handle,&hit,NULL);calls++;}
            if(!isfinite(hit.far_distance)||hit.far_distance<0||hit.far_distance>distance+1){atomic_fetch_add(&CMLStats.badRays,1);complete=false;break;}
            if(p==first&&(reference.geometry_id!=hit.geometry_id||!isfinite(reference.far_distance)||fabsf(reference.far_distance-hit.far_distance)>fmaxf(.001f,distance*1e-5f))){mismatch=true;complete=false;break;}
            if(hit.geometry_id!=UINT32_MAX)pixel->colour=2;
        }
        CMUnlock(lock);atomic_fetch_add(&CMLStats.queryCalls,calls);atomic_fetch_add(&CMLStats.batchPasses,1);
        if(mismatch){atomic_fetch_add(&CMLStats.batchParityFailures,1);atomic_store(&CMLBatchDisabled,true);
            return CMPClassify(mask,camera,CMLQueryPixel,q,white,red);}
        if(!complete)return false;
    }
    for(size_t p=0;p<count;p++)if(mask->pixels[p].inverse_depth){CMPPixel *pixel=&mask->pixels[p];if(pixel->colour==2)(*red)++;else{pixel->colour=1;(*white)++;}}
    return true;
}
static void CMLDrawSpan(void *list,int x,int y,int width,uint8_t colour) {
    CMVec2 p[4]={{x,y},{x+width,y},{x+width,y+1},{x,y+1}};CMLPolygon(list,p,4,colour==2 ? 0xFF0000FFU : 0xFFFFFFFFU);
}
static bool CMLActorEligible(uintptr_t context,uint64_t actor) {
    uint64_t begin=0,end=0;uint32_t localTeam=0;
    if(!CMSelfRead(context+0x3F0,&begin,8)||!CMSelfRead(context+0x3F8,&end,8)||!CMSelfRead(context+0x3A8,&localTeam,4)||end<begin||(end-begin)%0x1F8||(end-begin)/0x1F8>512)return false;
    for(uint64_t p=begin;p<end;p+=0x1F8){uint64_t id=0;uint32_t team=0;float distance=0;
        if(!CMSelfRead(p,&id,8)||id!=actor)continue;if(!CMSelfRead(p+8,&team,4)||!CMSelfRead(p+0x40,&distance,4))return false;
        return team!=localTeam&&isfinite(distance)&&distance>0&&distance<=600;}
    return false;
}
__attribute__((noinline)) static void CMLDraw(uintptr_t context) {
    atomic_fetch_add(&CMLStats.frames,1);if(!atomic_load(&CMLReady)||context!=CMLExpectedContext||CMInside)return;
    uint8_t enabled=0,ready=0;float size[2];uint8_t rawView[52];
    if(!CMSelfRead(context+0x23,&enabled,1)||!enabled||!CMSelfRead(CMBase+0xC54459,&ready,1)||ready!=1||
        !CMSelfRead(context+4,size,8)||!CMSelfRead(context+0x1F4,rawView,52))return;
    CMPView view;if(!CMPMakeView(rawView,size[0],size[1],&view))return;
    uint64_t drawStart=CMNow();
    CMInside=true;pthread_mutex_lock(&CMLMutex);
    @autoreleasepool {
        NSMutableArray<NSData *> *projected=[NSMutableArray array],*triangleArrays=[NSMutableArray array];bool refreshed=false;uint64_t now=CMNow();
        float minX=view.width,minY=view.height,maxX=0,maxY=0;CMLTrimStaleTargets(now);
        for(CMLTarget *target in CMLTargets){
            if(!target.poseTime||now-target.poseTime>250000000ULL){atomic_fetch_add(&CMLStats.staleTargets,1);continue;}
            if(!CMLActorEligible(context,target.actor))continue;
            if(!target.metadataTime||now-target.metadataTime>1000000000ULL){if(refreshed||now<target.metadataRetryAfter)continue;refreshed=true;target.meshes=nil;if(!CMLRefresh(target)){target.metadataRetryAfter=CMNow()+1000000000ULL;atomic_fetch_add(&CMLStats.metadataFailures,1);continue;}target.metadataRetryAfter=0;}
            CMLReadDeadline=CMNow()+12000000ULL;CMLReads=0;
            const CMGDescriptors *gpu=target.gpu.bytes;const CMNNames *names=target.names.bytes;
            NSMutableDictionary *states=[NSMutableDictionary dictionary],*poses=[NSMutableDictionary dictionary];
            for(uint32_t di=0;di<gpu->count;di++){
                const CMGDescriptor *d=&gpu->descriptors[di];CMLMesh *mesh=nil;for(CMLMesh *m in target.meshes)if(m.lod==d->lod_index&&m.key.asset==d->asset_address)mesh=m;if(!mesh)continue;
                NSData *state=CMLState(target,d->asset_index,states);if(!state)continue;const CMRReference *component=CMLComponent(target,d->asset_index);CMRReference root={0};
                if(!d->asset_index){memcpy(&root.header,state.bytes,sizeof(root.header));root.address=target.component;component=&root;}if(!component)continue;
                CMFSchema schema=CMLSchemaFor(component,names);bool visible=true,visibilityKnown=CMFVisibility(&schema,state.bytes,state.length,&visible);if(visibilityKnown&&!visible)continue;
                if(!visibilityKnown){atomic_fetch_add(&CMLStats.unknownVisibility,1);const char *name=CMNText(names,d->name_index);if(name&&(strstr(name,"Parachute")||strstr(name,"Wingsuit")||strstr(name,"Glide")||strstr(name,"Smoke")))continue;}
                bool lodKnown=false;uint32_t lod=CMFLOD(&schema,state.bytes,state.length,d->lod_count,&lodKnown);if(lod!=d->lod_index)continue;if(!lodKnown)atomic_fetch_add(&CMLStats.unverifiedLOD,1);
                NSData *pose=CMLPoseMatrices(target,d->asset_index,states,poses,0);if(!pose){atomic_fetch_add(&CMLStats.unknownPose,1);continue;}
                CMTransform componentTransform;memcpy(&componentTransform,(const uint8_t *)state.bytes+0x1F0,sizeof(componentTransform));CMSMatrix world;if(!CMSFromTransform(&componentTransform,&world))continue;
                NSMutableData *skinData=[NSMutableData dataWithLength:mesh.bones*sizeof(CMSMatrix)];CMSMatrix *skin=skinData.mutableBytes;
                for(uint32_t i=0;i<mesh.bones;i++)skin[i]=CMSMultiply(CMSMultiply(world,((const CMSMatrix *)pose.bytes)[i]),((const CMSMatrix *)mesh.inverseData.bytes)[i]);
                uint8_t known[256];memset(known,1,mesh.bones);NSMutableData *points=[NSMutableData dataWithLength:mesh.vertices*sizeof(CMPVertex)],*valid=[NSMutableData dataWithLength:mesh.vertices];CMPVertex *p=points.mutableBytes;uint8_t *ok=valid.mutableBytes;const CMSSection *sec=mesh.sectionData.bytes;bool projectionOK=true;
                for(uint32_t j=0;j<mesh.sectionCount;j++)for(uint32_t i=sec[j].first_vertex;i<sec[j].first_vertex+sec[j].vertex_count;i++){
                    CMVec3 position;CMSResult result=CMSSkinVertex((const uint8_t *)mesh.vertexData.bytes+i*24,(const uint8_t *)mesh.weightData.bytes+i*8,(const uint16_t *)((const uint8_t *)mesh.paletteData.bytes+j*512),sec[j].bone_count,skin,known,mesh.bones,&position);
                    if(result!=CMS_OK||!CMPProject(&view,position,&p[i]))continue;
                    if(i%128==0){CMVec2 native=CMProject(&position,(const void *)(context+0x1F4),(int)size[0],(int)size[1]);if(!isfinite(native.x)||!isfinite(native.y)||fabsf(native.x-p[i].xy.x)>0.75f||fabsf(native.y-p[i].xy.y)>0.75f){projectionOK=false;break;}}
                    ok[i]=1;minX=fminf(minX,p[i].xy.x);minY=fminf(minY,p[i].xy.y);maxX=fmaxf(maxX,p[i].xy.x);maxY=fmaxf(maxY,p[i].xy.y);
                }
                if(!projectionOK){atomic_fetch_add(&CMLStats.projectionMismatch,1);continue;}
                NSMutableData *tris=[NSMutableData data];for(uint32_t i=0;i<mesh.indexCount;i+=3){uint32_t t[3]={0};for(unsigned k=0;k<3;k++)memcpy(&t[k],(const uint8_t *)mesh.indexData.bytes+(size_t)(i+k)*mesh.indexWidth,mesh.indexWidth);if(ok[t[0]]&&ok[t[1]]&&ok[t[2]])[tris appendBytes:t length:sizeof(t)];}
                [projected addObject:points];[triangleArrays addObject:tris];
            }
        }
        int x=(int)fmaxf(0,floorf(minX)),y=(int)fmaxf(0,floorf(minY)),right=(int)fminf(view.width,ceilf(maxX)),bottom=(int)fminf(view.height,ceilf(maxY));
        if(right>x&&bottom>y&&projected.count){size_t count=(size_t)(right-x)*(bottom-y);if(count>CML_PIXEL_LIMIT)atomic_fetch_add(&CMLStats.oversizedMasks,1);else {
            NSMutableData *pixels=[NSMutableData dataWithLength:count*sizeof(CMPPixel)];CMPMask mask={x,y,right-x,bottom-y,pixels.mutableBytes};
            for(NSUInteger j=0;j<projected.count;j++){const CMPVertex *points=projected[j].bytes;const uint32_t *tris=triangleArrays[j].bytes;for(NSUInteger i=0;i<triangleArrays[j].length/12;i++){CMPVertex t[3]={points[tris[i*3]],points[tris[i*3+1]],points[tris[i*3+2]]};if(CMPRasterTriangle(&mask,t))atomic_fetch_add(&CMLStats.triangles,1);}}
            CMLQueryContext query={{NULL,NULL,NULL},CMNow()+12000000ULL};bool complete=false;uint32_t white=0,red=0;
            if(CMLock&&CMUnlock){CMLock((void *)(CMBase+0xBD4F78));if(CMSelfRead(CMBase+0xC15EC8,query.scenes,sizeof(query.scenes))&&query.scenes[0]&&query.scenes[1]&&query.scenes[2])complete=CMLClassifyPixels(&mask,view.position,&query,&white,&red);CMUnlock((void *)(CMBase+0xBD4F78));}
            uint32_t spans=complete ? CMPSpans(&mask,NULL,NULL,CML_SPAN_LIMIT) : 0;
            if(complete&&spans&&spans<=CML_SPAN_LIMIT){void *list=CMLGetDrawList();uint8_t flags=0;if(list&&CMSelfRead((uintptr_t)list+0x30,&flags,1)){
                *(uint8_t *)((uintptr_t)list+0x30)=flags&~4U;CMPSpans(&mask,CMLDrawSpan,list,CML_SPAN_LIMIT);*(uint8_t *)((uintptr_t)list+0x30)=flags;
                atomic_fetch_add(&CMLStats.drawFrames,1);atomic_fetch_add(&CMLStats.white,white);atomic_fetch_add(&CMLStats.red,red);atomic_fetch_add(&CMLStats.spans,spans);}}
            else if(!complete||spans>CML_SPAN_LIMIT)atomic_fetch_add(&CMLStats.budgetFrames,1);
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
    NSMutableArray *schemas=[NSMutableArray array];NSDictionary *published=CMLSchemaStore.schemas ?: @{};
    for(NSNumber *address in published){CMFSchema s;memcpy(&s,((NSData *)published[address]).bytes,sizeof(s));
        [schemas addObject:@{@"class_address":address,@"classes_walked":@(s.classes),@"fields_walked":@(s.fields),@"read_attempts":@(s.reads),@"read_failures":@(s.failures),@"truncated":@(s.truncated),
            @"visible_found":@(s.visible.found),@"visible_offset":@(s.visible.offset),@"visible_mask":@(s.visible.mask),@"hidden_found":@(s.hidden.found),@"hidden_offset":@(s.hidden.offset),@"hidden_mask":@(s.hidden.mask),
            @"hide_skin_found":@(s.hide_skin.found),@"hide_skin_offset":@(s.hide_skin.offset),@"hide_skin_mask":@(s.hide_skin.mask),
            @"forced_lod_found":@(s.forced_found),@"forced_lod_offset":@(s.forced_lod),@"predicted_lod_found":@(s.predicted_found),@"predicted_lod_offset":@(s.predicted_lod),@"predicted_lod_source":@(s.predicted_source),@"predicted_lod_getter":@(s.predicted_getter),
            @"class_names":({NSMutableArray *a=[NSMutableArray array];for(uint32_t j=0;j<s.classes&&j<12;j++)[a addObject:[NSString stringWithUTF8String:s.class_names[j]]];a;})}];}
    NSDictionary *status=@{ @"render_version":@3,@"date":NSDate.date.description,@"frames_observed":CML_VALUE(frames),@"pose_updates":CML_VALUE(poseUpdates),
        @"metadata_loads":CML_VALUE(metadataLoads),@"metadata_failures":CML_VALUE(metadataFailures),@"mesh_loads":CML_VALUE(meshLoads),@"mesh_cache_bytes":CML_VALUE(cacheBytes),
        @"rasterized_triangles":CML_VALUE(triangles),@"pixel_query_calls":CML_VALUE(queryCalls),@"filled_frames":CML_VALUE(drawFrames),@"white_pixels":CML_VALUE(white),@"red_pixels":CML_VALUE(red),@"filled_spans":CML_VALUE(spans),
        @"stale_targets_skipped":CML_VALUE(staleTargets),@"unknown_visibility_samples":CML_VALUE(unknownVisibility),@"unverified_lod_samples":CML_VALUE(unverifiedLOD),@"master_reference_pose_fallback_bones":CML_VALUE(referenceFallbacks),
        @"unknown_pose_meshes":CML_VALUE(unknownPose),@"malformed_pixel_queries":CML_VALUE(badRays),@"budget_frames_skipped":CML_VALUE(budgetFrames),@"projection_mismatch_meshes":CML_VALUE(projectionMismatch),@"oversized_masks_skipped":CML_VALUE(oversizedMasks),
        @"draw_time_total_ns":CML_VALUE(drawTimeTotal),@"draw_time_maximum_ns":CML_VALUE(drawTimeMaximum),@"reflected_component_schemas":schemas,@"schema_snapshot_busy":@NO,@"schema_snapshot_source":@"immutable-discovery-snapshot",
        @"native_batch_passes":CML_VALUE(batchPasses),@"native_batch_parity_checks":CML_VALUE(batchParityChecks),@"native_batch_parity_failures":CML_VALUE(batchParityFailures),@"native_batch_disabled":@(atomic_load(&CMLBatchDisabled)),
        @"native_batch_enabled":@(CMLIntersect&&!atomic_load(&CMLBatchDisabled)),@"native_ray_result_bytes":@(sizeof(CMRayHit)),@"native_ray_result_alignment":@(_Alignof(CMRayHit)),
        @"mesh_cache_entries":CML_VALUE(cacheEntries),@"mesh_cache_entry_limit":@CML_CACHE_ENTRIES,@"mesh_cache_evictions":CML_VALUE(cacheEvictions),@"mesh_cache_capacity_rejections":CML_VALUE(cacheRejects),
        @"pixel_grid_step":@1,@"occlusion_source":@"Core-local-geometry-per-pixel-rays",@"continuous_pose_sampling":@YES,@"silhouette_renderer_connected":@YES,
        @"game_current_lod_verified":@NO,@"material_alpha_clip_verified":@NO,@"cloth_morph_verified":@NO,@"master_reference_fallback_game_verified":@NO,@"gpu_scene_depth_available":@NO,@"full_requested_effect_verified":@NO };
#undef CML_VALUE
    NSData *data=[NSJSONSerialization dataWithJSONObject:status options:NSJSONWritingSortedKeys error:NULL];if(data.length&&data.length<65536)[data writeToURL:url options:NSDataWritingAtomic error:NULL];
}
static void CMLStartup(void) {
    CMLTargets=[NSMutableArray array];CMLCache=[NSMutableArray array];CMLSchemas=[NSMutableDictionary dictionary];CMLSchemaStore=[CMLSchemaSnapshot new];CMLExpectedContext=CMBase+0xC54270;
    CMLOriginalDraw=(void (*)(void *))CMCallable(CMBase+0xDB144);CMLGetDrawList=(void *(*)(void))CMCallable(CMBase+0x123E30);CMLPolygon=(void (*)(void *,const CMVec2 *,int,uint32_t))CMCallable(CMBase+0x13C8AC);
    // Build 29 crashed in this fast path after a short RayHit buffer corrupted
    // the adjacent handle. Keep the corrected ABI everywhere, and use the
    // unchanged original wrappers in this stability build.
    CMLIntersect=NULL;atomic_store(&CMLReady,true);
    dispatch_async(CMReportQueue, ^{NSURL *url=[CMReportURL.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Core-model-render-status.json"];CMLWriteStatus(url);
        CMLStatusTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,CMReportQueue);dispatch_source_set_timer(CMLStatusTimer,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),5*NSEC_PER_SEC,NSEC_PER_SEC);
        dispatch_source_set_event_handler(CMLStatusTimer, ^{CMLWriteStatus(url);});dispatch_resume(CMLStatusTimer);});
}
