// Production observer -> mesh skinning -> pixel rays -> native fill, with
// independent synthetic memory/functions. Run only in GitHub Actions.
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <assert.h>
#include <errno.h>
static const uintptr_t TestBase=0x100000000ULL;
static const uint64_t GameComponent=0x200001000ULL,GamePose=0x200010000ULL,GameActor=0x200020000ULL,Asset=0x300000000ULL,GameClass=0x400000000ULL;
static const uint64_t ChildComponent=0x200030000ULL,ChildPose=0x200040000ULL,ChildAsset=0x300010000ULL;
static const uint64_t RenderAddress=0x500000000ULL,LODsAddress=0x500010000ULL,LODAddress=0x500020000ULL,IndexAddress=0x500030000ULL,SectionsAddress=0x500040000ULL;
static uint8_t Context[2048],ComponentData[4096],ActorRecord[504],SourceRecord[24],Frame[2048],DrawList[256],SceneData[3][160];
#ifdef CORE_GROUND_BOW_TRACE
static uint8_t BowSources[3*32],BowRecords[3*88],BowMetas[3][64],BowLongNames[2][16];
static bool TraceBowMutate;
#endif
static uint8_t AssetData[1048],RenderData[16],LODArray[8],LODData[1024],IndexData[112],SectionsData[256];
static uint8_t VertexData[96],WeightData[24];static uint16_t TriangleData[3]={0,1,2},BonePalette=0;static uint32_t VertexStride=24;
static uint8_t ChildData[4096];static uint32_t WideTriangleData[3]={0,1,2};
static uint64_t TestTime=10000000000ULL;
static uint64_t NativeDelay;
#ifdef CORE_FILL_WORK
static bool LargeStatic;
static uint8_t LargeVertices[2048*24],LargeWeights[2048*8];
static unsigned LargeReads,LargeVertexReads;
#endif
static unsigned OriginalCalls,Fills,WhiteFills,RedFills,LockDepth,Reads,SceneLocks,RawCalls,BlockScene;
static bool BadRay,WrongProjection,FailQueryBudget,WrongRaw,UnreadableScene,ResourceMemory,FailResourceRead,ReallocatedActors,SlowProjection,SchemaMemory;
static bool GeometryReady;
#ifdef CORE_ACTOR_TRACE
static bool TraceMutate;
static void TraceOnCopy(uintptr_t address);
#endif
static unsigned CameraMotion;
static bool CameraMoved;
static bool CheckObserverProgress;
static uint8_t LastProjectedCamera[52];
static int TestClock(clockid_t clock,struct timespec *t){(void)clock;if(FailQueryBudget)TestTime+=20000000ULL;t->tv_sec=TestTime/1000000000ULL;t->tv_nsec=TestTime%1000000000ULL;return 0;}
static kern_return_t SelfRead(vm_map_t task,vm_address_t address,vm_size_t size,vm_address_t dest,vm_size_t *copied){
    (void)task;uint64_t value=0;
    if(address==TestBase+0xBD4720)value=0xAA55;
    else if(address==TestBase+0xBD4978)value=0xAA55^48;
    else if(address==TestBase+0xC54459)value=GeometryReady;
    else if(address==TestBase+0xC15EC8&&size==24){uintptr_t scenes[3]={(uintptr_t)SceneData[0],(uintptr_t)SceneData[1],(uintptr_t)SceneData[2]};memcpy((void *)dest,scenes,24);*copied=24;return KERN_SUCCESS;}
    else if(address>=(uintptr_t)SceneData&&address<(uintptr_t)SceneData+sizeof(SceneData)){
        if(UnreadableScene||size!=8||(address-(uintptr_t)SceneData)%160!=8)return KERN_INVALID_ADDRESS;
        memcpy((void *)dest,(void *)address,8);*copied=8;return KERN_SUCCESS;}
    else if(ReallocatedActors&&address==(uintptr_t)Context+0x3F0&&size==16){memset((void *)dest,0,size);*copied=size;return KERN_SUCCESS;}
    else {const void *arrays[]={Context,ActorRecord,SourceRecord,Frame,DrawList,
#ifdef CORE_GROUND_BOW_TRACE
        BowSources,BowRecords,BowMetas,BowLongNames,
#endif
        };const size_t sizes[]={sizeof(Context),sizeof(ActorRecord),sizeof(SourceRecord),sizeof(Frame),sizeof(DrawList),
#ifdef CORE_GROUND_BOW_TRACE
        sizeof(BowSources),sizeof(BowRecords),sizeof(BowMetas),sizeof(BowLongNames),
#endif
        };
        for(unsigned i=0;i<sizeof(arrays)/sizeof(arrays[0]);i++)if(address>=(uintptr_t)arrays[i]&&size<=sizes[i]&&address-(uintptr_t)arrays[i]<=sizes[i]-size){memcpy((void *)dest,(void *)address,size);*copied=size;
#ifdef CORE_ACTOR_TRACE
            TraceOnCopy(address);
#endif
            return KERN_SUCCESS;}return KERN_INVALID_ADDRESS;}
    if(size>8)return KERN_INVALID_ADDRESS;memcpy((void *)dest,&value,size);*copied=size;return KERN_SUCCESS;
}
#define CORE_MODEL_CAPTURE_TESTING 1
#define CORE_MODEL_RENDER 1
#define clock_gettime TestClock
#define vm_read_overwrite SelfRead
#include "../patch/CoreModelCapture.m"
#undef vm_read_overwrite
#undef clock_gettime
#ifdef CORE_ACTOR_TRACE
static void TraceOnCopy(uintptr_t address){if(TraceMutate&&address==(uintptr_t)ActorRecord){atomic_fetch_add(&CAVGeneration,1);TraceMutate=false;}
#ifdef CORE_GROUND_BOW_TRACE
    if(TraceBowMutate&&address==(uintptr_t)BowRecords){atomic_fetch_add(&CAVGeneration,1);TraceBowMutate=false;}
#endif
}
#endif
static bool Reader(uint64_t address,void *out,size_t size){Reads++;CoreModelBzeroCapture(out,size,0,address,TestBase+0xE1960);
#ifdef CORE_FILL_WORK
    if(LargeStatic){const uint64_t base[]={0x600000000ULL,0x600010000ULL,0x600020000ULL,0x600030000ULL};const void *data[]={LargeVertices,LargeWeights,TriangleData,&BonePalette};const size_t lengths[]={sizeof(LargeVertices),sizeof(LargeWeights),sizeof(TriangleData),sizeof(BonePalette)};
        for(unsigned i=0;i<4;i++)if(address>=base[i]&&size<=lengths[i]&&address-base[i]<=lengths[i]-size){memcpy(out,(const uint8_t *)data[i]+(address-base[i]),size);LargeReads++;if(!i)LargeVertexReads++;TestTime+=1100000ULL;return true;}}
#endif
    // The original context can receive a newer camera after CMLDraw captured
    // its view, while remote mesh/pose reads are still in progress.
    if(address==GameComponent&&CameraMotion&&!CameraMoved){float view[13];memcpy(view,Context+0x1F4,sizeof(view));
        if(CameraMotion<=3)view[CameraMotion-1]+=3;
        else if(CameraMotion<=6)view[CameraMotion+2]+=15;
        else view[12]+=30;
        memcpy(Context+0x1F4,view,sizeof(view));CameraMoved=true;}
    if(SchemaMemory&&address==GameClass&&size==80){memset(out,0,size);CMRHeader h={0};h.vtable=TestBase+0x1000;h.class_address=GameClass;h.internal_index=1;memcpy(out,&h,sizeof(h));uint32_t bytes=4096;memcpy((uint8_t *)out+0x40,&bytes,4);return true;}
    if(address>=GameComponent&&size<=sizeof(ComponentData)&&address-GameComponent<=sizeof(ComponentData)-size){memcpy(out,ComponentData+(address-GameComponent),size);return true;}
    if(address>=ChildComponent&&size<=sizeof(ChildData)&&address-ChildComponent<=sizeof(ChildData)-size){memcpy(out,ChildData+(address-ChildComponent),size);return true;}
    if(ResourceMemory&&!FailResourceRead){const uint64_t addresses[]={Asset,RenderAddress,LODsAddress,LODAddress,IndexAddress,SectionsAddress};const void *arrays[]={AssetData,RenderData,LODArray,LODData,IndexData,SectionsData};const size_t sizes[]={sizeof(AssetData),sizeof(RenderData),sizeof(LODArray),sizeof(LODData),sizeof(IndexData),sizeof(SectionsData)};
        for(unsigned i=0;i<6;i++)if(address==addresses[i]&&size<=sizes[i]){memcpy(out,arrays[i],size);return true;}
        if(address==0x600000000ULL&&size==(size_t)VertexStride*3){memcpy(out,VertexData,size);return true;}if(address==0x600010000ULL&&size==24){memcpy(out,WeightData,24);return true;}
        if(address==0x600020000ULL&&size==6){memcpy(out,TriangleData,6);return true;}if(address==0x600020000ULL&&size==12){memcpy(out,WideTriangleData,12);return true;}if(address==0x600030000ULL&&size==2){memcpy(out,&BonePalette,2);return true;}}
    if((address==GamePose||address==ChildPose)&&size==48){CMTransform t={{0,0,0,1,0,0,0,0,1,1,1}};memcpy(out,&t,sizeof(t));return true;}return false;
}
static void ObserverCanProgress(void){if(CheckObserverProgress){assert(!pthread_mutex_trylock(&CMLMutex));pthread_mutex_unlock(&CMLMutex);}}
static CMVec2 Project(const CMVec3 *world,const void *camera,int width,int height){ObserverCanProgress();memcpy(LastProjectedCamera,camera,52);CMPView view;CMPVertex p;assert(CMPMakeView(camera,width,height,&view)&&CMPProject(&view,*world,&p));if(SlowProjection)TestTime+=20000000ULL;if(WrongProjection)p.xy.x+=10;return p.xy;}
static void Lock(void *p){if(p==(void *)(TestBase+0xBD4F78)){assert(!LockDepth);LockDepth=1;return;}
    assert(LockDepth==1);for(unsigned i=0;i<3;i++)if(p==SceneData[i]+0x58){LockDepth|=2U<<i;SceneLocks++;return;}assert(false);}
static void Unlock(void *p){if(p==(void *)(TestBase+0xBD4F78)){assert(LockDepth==1);LockDepth=0;return;}
    for(unsigned i=0;i<3;i++)if(p==SceneData[i]+0x58){assert(LockDepth==(1U|(2U<<i)));LockDepth=1;return;}assert(false);}
static CMRayHit Ray(void *scene,const CMVec3 *camera,const CMVec3 *world){ObserverCanProgress();assert(LockDepth==1);CMVec3 d={world->x-camera->x,world->y-camera->y,world->z-camera->z};CMRayHit hit={0};hit.geometry_id=world->y>=0&&scene==SceneData[BlockScene]?7:UINT32_MAX;hit.far_distance=sqrtf(CMPDot(d,d))*(hit.geometry_id==7?.5f:1);if(BadRay)hit.far_distance=NAN;return hit;}
static void Intersect(void *handle,CMRayHit *ray,void *context){unsigned slot=(unsigned)(uintptr_t)handle-1;assert(slot<3&&!context&&LockDepth==(1U|(2U<<slot))&&!((uintptr_t)ray&15));RawCalls++;
    ObserverCanProgress();
    assert(ray->near_distance==0&&ray->time==0&&ray->flags==0&&ray->mask==UINT32_MAX&&ray->geometry_id==UINT32_MAX);
    float norm=ray->direction[0]*ray->direction[0]+ray->direction[1]*ray->direction[1]+ray->direction[2]*ray->direction[2];assert(fabsf(norm-1)<.00001f);
    if(slot==BlockScene&&ray->origin[1]+ray->direction[1]*ray->far_distance>=0){ray->geometry_id=7;ray->far_distance*=.5f;}
    // Independent native writer extends through byte 95, as Core's native
    // result slots do. Build 29 placed its scene handle at byte 80 instead.
    uint64_t tail[2]={0x1FFFFFFFFULL,0xA5A55A5A11223344ULL};memcpy((uint8_t *)ray+80,tail,16);
    if(BadRay)ray->far_distance=NAN;if(WrongRaw)ray->geometry_id=123;}
static void Original(void *p){assert(p==Context);OriginalCalls++;TestTime+=NativeDelay;}
static void *List(void){return DrawList;}
static void Polygon(void *list,const CMVec2 *points,int count,uint32_t colour){assert(list==DrawList&&count==4&&!(DrawList[0x30]&4));assert(OriginalCalls&&points[1].x>points[0].x&&points[2].y>points[1].y);assert(colour==0xFFFFFFFF||colour==0xFF0000FF);Fills++;if(colour==0xFFFFFFFF)WhiteFills++;else RedFills++;}
static void Setup(void){
    memset(Context,0,sizeof(Context));memset(ComponentData,0,sizeof(ComponentData));memset(Frame,0,sizeof(Frame));memset(ActorRecord,0,sizeof(ActorRecord));memset(SourceRecord,0,sizeof(SourceRecord));memset(DrawList,0,sizeof(DrawList));
    TestTime=10000000000ULL;NativeDelay=0;OriginalCalls=Fills=WhiteFills=RedFills=LockDepth=Reads=SceneLocks=RawCalls=BlockScene=0;VertexStride=24;BadRay=WrongProjection=FailQueryBudget=WrongRaw=UnreadableScene=ResourceMemory=FailResourceRead=ReallocatedActors=SlowProjection=SchemaMemory=false;
    GeometryReady=true;
#ifdef CORE_GROUND_BOW_TRACE
    memset(BowSources,0,sizeof(BowSources));memset(BowRecords,0,sizeof(BowRecords));memset(BowMetas,0,sizeof(BowMetas));memset(BowLongNames,0,sizeof(BowLongNames));TraceBowMutate=false;
    BVTTargets=nil;BVTEvents=nil;BVTReport=nil;BVTSamples=BVTBusy=BVTFailures=BVTNextID=BVTBytes=BVTTotalBytes=BVTTimeMaximum=BVTCapacity=0;
#endif
#ifdef CORE_ACTOR_TRACE
    TraceMutate=false;atomic_store(&CAVGeneration,0);atomic_store(&CAVNativeActive,0);atomic_store(&CAVLastNativeEnd,0);
    CAVTargets=nil;CAVEvents=nil;CAVReport=nil;CAVSamples=CAVBusy=CAVFailures=CAVReadBytes=CAVTimeTotal=CAVTimeMaximum=CAVNextID=CAVCapacity=0;
#endif
    CameraMotion=0;CameraMoved=false;
    CheckObserverProgress=false;memset(LastProjectedCamera,0,sizeof(LastProjectedCamera));
    memset(SceneData,0,sizeof(SceneData));for(uintptr_t i=0;i<3;i++){uintptr_t handle=i+1;memcpy(SceneData[i]+8,&handle,8);}
    CMLIntersect=Intersect;atomic_store(&CMLBatchDisabled,false);
    atomic_store(&CMLReady,true);atomic_store(&CMEnabled,false);atomic_flag_clear(&CMBusy);CMInside=false;
    CMLTargets=[NSMutableArray array];CMLCache=[NSMutableArray array];CMLLoadingMeshes=nil;CMLSchemas=[NSMutableDictionary dictionary];CMLSchemaStore=[CMLSchemaSnapshot new];CMLDiscoveries=[NSMutableDictionary dictionary];CMLSchemaStepUsed=false;CMLFailureRecords=[NSMutableArray array];CMLFailureKeyCount=0;atomic_store(&CMLFailureSamplesTruncated,false);CMLCachedBytes=0;
#ifdef CORE_FILL_WORK
    CMLPendingMeshes=[NSMutableArray array];CMLActivePending=nil;CMLPendingBytes=0;CMLStaticRemaining=CML_STATIC_WORK_NS;LargeStatic=false;LargeReads=LargeVertexReads=0;
#endif
    CMBase=TestBase;CMReadRemote=Reader;CMProject=Project;CMLock=Lock;CMUnlock=Unlock;for(unsigned i=0;i<3;i++)CMRayFunctions[i]=Ray;
    CMLExpectedContext=(uintptr_t)Context;CMLOriginalDraw=Original;CMLGetDrawList=List;CMLPolygon=Polygon;
    CMTransform identity={{0,0,0,1,0,0,0,0,1,1,1}};CMRHeader h={0};h.vtable=TestBase+0x1000;h.class_address=GameClass;h.internal_index=7;h.name_index=100;memcpy(ComponentData,&h,40);
    memcpy(ComponentData+0x1F0,&identity,44);memcpy(ComponentData+0x828,&Asset,8);ComponentData[0x100]=1;
    uint32_t poseCount=1;memcpy(ComponentData+0x848,&GamePose,8);memcpy(ComponentData+0x850,&poseCount,4);memcpy(ComponentData+0x854,&poseCount,4);
    Context[0x23]=1;float screen[2]={8,8},fov=90;memcpy(Context+4,screen,8);memcpy(Context+0x1F4+48,&fov,4);
    Context[0x20]=1;
    uintptr_t begin=(uintptr_t)ActorRecord,end=begin+sizeof(ActorRecord);memcpy(Context+0x3F0,&begin,8);memcpy(Context+0x3F8,&end,8);uint32_t localTeam=1,team=2;float distance=10;memcpy(Context+0x3A8,&localTeam,4);memcpy(ActorRecord,&GameActor,8);memcpy(ActorRecord+8,&team,4);memcpy(ActorRecord+0x40,&distance,4);memcpy(SourceRecord,&GameActor,8);DrawList[0x30]=4;
    CMFSchema schema={0};schema.visible=(CMFBool){0x100,1,true};schema.hidden=(CMFBool){0x101,1,true};schema.predicted_found=true;schema.predicted_lod=0x900;CMLSchemas[@(GameClass)]=[NSData dataWithBytes:&schema length:sizeof(schema)];CMLSchemaStore.schemas=CMLSchemas;
    CMLMesh *mesh=[CMLMesh new];mesh.key=(CMGKey){.asset=Asset};mesh.vertices=3;mesh.vertexStride=24;mesh.bones=1;mesh.sectionCount=1;mesh.indexCount=3;mesh.indexWidth=2;
    uint8_t vertices[72]={0},weights[24]={0};const CMVec3 positions[3]={{10,-7.5f,7.5f},{10,7.5f,7.5f},{10,-7.5f,-7.5f}};for(unsigned i=0;i<3;i++){memcpy(vertices+i*24+8,&positions[i],12);weights[i*8+4]=255;}
    memcpy(VertexData,vertices,72);memcpy(WeightData,weights,24);
    uint16_t indices[]={0,1,2},palettes[256]={0};CMSSection section={.triangle_count=1,.vertex_count=3,.bone_count=1};CMSMatrix inverse=CMSIdentity();uint32_t info[4]={100,0,UINT32_MAX,0};
    mesh.vertexData=[NSData dataWithBytes:vertices length:72];mesh.weightData=[NSData dataWithBytes:weights length:24];mesh.indexData=[NSData dataWithBytes:indices length:6];mesh.sectionData=[NSData dataWithBytes:&section length:sizeof(section)];mesh.paletteData=[NSData dataWithBytes:palettes length:512];mesh.inverseData=[NSData dataWithBytes:&inverse length:sizeof(inverse)];mesh.boneInfo=[NSData dataWithBytes:info length:16];
    mesh.triangleData=[NSData dataWithBytes:WideTriangleData length:12];
    CMLTarget *target=[CMLTarget new];target.component=GameComponent;target.actor=GameActor;target.asset=Asset;target.poseTime=target.metadataTime=TestTime;target.count=1;target.stride=48;target.transform=identity;
    uint8_t raw[48]={0};memcpy(raw,&identity,44);target.pose=[NSData dataWithBytes:raw length:48];target.meshes=@[mesh];target.identity=h;CMLReadDeadline=TestTime+12000000;CMLReads=0;target.topology=CMLTopology(ComponentData);
    NSMutableData *gpu=[NSMutableData dataWithLength:sizeof(CMGDescriptors)],*binding=[NSMutableData dataWithLength:sizeof(CMBBinding)],*refs=[NSMutableData dataWithLength:sizeof(CMRReferences)],*names=[NSMutableData dataWithLength:sizeof(CMNNames)];
    CMGDescriptors *g=gpu.mutableBytes;g->count=1;g->descriptors[0].asset_address=Asset;g->descriptors[0].asset_index=0;g->descriptors[0].lod_count=2;g->descriptors[0].name_index=100;
    CMBBinding *b=binding.mutableBytes;b->assets.count=1;b->assets.references[0].address=Asset;b->link_count=1;b->links[0]=(CMBLink){GameComponent,Asset,0};
    target.gpu=gpu;target.binding=binding;target.refs=refs;target.names=names;[CMLTargets addObject:target];
#ifdef CORE_ACTOR_TRACE
    Context[0x3B8]=1;ActorRecord[0x4C]=1;uintptr_t sourceBegin=(uintptr_t)SourceRecord,sourceEnd=sourceBegin+sizeof(SourceRecord);memcpy(Context+0x3C0,&sourceBegin,8);memcpy(Context+0x3C8,&sourceEnd,8);memcpy(SourceRecord+8,&team,4);
#endif
}
static void Array(void *data,unsigned offset,uint64_t address,int32_t count){memcpy((uint8_t *)data+offset,&address,8);memcpy((uint8_t *)data+offset+8,&count,4);memcpy((uint8_t *)data+offset+12,&count,4);}
static CMLTarget *Reusable(void){
    ResourceMemory=true;memset(AssetData,0,sizeof(AssetData));memset(RenderData,0,sizeof(RenderData));memset(LODData,0,sizeof(LODData));memset(IndexData,0,sizeof(IndexData));memset(SectionsData,0,sizeof(SectionsData));
    CMLTarget *target=CMLTargets.firstObject;NSMutableData *binding=[target.binding mutableCopy],*names=[target.names mutableCopy],*gpu=[NSMutableData dataWithLength:sizeof(CMGDescriptors)];CMBBinding *b=binding.mutableBytes;CMNNames *n=names.mutableBytes;
    CMRReference *asset=&b->assets.references[0];asset->header=target.identity;asset->header.name_index=200;asset->header.internal_index=8;asset->class_header_valid=true;asset->class_header.name_index=999;asset->prefix_bytes=sizeof(AssetData);memcpy(AssetData,&asset->header,40);memcpy(AssetData+0x38,&RenderAddress,8);float extent[3]={20,20,20};memcpy(AssetData+0x7C,extent,12);memcpy(asset->prefix,AssetData,sizeof(AssetData));
    n->count=1;n->names[0].index=999;n->names[0].valid=true;memcpy(n->names[0].text,"SkeletalMesh",13);
    Array(RenderData,0,LODsAddress,1);memcpy(LODArray,&LODAddress,8);Array(LODData,0,SectionsAddress,1);LODData[0x50]=2;memcpy(LODData+0x58,&IndexAddress,8);
    uint32_t count=3,vertices=24,weights=8;uint64_t vertex=0x600000000ULL,weight=0x600010000ULL,index=0x600020000ULL;
    memcpy(LODData+0x60,&count,4);memcpy(LODData+0xC8,&vertex,8);memcpy(LODData+0xD0,&vertices,4);memcpy(LODData+0xD4,&count,4);memcpy(LODData+0x148,&weight,8);memcpy(LODData+0x150,&weights,4);memcpy(LODData+0x154,&count,4);Array(IndexData,0x48,index,3);
    uint32_t triangles=1;memcpy(SectionsData+8,&triangles,4);memcpy(SectionsData+0x70,&count,4);Array(SectionsData,0x60,0x600030000ULL,1);
    CMGCollect(b,n,Reader,gpu.mutableBytes);assert(((CMGDescriptors *)gpu.mutableBytes)->count==1);target.binding=binding;target.names=names;target.gpu=gpu;Reads=0;return target;
}
static void ReferenceEvidence(CMLTarget *target) {
    NSMutableData *data=[NSMutableData dataWithLength:sizeof(CMEMeshEvidence)];CMEMeshEvidence *e=data.mutableBytes;e->resource_count=1;CMEResource *r=&e->resources[0];r->asset_index=0;r->count=1;r->info_bytes=16;r->pose_bytes=48;r->hierarchy_valid=r->reference_pose_valid=true;
    uint32_t info[4]={100,0,UINT32_MAX,0};CMTransform identity={{0,0,0,1,0,0,0,0,1,1,1}};memcpy(r->info,info,16);memcpy(r->pose,&identity,44);target.evidence=data;
}
static void AddChild(CMLTarget *target) {
    memcpy(ChildData,ComponentData,sizeof(ChildData));CMRHeader header;memcpy(&header,ChildData,40);header.internal_index=8;memcpy(ChildData,&header,40);memcpy(ChildData+0x828,&ChildAsset,8);memcpy(ChildData+0x848,&ChildPose,8);
    NSMutableData *binding=[target.binding mutableCopy],*refs=[target.refs mutableCopy],*gpu=[target.gpu mutableCopy];CMBBinding *b=binding.mutableBytes;CMRReferences *r=refs.mutableBytes;CMGDescriptors *g=gpu.mutableBytes;
    b->assets.count=2;b->assets.references[1].address=ChildAsset;b->link_count=2;b->links[1]=(CMBLink){ChildComponent,ChildAsset,1};r->count=1;r->references[0].address=ChildComponent;r->references[0].header=header;r->references[0].prefix_bytes=4096;
    g->count=2;g->descriptors[1]=g->descriptors[0];g->descriptors[1].asset_index=1;g->descriptors[1].asset_address=ChildAsset;target.binding=binding;target.refs=refs;target.gpu=gpu;
    CMLMesh *source=target.meshes.firstObject,*child=[CMLMesh new];child.key=(CMGKey){.asset=ChildAsset};child.vertices=3;child.vertexStride=24;child.bones=1;child.sectionCount=1;child.indexCount=3;child.indexWidth=2;
    child.vertexData=source.vertexData;child.weightData=source.weightData;child.indexData=source.indexData;child.triangleData=source.triangleData;child.sectionData=source.sectionData;child.paletteData=source.paletteData;child.inverseData=source.inverseData;child.boneInfo=source.boneInfo;target.meshes=@[source,child];
}
static CMLTarget *WithStride(uint32_t stride) {
    Setup();CMLTarget *target=Reusable();ReferenceEvidence(target);CMLMesh *original=target.meshes.firstObject;
    memset(VertexData,0xD3,sizeof(VertexData));for(unsigned i=0;i<3;i++)memcpy(VertexData+(size_t)i*stride,(const uint8_t *)original.vertexData.bytes+i*24,20);VertexStride=stride;
    NSMutableData *gpu=[target.gpu mutableCopy];((CMGDescriptors *)gpu.mutableBytes)->descriptors[0].vertex_stride=stride;memcpy(LODData+0xD0,&stride,4);target.gpu=gpu;target.meshes=@[];return target;
}
static void Draw(void){errno=ERANGE;CoreModelRenderDispatch(Context,TestBase+0x21BC4);assert(errno==ERANGE&&!LockDepth&&!CMInside&&DrawList[0x30]==4);}
#ifdef CORE_FILL_WORK
static CMLTarget *LargeStaticTarget(void) {
    Setup();CMLTarget *target=Reusable();ReferenceEvidence(target);target.meshes=@[];
    NSMutableData *gpu=[target.gpu mutableCopy];CMGDescriptor *d=&((CMGDescriptors *)gpu.mutableBytes)->descriptors[0];d->vertex_count=2048;uint32_t vertices=2048;memcpy(d->sections+0x70,&vertices,4);target.gpu=gpu;
    memset(LargeVertices,0,sizeof(LargeVertices));memset(LargeWeights,0,sizeof(LargeWeights));
    for(unsigned i=0;i<2048;i++){float p[3]={10,(float)(i%3),0};memcpy(LargeVertices+(size_t)i*24+8,p,12);LargeWeights[(size_t)i*8+4]=255;}
    LargeStatic=true;return target;
}
static void CheckStaticResume(void) {
    CMLTarget *target=LargeStaticTarget();const CMGDescriptor *d=&((const CMGDescriptors *)target.gpu.bytes)->descriptors[0];
    assert(!CMLLiveMesh(target,d)&&LargeReads==3&&LargeVertexReads==3&&!CMLStaticRemaining&&CMLPendingBytes==sizeof(LargeVertices)&&CMLPendingMeshes.count==1&&!CMLActivePending);
    unsigned before=Reads;CMGDescriptor second=*d;second.asset_address+=0x10000;assert(!CMLLiveMesh(target,&second)&&Reads==before); // All targets share the same static-work allowance.
    CMLStaticRemaining=CML_STATIC_WORK_NS;CMLReadDeadline=TestTime+12000000ULL;CMLMesh *mesh=CMLLiveMesh(target,d);
    assert(mesh&&mesh.vertices==2048&&LargeReads==6&&LargeVertexReads==3&&!CMLPendingBytes&&!CMLPendingMeshes.count&&!CMLActivePending);
    assert(mesh.vertexData.length==sizeof(LargeVertices)&&!memcmp(mesh.vertexData.bytes,LargeVertices,sizeof(LargeVertices))&&mesh.weightData.length==sizeof(LargeWeights));
    assert(atomic_load(&CMLStats.staticReuseBytes)>=sizeof(LargeVertices));
    target=LargeStaticTarget();d=&((const CMGDescriptors *)target.gpu.bytes)->descriptors[0];assert(!CMLLiveMesh(target,d)&&LargeVertexReads==3);
    CMGDescriptor changed=*d;changed.bounds_extent[0]+=1;CMLStaticRemaining=CML_STATIC_WORK_NS;CMLReadDeadline=TestTime+12000000ULL;
    assert(!CMLLiveMesh(target,&changed)&&LargeVertexReads==6&&CMLPendingMeshes.count==1&&CMLPendingBytes==sizeof(LargeVertices)); // Changed descriptor discards the previous chunks.
    Setup();CMLPendingMesh *staging=[CMLPendingMesh new];staging.chunks=[NSMutableDictionary dictionary];[CMLPendingMeshes addObject:staging];CMLActivePending=staging;uint8_t chunk[16384]={0};
    for(unsigned i=0;i<257;i++)CMLStoreStaticChunk(0x700000000ULL+(uint64_t)i*16384,chunk,sizeof(chunk));
    assert(CMLPendingBytes==CML_STATIC_STAGE_BYTES&&staging.chunks.count==256);CMLActivePending=nil;CMLDropPending(staging);assert(!CMLPendingBytes);
}
#endif
#ifdef CORE_ACTOR_TRACE
static void CheckActorTrace(void) {
    Setup();Context[0x20]=0;Draw();assert(!Reads&&!Fills&&atomic_load(&CAVGeneration)==2&&!atomic_load(&CAVNativeActive));
    CAVSample((uintptr_t)Context);assert(!Reads&&CAVSamples==1&&[CAVReport[@"source_enemy_candidates"] intValue]==1&&[CAVReport[@"cache_0_to_300m"] intValue]==1&&CAVEvents.count==1);
    NSNumber *id=CAVEvents.lastObject[@"target_id"];float distance=450;memcpy(ActorRecord+0x40,&distance,4);TestTime+=1000000000ULL;Draw();CAVSample((uintptr_t)Context);
    assert([CAVReport[@"cache_300_to_600m"] intValue]==1&&[CAVReport[@"cache_300_to_600m_native_projection_valid"] intValue]==1&&CAVEvents.count==2&&[CAVEvents.lastObject[@"target_id"] isEqual:id]);
    ActorRecord[0x4C]=0;CAVSample((uintptr_t)Context);assert(CAVEvents.count==3&&![CAVEvents.lastObject[@"native_projection_valid"] boolValue]&&[CAVReport[@"cache_300_to_600m_native_projection_valid"] intValue]==0);
    uintptr_t begin=(uintptr_t)ActorRecord,end=begin+sizeof(ActorRecord);memcpy(Context+0x3F8,&begin,8);TestTime+=1000000000ULL;Draw();CAVSample((uintptr_t)Context);
    NSDictionary *event=CAVEvents.lastObject;assert([CAVReport[@"source_without_cache"] intValue]==1&&[event[@"source_present"] boolValue]&&![event[@"cache_present"] boolValue]&&event[@"current_cached_distance_metres"]==NSNull.null&&[event[@"last_cached_distance_metres"] floatValue]==450&&[event[@"last_cached_age_ms"] intValue]==1000);
    uintptr_t sourceBegin=(uintptr_t)SourceRecord;memcpy(Context+0x3C8,&sourceBegin,8);TestTime+=1000000000ULL;Draw();CAVSample((uintptr_t)Context);assert(![CAVEvents.lastObject[@"source_present"] boolValue]&&![CAVEvents.lastObject[@"cache_present"] boolValue]);
    uint64_t samples=CAVSamples;atomic_store(&CAVNativeActive,1);CAVSample((uintptr_t)Context);atomic_store(&CAVNativeActive,0);assert(CAVSamples==samples&&CAVBusy==1);
    memcpy(Context+0x3F8,&end,8);TraceMutate=true;CAVSample((uintptr_t)Context);assert(CAVSamples==samples&&CAVBusy==2); // A collector epoch changes during the vector copy: no false disappearance event.
    end++;memcpy(Context+0x3F8,&end,8);CAVSample((uintptr_t)Context);assert(CAVSamples==samples&&CAVFailures==1);
    end=begin+513*0x1F8;memcpy(Context+0x3F8,&end,8);CAVSample((uintptr_t)Context);assert(CAVSamples==samples&&CAVFailures==2&&!Reads);
    TestTime+=1000000001ULL;CAVSample((uintptr_t)Context);assert(CAVSamples==samples&&CAVBusy==3);
    Setup();Context[0x20]=0;for(unsigned i=0;i<70;i++){distance=i%2?450:150;memcpy(ActorRecord+0x40,&distance,4);Draw();CAVSample((uintptr_t)Context);}assert(CAVEvents.count==CAV_EVENTS&&!Reads&&!Fills);
    assert(CAVBucket(300)==1&&CAVBucket(300.01f)==2&&CAVBucket(600)==2&&CAVBucket(600.01f)==3&&!CAVBucket(NAN)&&!CAVBucket(-1));
    NSData *json=[NSJSONSerialization dataWithJSONObject:CAVStatus() options:0 error:NULL];assert(json.length&&json.length<30000);puts("PASS: background actor source/cache stage events, 300/600m boundaries, cached-range age, fill-off zero remote reads, original-draw epoch rejection, malformed/oversized/stale snapshots and bounded history");
}
#endif
#ifdef CORE_GROUND_BOW_TRACE
#include "test_ground_bow_trace.inc"
#endif
int main(void){@autoreleasepool {
#ifdef CORE_GROUND_BOW_TRACE
    CheckGroundBowTrace();
#endif
#ifdef CORE_ACTOR_TRACE
    CheckActorTrace();
#endif
#ifdef CORE_FILL_WORK
    CheckStaticResume();
#endif
#ifdef CORE_OVERLAY_LATENCY
    assert(!CMStartDetailedCapture());
    Setup();Context[0x20]=0;NativeDelay=3000000ULL;uint64_t offCallbacks=atomic_load(&CMLStats.fillOffCallbacks),offNative=atomic_load(&CMLStats.fillOffNativeTotal),offTotal=atomic_load(&CMLStats.fillOffCallbackTotal);
    Draw();assert(OriginalCalls==1&&!Fills&&!Reads&&!RawCalls&&!SceneLocks&&!CMObserverEnabled());
    assert(atomic_load(&CMLStats.fillOffCallbacks)==offCallbacks+1&&atomic_load(&CMLStats.fillOffNativeTotal)==offNative+3000000ULL&&atomic_load(&CMLStats.fillOffCallbackTotal)==offTotal+3000000ULL);
    CMLTarget *disabledTarget=CMLTargets.firstObject;disabledTarget.poseTime=0;CMLObserve(GameComponent,GameActor,GameComponent+0x1F0,GamePose,1,48);assert(!Reads&&!disabledTarget.poseTime);
    uint64_t offGaps=atomic_load(&CMLStats.fillOffGapSamples);TestTime+=16000000ULL;Draw();assert(OriginalCalls==2&&!Reads&&!Fills&&atomic_load(&CMLStats.fillOffGapSamples)==offGaps+1);
    Context[0x20]=1;disabledTarget.poseTime=TestTime;uint64_t onCallbacks=atomic_load(&CMLStats.fillOnCallbacks),onNative=atomic_load(&CMLStats.fillOnNativeTotal),onTotal=atomic_load(&CMLStats.fillOnCallbackTotal);
    Draw();assert(OriginalCalls==3&&Fills&&Reads&&RawCalls&&CMObserverEnabled()&&atomic_load(&CMLStats.fillOnCallbacks)==onCallbacks+1&&atomic_load(&CMLStats.fillOnNativeTotal)==onNative+3000000ULL&&atomic_load(&CMLStats.fillOnCallbackTotal)==onTotal+3000000ULL);
    // Native-only slowness remains visible in separate timing; disabling fill
    // is never reported as proving that the whole overlay latency is fixed.
    Setup();Context[0x20]=0;NativeDelay=40000000ULL;Draw();assert(!Reads&&!Fills&&atomic_load(&CMLStats.fillOffNativeMaximum)>=40000000ULL);
#else
    assert(CMStartDetailedCapture());
#endif
    Setup();Draw();assert(OriginalCalls==1&&Fills==6&&WhiteFills>0&&RedFills>0&&SceneLocks==3&&RawCalls==51);
    Setup();CheckObserverProgress=true;CameraMotion=5;Draw();assert(Fills&&CameraMoved&&!memcmp(LastProjectedCamera,Context+0x1F4,52));
    Setup();CheckObserverProgress=true;CMLIntersect=NULL;Draw();assert(Fills); // The unbatched query also leaves acquisition unblocked.
    Setup();uint64_t busy=atomic_load(&CMLStats.drawBusyFrames);pthread_mutex_lock(&CMLDrawMutex);Draw();assert(!Fills&&atomic_load(&CMLStats.drawBusyFrames)==busy+1);pthread_mutex_unlock(&CMLDrawMutex);Draw();assert(Fills);
    for(unsigned motion=1;motion<=7;motion++){
        Setup();CameraMotion=motion;uint64_t mismatches=atomic_load(&CMLStats.projectionMismatch);
        for(unsigned frame=0;frame<6;frame++){
            float camera[13]={0};camera[0]=frame*.05f;camera[7]=frame*.25f;camera[12]=90;
            memcpy(Context+0x1F4,camera,sizeof(camera));CameraMoved=false;
            unsigned fills=Fills,white=WhiteFills,red=RedFills;Draw();
            assert(CameraMoved&&Fills>fills&&WhiteFills>white&&RedFills>red&&atomic_load(&CMLStats.projectionMismatch)==mismatches&&!memcmp(LastProjectedCamera,Context+0x1F4,52));
            TestTime+=1000000ULL;
        }
    }
    Setup();CameraMotion=4;WrongProjection=true;Draw();assert(CameraMoved&&!Fills); // A real projection error must still reject the mesh.
    Setup();BlockScene=2;Draw();assert(Fills==6&&WhiteFills>0&&RedFills>0&&SceneLocks==3&&RawCalls==63);
    Setup();CMLIntersect=NULL;Draw();assert(Fills==6&&!SceneLocks&&!RawCalls);
    Setup();WrongRaw=true;Draw();assert(Fills==6&&WhiteFills>0&&RedFills>0&&SceneLocks==1&&atomic_load(&CMLBatchDisabled));
    Setup();UnreadableScene=true;Draw();assert(!Fills&&SceneLocks==1&&!RawCalls);
    Setup();uintptr_t noHandle=0;memcpy(SceneData[1]+8,&noHandle,8);Draw();assert(Fills==6&&SceneLocks==3&&RawCalls==36);
    Setup();AddChild(CMLTargets.firstObject);SlowProjection=true;uint64_t projectedBefore=atomic_load(&CMLStats.meshFrames);Draw();assert(Fills==6&&Reads==4&&atomic_load(&CMLStats.meshFrames)==projectedBefore+2); // CPU time on the first mesh cannot expire the second mesh's state/pose reads.
    for(uint32_t stride=24;stride<=32;stride+=4){CMLTarget *target=WithStride(stride);uint64_t white=atomic_load(&CMLStats.white),red=atomic_load(&CMLStats.red);Draw();CMLMesh *mesh=target.meshes.firstObject;
        assert(Fills==6&&mesh.vertexStride==stride&&mesh.vertexData.length==stride*3&&atomic_load(&CMLStats.white)==white+15&&atomic_load(&CMLStats.red)==red+6);unsigned reads=Reads;Draw();assert(Fills==12&&Reads==reads+2);}
    CMLTarget *badPositions=WithStride(28);float outside=1000;memcpy(VertexData+8,&outside,4);uint64_t positionsRejected=atomic_load(&CMLLoadFailures[CML_LOAD_POSITION_SHAPE]);Draw();assert(!Fills&&!badPositions.meshes.count&&atomic_load(&CMLLoadFailures[CML_LOAD_POSITION_SHAPE])==positionsRejected+1);
    Setup();CMLTarget *unsupported=Reusable();ReferenceEvidence(unsupported);CMGDescriptor unsupportedDescriptor=((const CMGDescriptors *)unsupported.gpu.bytes)->descriptors[0];unsupportedDescriptor.vertex_stride=36;unsigned layoutReads=Reads;uint64_t layoutRejected=atomic_load(&CMLLoadFailures[CML_LOAD_LAYOUT]);assert(!CMLLoadMesh(&unsupportedDescriptor,CMEResourceAt(unsupported.evidence.bytes,0))&&Reads==layoutReads&&atomic_load(&CMLLoadFailures[CML_LOAD_LAYOUT])==layoutRejected+1);
    unsupported.meshes=@[];assert(!CMLLiveMesh(unsupported,&unsupportedDescriptor));NSArray *firstFailures=CMLSchemaStore.meshFailures;assert(firstFailures.count==1&&[firstFailures[0][@"detail"] isEqualToString:@"vertex_stride"]&&[firstFailures[0][@"vertex_stride"] intValue]==36);
    assert(!CMLLiveMesh(unsupported,&unsupportedDescriptor)&&CMLSchemaStore.meshFailures.count==1);unsupportedDescriptor.vertex_stride=24;unsupportedDescriptor.weight_stride=12;assert(!CMLLiveMesh(unsupported,&unsupportedDescriptor)&&CMLSchemaStore.meshFailures.count==2&&firstFailures.count==1);
    for(unsigned i=0;i<20;i++){unsupportedDescriptor.lod_address=LODAddress+(i+1)*0x10000;assert(!CMLLiveMesh(unsupported,&unsupportedDescriptor));}assert(CMLSchemaStore.meshFailures.count==16&&atomic_load(&CMLFailureSamplesTruncated));
    NSString *diagnosticFile=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];pthread_mutex_lock(&CMLMutex);CMLWriteStatus([NSURL fileURLWithPath:diagnosticFile]);pthread_mutex_unlock(&CMLMutex);NSDictionary *diagnosticStatus=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:diagnosticFile] options:0 error:NULL];NSArray *reportedFailures=diagnosticStatus[@"mesh_load_failure_samples"];assert(reportedFailures.count==16&&[diagnosticStatus[@"mesh_load_failure_samples_truncated"] boolValue]);[[NSFileManager defaultManager]removeItemAtPath:diagnosticFile error:NULL];
    Setup();CMLTarget *prefixTarget=CMLTargets.firstObject;assert(CMLStateBytes(GameClass,4096)==0x910);NSMutableData *schemaBytes=[CMLSchemas[@(GameClass)] mutableCopy];CMFSchema *bounded=schemaBytes.mutableBytes;bounded->hide_skin=(CMFBool){0x950,8,true};CMLSchemas[@(GameClass)]=schemaBytes;assert(CMLStateBytes(GameClass,4096)==0x960);
    NSMutableDictionary *state=[NSMutableDictionary dictionary];assert(CMLState(prefixTarget,0,state).length==0x960);bounded->predicted_lod=4092;CMLSchemas[@(GameClass)]=schemaBytes;assert(CMLStateBytes(GameClass,4096)==4096);[CMLSchemas removeAllObjects];assert(CMLStateBytes(GameClass,4096)==4096);
    Setup();CMLTarget *schemaTarget=CMLTargets.firstObject;CMRReference schemaComponent={0};schemaComponent.header.class_address=GameClass;CMNNames schemaNames={0};
    CMLDiscoveries[@(GameClass)]=[CMLSchemaDiscovery new];CMLReadDeadline=TestTime+5000000ULL;CMLReads=777;uint64_t schemaSteps=atomic_load(&CMLStats.schemaSteps);
    CMFSchema recovered=CMLSchemaFor(&schemaComponent,&schemaNames);assert(recovered.failures==1&&recovered.visible.found&&recovered.predicted_found&&CMLReadDeadline==TestTime+5000000ULL&&CMLReads==777&&atomic_load(&CMLStats.schemaSteps)==schemaSteps+1);
    CMLSchemaFor(&schemaComponent,&schemaNames);assert(atomic_load(&CMLStats.schemaSteps)==schemaSteps+1); // At most one discovery step in a frame.
    for(unsigned i=0;i<40;i++){schemaComponent.header.class_address=GameClass+(i+1)*0x10000;CMLSchemaFor(&schemaComponent,&schemaNames);}assert(CMLDiscoveries.count==1&&CMLSchemas.count==1);schemaComponent.header.class_address=GameClass; // Deferred classes allocate no pending state.
    SchemaMemory=true;CMLSchemaStepUsed=false;CMLSchemaFor(&schemaComponent,&schemaNames);assert(atomic_load(&CMLStats.schemaSteps)==schemaSteps+1); // Failed reads back off.
    TestTime+=1000000000ULL;CMLSchemaStepUsed=false;recovered=CMLSchemaFor(&schemaComponent,&schemaNames);assert(recovered.scan_finished&&!recovered.failures&&recovered.visible.found&&recovered.predicted_found&&!CMLDiscoveries.count&&schemaTarget==CMLTargets.firstObject);
    Setup();[CMLSchemas removeAllObjects];for(unsigned attempt=0;attempt<3;attempt++){CMLSchemaStepUsed=false;CMLSchemaFor(&schemaComponent,&schemaNames);TestTime+=1000000000ULL;}assert(!CMLDiscoveries.count);schemaSteps=atomic_load(&CMLStats.schemaSteps);CMLSchemaStepUsed=false;CMLSchemaFor(&schemaComponent,&schemaNames);assert(atomic_load(&CMLStats.schemaSteps)==schemaSteps); // Permanent faults cannot trigger endless scans.
    Setup();CMLTarget *reused=Reusable();assert(CMLReuseMetadata(reused)&&Reads==7); // No bone-name, reference-pose, vertex or weight reads.
    LODData[0x2A0]=1;assert(CMLReuseMetadata(reused)); // Unrelated mutable GPU state must not trigger a reload.
    LODData[0xC8]^=8;assert(!CMLReuseMetadata(reused));LODData[0xC8]^=8;
    IndexData[0x48]^=8;assert(!CMLReuseMetadata(reused));IndexData[0x48]^=8;
    SectionsData[20]=1;assert(!CMLReuseMetadata(reused));SectionsData[20]=0;
    AssetData[0x38]^=8;assert(!CMLReuseMetadata(reused));AssetData[0x38]^=8;
    AssetData[0x1C]=1;assert(!CMLReuseMetadata(reused));AssetData[0x1C]=0;
    ComponentData[0x1C]=1;assert(!CMLReuseMetadata(reused));ComponentData[0x1C]=0;
    ComponentData[0x178]=1;assert(!CMLReuseMetadata(reused));ComponentData[0x178]=0;
    FailResourceRead=true;assert(!CMLReuseMetadata(reused));FailResourceRead=false;
    Setup();reused=Reusable();CMLTarget *deferred=[CMLTarget new];deferred.meshes=reused.meshes;deferred.gpu=reused.gpu;deferred.metadataTime=reused.metadataTime=TestTime-1000000001ULL;bool checked=false;
    uint64_t checks=atomic_load(&CMLStats.metadataChecks);assert(CMLPrepareMetadata(reused,TestTime,&checked)&&checked);assert(CMLPrepareMetadata(deferred,TestTime,&checked)&&atomic_load(&CMLStats.metadataChecks)==checks+1);
    checked=false;deferred.component=GameComponent;deferred.asset=Asset;deferred.count=1;NSArray *preserved=deferred.meshes;assert(!CMLPrepareMetadata(deferred,TestTime,&checked)&&deferred.metadataInvalid&&deferred.meshes==preserved);checked=false;assert(!CMLPrepareMetadata(deferred,TestTime,&checked)&&!checked);
    Setup();reused=Reusable();CMLTarget *second=[CMLTarget new];second.component=reused.component;second.actor=reused.actor;second.asset=reused.asset;second.count=1;second.stride=48;second.poseTime=TestTime;second.identity=reused.identity;
    second.meshes=reused.meshes;second.binding=reused.binding;second.names=reused.names;second.gpu=reused.gpu;second.refs=reused.refs;second.topology=reused.topology;second.metadataTime=reused.metadataTime=TestTime-1000000001ULL;[CMLTargets addObject:second];
    uint64_t meshes=atomic_load(&CMLStats.meshFrames),deferredCount=atomic_load(&CMLStats.metadataDeferred);Draw();assert(Fills==6&&atomic_load(&CMLStats.meshFrames)==meshes+2&&atomic_load(&CMLStats.metadataDeferred)==deferredCount+1);
    Setup();reused=Reusable();ReferenceEvidence(reused);reused.meshes=@[];CMLMesh *skeleton=CMLMeshFor(reused,0);assert(skeleton.bones==1&&!skeleton.vertexData&&CMLMeshFor(reused,0)==skeleton&&!CMLCache.count);
    NSMutableDictionary *states=[NSMutableDictionary dictionary],*poses=[NSMutableDictionary dictionary];assert(CMLPoseMatrices(reused,0,states,poses,0).length==sizeof(CMSMatrix)&&!reused.meshes.count); // A hidden master needs bones, not static triangle buffers.
    uint64_t decodedBefore=atomic_load(&CMLStats.indexDecodes);Draw();assert(Fills==6&&reused.meshes.count==1&&CMLCache.count==1&&atomic_load(&CMLStats.indexDecodes)==decodedBefore+3);unsigned reads=Reads;Draw();assert(Fills==12&&CMLCache.count==1&&Reads==reads+2&&atomic_load(&CMLStats.indexDecodes)==decodedBefore+3); // The next frame reads state and animation only, with no repeated static index decoding.
    assert(((CMLMesh *)reused.meshes.firstObject).triangleData.length==12&&!memcmp(((CMLMesh *)reused.meshes.firstObject).triangleData.bytes,WideTriangleData,12));
    CMGDescriptor otherSlot=((const CMGDescriptors *)reused.gpu.bytes)->descriptors[0];otherSlot.asset_index=7;reads=Reads;assert(CMLLoadMesh(&otherSlot,NULL)==reused.meshes.firstObject&&CMLCache.count==1&&Reads==reads); // Shared assets can occupy different attachment slots.
    CMGDescriptor wide=otherSlot;wide.index_width=4;CMLMesh *wideMesh=CMLLoadMesh(&wide,CMEResourceAt(reused.evidence.bytes,0));assert(wideMesh.triangleData.length==12&&!memcmp(wideMesh.triangleData.bytes,WideTriangleData,12));
    uint64_t budgetFailures=atomic_load(&CMLLoadFailures[CML_LOAD_READ_BUDGET]);wide.vertex_address+=8;CMLReadDeadline=TestTime;assert(!CMLLoadMesh(&wide,CMEResourceAt(reused.evidence.bytes,0))&&atomic_load(&CMLLoadFailures[CML_LOAD_READ_BUDGET])==budgetFailures+1);
    Setup();reused=Reusable();ReferenceEvidence(reused);reused.meshes=@[];ComponentData[0x101]=1;Draw();assert(!Fills&&!CMLCache.count&&!reused.meshes.count);
    Setup();reused=Reusable();ReferenceEvidence(reused);reused.meshes=@[];NSMutableData *multiple=[reused.gpu mutableCopy];CMGDescriptors *descriptors=multiple.mutableBytes;descriptors->count=2;descriptors->descriptors[1]=descriptors->descriptors[0];descriptors->descriptors[1].lod_index=1;descriptors->descriptors[0].lod_count=descriptors->descriptors[1].lod_count=2;reused.gpu=multiple;
    uint64_t lodSkips=atomic_load(&CMLStats.lodDescriptorSkips);Draw();assert(Fills==6&&CMLCache.count==1&&((CMLMesh *)reused.meshes.firstObject).lod==0);uint32_t requestedLOD=1;memcpy(ComponentData+0x900,&requestedLOD,4);Draw();assert(Fills==12&&reused.meshes.count==1&&((CMLMesh *)reused.meshes.firstObject).lod==1&&CMLCache.count==2&&atomic_load(&CMLStats.lodDescriptorSkips)==lodSkips+2);
    Setup();uint32_t ownTeam=0;uint64_t chunks=atomic_load(&CMLStats.actorSnapshotReads);NSData *snapshot=CMLReadActors((uintptr_t)Context,&ownTeam);assert(snapshot.length==504&&ownTeam==1&&CMLActorEligible(snapshot,ownTeam,GameActor)&&atomic_load(&CMLStats.actorSnapshotReads)==chunks+1);
    ReallocatedActors=true;assert(!CMLReadActors((uintptr_t)Context,&ownTeam));ReallocatedActors=false;Context[0x3F8]++;assert(!CMLReadActors((uintptr_t)Context,&ownTeam));
    Setup();CMLTarget *pinnedTarget=CMLTargets.firstObject;CMLMesh *pinned=pinnedTarget.meshes.firstObject,*old=[CMLMesh new],*recent=[CMLMesh new];
    pinned.byteCost=12*1024*1024;old.byteCost=8*1024*1024;recent.byteCost=4*1024*1024;[CMLCache addObjectsFromArray:@[old,pinned,recent]];CMLCachedBytes=CML_CACHE_BYTES;
    assert(CMLMakeCacheRoom(12*1024*1024)&&CMLCache.count==1&&CMLCache.firstObject==pinned&&CMLCachedBytes==pinned.byteCost);
    assert(!CMLMakeCacheRoom(13*1024*1024)&&pinnedTarget.meshes.count==1);
    CMLLoadingMeshes=@[pinned];pinnedTarget.meshes=nil;assert(!CMLMakeCacheRoom(13*1024*1024));CMLLoadingMeshes=nil;pinnedTarget.meshes=@[pinned];
    CMLTrimStaleTargets(TestTime+2000000001ULL);assert(!pinnedTarget.meshes&&!pinnedTarget.binding&&CMLMakeCacheRoom(13*1024*1024)&&!CMLCache.count&&!CMLCachedBytes);
    for(unsigned i=0;i<96;i++){CMLMesh *entry=[CMLMesh new];entry.byteCost=1;[CMLCache addObject:entry];CMLCachedBytes++;}
    assert(CMLMakeCacheRoom(1)&&CMLCache.count==96); // The observed build-28 ceiling no longer blocks a new outfit.
    Setup();Context[0x23]=0;uint64_t disabled=atomic_load(&CMLStats.disabledFrames);Draw();assert(OriginalCalls==1&&!Fills&&atomic_load(&CMLStats.disabledFrames)==disabled+1);
    Setup();GeometryReady=false;uint64_t unavailable=atomic_load(&CMLStats.geometryUnavailableFrames);Draw();assert(OriginalCalls==1&&!Fills&&!Reads&&atomic_load(&CMLStats.geometryUnavailableFrames)==unavailable+1);
    Setup();ComponentData[0x101]=1;Draw();assert(!Fills);
    Setup();uint32_t team=1;memcpy(ActorRecord+8,&team,4);Draw();assert(!Fills);
    Setup();TestTime+=250000001ULL;Draw();assert(!Fills);
    Setup();WrongProjection=true;Draw();assert(!Fills);
    const float offscreen[][2]={{100,0},{-100,0},{0,100},{0,-100}};for(unsigned i=0;i<4;i++){Setup();CMTransform transform;memcpy(&transform,ComponentData+0x1F0,sizeof(transform));transform.values[5]=offscreen[i][0];transform.values[6]=offscreen[i][1];memcpy(ComponentData+0x1F0,&transform,sizeof(transform));uint64_t rejected=atomic_load(&CMLStats.screenRejectedMeshes),triangles=atomic_load(&CMLStats.triangles);Draw();assert(!Fills&&!RawCalls&&atomic_load(&CMLStats.screenRejectedMeshes)==rejected+1&&atomic_load(&CMLStats.triangles)==triangles);}
    Setup();CMTransform edge;memcpy(&edge,ComponentData+0x1F0,sizeof(edge));edge.values[5]=5;memcpy(ComponentData+0x1F0,&edge,sizeof(edge));Draw();assert(Fills>0&&WhiteFills>0&&RedFills>0); // A mesh straddling the screen edge remains visible.
    Setup();CMLMesh *spanning=((CMLTarget *)CMLTargets.firstObject).meshes.firstObject;NSMutableData *spanningVertices=[spanning.vertexData mutableCopy];for(unsigned i=0;i<3;i++){float yz[2];memcpy(yz,(uint8_t *)spanningVertices.mutableBytes+i*24+12,8);yz[0]*=16;yz[1]*=16;memcpy((uint8_t *)spanningVertices.mutableBytes+i*24+12,yz,8);}spanning.vertexData=spanningVertices;Draw();assert(Fills>0&&RawCalls>0); // All vertices can be outside while the triangle crosses the viewport.
    Setup();CMLMesh *clipped=((CMLTarget *)CMLTargets.firstObject).meshes.firstObject;NSMutableData *clippedVertices=[clipped.vertexData mutableCopy];float behind=-10;memcpy((uint8_t *)clippedVertices.mutableBytes+2*24+8,&behind,4);clipped.vertexData=clippedVertices;Draw();assert(!Fills&&!RawCalls); // Reusing static triangles must still exclude an unprojectable vertex.
    Setup();BadRay=true;Draw();assert(!Fills);
    Setup();FailQueryBudget=true;Draw();assert(!Fills);
    Setup();uint32_t lod=1;memcpy(ComponentData+0x900,&lod,4);Draw();assert(!Fills);
    Setup();uint64_t wrong=Asset+0x10000;memcpy(ComponentData+0x828,&wrong,8);Draw();assert(!Fills);
    Setup();CMLTarget *target=CMLTargets.firstObject;target.poseTime=0;
    uintptr_t f=(uintptr_t)(Frame+256),ret=TestBase+0xD8980,ctx=(uintptr_t)Context,record=(uintptr_t)SourceRecord;uint32_t count=1;
    memcpy((void *)(f+8),&ret,8);memcpy((void *)(f-8),&ctx,8);memcpy((void *)(f-0x48),&GameComponent,8);memcpy((void *)(f-0x20),&record,8);memcpy((void *)(f+16+0x110),&GamePose,8);memcpy((void *)(f+16+0x118),&count,4);
    CoreModelBzeroCapture((void *)(f+16+0xD0),44,f,GameComponent+0x1F0,TestBase+0xE1960);assert(target.poseTime==TestTime&&Reads==3&&target.pose.length==48&&!atomic_load(&CMEnabled));
    errno=ERANGE;unsigned calls=OriginalCalls;assert(CoreModelRenderDispatch(NULL,TestBase+0x1234)==(uintptr_t)getpid()&&errno==ERANGE&&OriginalCalls==calls);
    NSString *file=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];pthread_mutex_lock(&CMLMutex);CMLWriteStatus([NSURL fileURLWithPath:file]);pthread_mutex_unlock(&CMLMutex);
    NSDictionary *status=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:file] options:0 error:NULL];NSArray *reportedSchemas=status[@"reflected_component_schemas"];
    assert([status[@"silhouette_renderer_connected"] boolValue]&&![status[@"full_requested_effect_verified"] boolValue]&&reportedSchemas.count==1&&![status[@"schema_snapshot_busy"] boolValue]&&[status[@"native_ray_result_bytes"] intValue]==96);[[NSFileManager defaultManager]removeItemAtPath:file error:NULL];
    puts("PASS: late camera capture follows 42 motion frames; observer mutex is free during projection and both query paths; overlapping draw callbacks do not queue; genuine projection errors reject; original forwarding, exact mesh/pixel fill, LODs, native ray ABI, metadata, caches, poses, budgets, errno and status");
}return 0;}
