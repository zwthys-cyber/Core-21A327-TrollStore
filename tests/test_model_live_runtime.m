// Production observer -> mesh skinning -> pixel rays -> native fill, with
// independent synthetic memory/functions. Run only in GitHub Actions.
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <assert.h>
#include <errno.h>
static const uintptr_t TestBase=0x100000000ULL;
static const uint64_t GameComponent=0x200001000ULL,GamePose=0x200010000ULL,GameActor=0x200020000ULL,Asset=0x300000000ULL,GameClass=0x400000000ULL;
static uint8_t Context[2048],ComponentData[4096],ActorRecord[504],SourceRecord[24],Frame[2048],DrawList[256],SceneData[3][160];
static uint64_t TestTime=10000000000ULL;
static unsigned OriginalCalls,Fills,WhiteFills,RedFills,LockDepth,Reads,SceneLocks,RawCalls,BlockScene;
static bool BadRay,WrongProjection,FailQueryBudget,WrongRaw,UnreadableScene;
static int TestClock(clockid_t clock,struct timespec *t){(void)clock;if(FailQueryBudget)TestTime+=20000000ULL;t->tv_sec=TestTime/1000000000ULL;t->tv_nsec=TestTime%1000000000ULL;return 0;}
static kern_return_t SelfRead(vm_map_t task,vm_address_t address,vm_size_t size,vm_address_t dest,vm_size_t *copied){
    (void)task;uint64_t value=0;
    if(address==TestBase+0xBD4720)value=0xAA55;
    else if(address==TestBase+0xBD4978)value=0xAA55^48;
    else if(address==TestBase+0xC54459)value=1;
    else if(address==TestBase+0xC15EC8&&size==24){uintptr_t scenes[3]={(uintptr_t)SceneData[0],(uintptr_t)SceneData[1],(uintptr_t)SceneData[2]};memcpy((void *)dest,scenes,24);*copied=24;return KERN_SUCCESS;}
    else if(address>=(uintptr_t)SceneData&&address<(uintptr_t)SceneData+sizeof(SceneData)){
        if(UnreadableScene||size!=8||(address-(uintptr_t)SceneData)%160!=8)return KERN_INVALID_ADDRESS;
        memcpy((void *)dest,(void *)address,8);*copied=8;return KERN_SUCCESS;}
    else {const void *arrays[]={Context,ActorRecord,SourceRecord,Frame,DrawList};const size_t sizes[]={sizeof(Context),sizeof(ActorRecord),sizeof(SourceRecord),sizeof(Frame),sizeof(DrawList)};
        for(unsigned i=0;i<5;i++)if(address>=(uintptr_t)arrays[i]&&size<=sizes[i]&&address-(uintptr_t)arrays[i]<=sizes[i]-size){memcpy((void *)dest,(void *)address,size);*copied=size;return KERN_SUCCESS;}return KERN_INVALID_ADDRESS;}
    if(size>8)return KERN_INVALID_ADDRESS;memcpy((void *)dest,&value,size);*copied=size;return KERN_SUCCESS;
}
#define CORE_MODEL_CAPTURE_TESTING 1
#define CORE_MODEL_RENDER 1
#define clock_gettime TestClock
#define vm_read_overwrite SelfRead
#include "../patch/CoreModelCapture.m"
#undef vm_read_overwrite
#undef clock_gettime
static bool Reader(uint64_t address,void *out,size_t size){Reads++;CoreModelBzeroCapture(out,size,0,address,TestBase+0xE1960);
    if(address>=GameComponent&&size<=sizeof(ComponentData)&&address-GameComponent<=sizeof(ComponentData)-size){memcpy(out,ComponentData+address-GameComponent,size);return true;}
    if(address==GamePose&&size==48){CMTransform t={{0,0,0,1,0,0,0,0,1,1,1}};memcpy(out,&t,sizeof(t));return true;}return false;
}
static CMVec2 Project(const CMVec3 *world,const void *camera,int width,int height){assert(camera==Context+0x1F4);CMPView view;CMPVertex p;assert(CMPMakeView(camera,width,height,&view)&&CMPProject(&view,*world,&p));if(WrongProjection)p.xy.x+=10;return p.xy;}
static void Lock(void *p){if(p==(void *)(TestBase+0xBD4F78)){assert(!LockDepth);LockDepth=1;return;}
    assert(LockDepth==1);for(unsigned i=0;i<3;i++)if(p==SceneData[i]+0x58){LockDepth|=2U<<i;SceneLocks++;return;}assert(false);}
static void Unlock(void *p){if(p==(void *)(TestBase+0xBD4F78)){assert(LockDepth==1);LockDepth=0;return;}
    for(unsigned i=0;i<3;i++)if(p==SceneData[i]+0x58){assert(LockDepth==(1U|(2U<<i)));LockDepth=1;return;}assert(false);}
static CMRayHit Ray(void *scene,const CMVec3 *camera,const CMVec3 *world){assert(LockDepth==1);CMVec3 d={world->x-camera->x,world->y-camera->y,world->z-camera->z};CMRayHit hit={0};hit.geometry_id=world->y>=0&&scene==SceneData[BlockScene]?7:UINT32_MAX;hit.far_distance=sqrtf(CMPDot(d,d))*(hit.geometry_id==7?.5f:1);if(BadRay)hit.far_distance=NAN;return hit;}
static void Intersect(void *handle,CMRayHit *ray,void *context){unsigned slot=(unsigned)(uintptr_t)handle-1;assert(slot<3&&!context&&LockDepth==(1U|(2U<<slot))&&!((uintptr_t)ray&15));RawCalls++;
    assert(ray->near_distance==0&&ray->time==0&&ray->flags==0&&ray->mask==UINT32_MAX&&ray->geometry_id==UINT32_MAX);
    float norm=ray->direction[0]*ray->direction[0]+ray->direction[1]*ray->direction[1]+ray->direction[2]*ray->direction[2];assert(fabsf(norm-1)<.00001f);
    if(slot==BlockScene&&ray->origin[1]+ray->direction[1]*ray->far_distance>=0){ray->geometry_id=7;ray->far_distance*=.5f;}
    if(BadRay)ray->far_distance=NAN;if(WrongRaw)ray->geometry_id=123;}
static void Original(void *p){assert(p==Context);OriginalCalls++;}
static void *List(void){return DrawList;}
static void Polygon(void *list,const CMVec2 *points,int count,uint32_t colour){assert(list==DrawList&&count==4&&!(DrawList[0x30]&4));assert(OriginalCalls&&points[1].x>points[0].x&&points[2].y==points[1].y+1);assert(colour==0xFFFFFFFF||colour==0xFF0000FF);Fills++;if(colour==0xFFFFFFFF)WhiteFills++;else RedFills++;}
static void Setup(void){
    memset(Context,0,sizeof(Context));memset(ComponentData,0,sizeof(ComponentData));memset(Frame,0,sizeof(Frame));memset(ActorRecord,0,sizeof(ActorRecord));memset(SourceRecord,0,sizeof(SourceRecord));memset(DrawList,0,sizeof(DrawList));
    TestTime=10000000000ULL;OriginalCalls=Fills=WhiteFills=RedFills=LockDepth=Reads=SceneLocks=RawCalls=BlockScene=0;BadRay=WrongProjection=FailQueryBudget=WrongRaw=UnreadableScene=false;
    memset(SceneData,0,sizeof(SceneData));for(uintptr_t i=0;i<3;i++){uintptr_t handle=i+1;memcpy(SceneData[i]+8,&handle,8);}
    CMLIntersect=Intersect;atomic_store(&CMLBatchDisabled,false);
    atomic_store(&CMLReady,true);atomic_store(&CMEnabled,false);atomic_flag_clear(&CMBusy);CMInside=false;
    CMLTargets=[NSMutableArray array];CMLCache=[NSMutableArray array];CMLSchemas=[NSMutableDictionary dictionary];CMLCachedBytes=0;
    CMBase=TestBase;CMReadRemote=Reader;CMProject=Project;CMLock=Lock;CMUnlock=Unlock;for(unsigned i=0;i<3;i++)CMRayFunctions[i]=Ray;
    CMLExpectedContext=(uintptr_t)Context;CMLOriginalDraw=Original;CMLGetDrawList=List;CMLPolygon=Polygon;
    CMTransform identity={{0,0,0,1,0,0,0,0,1,1,1}};CMRHeader h={0};h.vtable=TestBase+0x1000;h.class_address=GameClass;h.internal_index=7;h.name_index=100;memcpy(ComponentData,&h,40);
    memcpy(ComponentData+0x1F0,&identity,44);memcpy(ComponentData+0x828,&Asset,8);ComponentData[0x100]=1;
    uint32_t poseCount=1;memcpy(ComponentData+0x848,&GamePose,8);memcpy(ComponentData+0x850,&poseCount,4);memcpy(ComponentData+0x854,&poseCount,4);
    Context[0x23]=1;float screen[2]={8,8},fov=90;memcpy(Context+4,screen,8);memcpy(Context+0x1F4+48,&fov,4);
    uintptr_t begin=(uintptr_t)ActorRecord,end=begin+sizeof(ActorRecord);memcpy(Context+0x3F0,&begin,8);memcpy(Context+0x3F8,&end,8);uint32_t localTeam=1,team=2;float distance=10;memcpy(Context+0x3A8,&localTeam,4);memcpy(ActorRecord,&GameActor,8);memcpy(ActorRecord+8,&team,4);memcpy(ActorRecord+0x40,&distance,4);memcpy(SourceRecord,&GameActor,8);DrawList[0x30]=4;
    CMFSchema schema={0};schema.visible=(CMFBool){0x100,1,true};schema.hidden=(CMFBool){0x101,1,true};schema.predicted_found=true;schema.predicted_lod=0x900;CMLSchemas[@(GameClass)]=[NSData dataWithBytes:&schema length:sizeof(schema)];
    CMLMesh *mesh=[CMLMesh new];mesh.key=(CMGKey){.asset=Asset};mesh.vertices=3;mesh.bones=1;mesh.sectionCount=1;mesh.indexCount=3;mesh.indexWidth=2;
    uint8_t vertices[72]={0},weights[24]={0};const CMVec3 positions[3]={{10,-7.5f,7.5f},{10,7.5f,7.5f},{10,-7.5f,-7.5f}};for(unsigned i=0;i<3;i++){memcpy(vertices+i*24+8,&positions[i],12);weights[i*8+4]=255;}
    uint16_t indices[]={0,1,2},palettes[256]={0};CMSSection section={.triangle_count=1,.vertex_count=3,.bone_count=1};CMSMatrix inverse=CMSIdentity();uint32_t info[4]={100,0,UINT32_MAX,0};
    mesh.vertexData=[NSData dataWithBytes:vertices length:72];mesh.weightData=[NSData dataWithBytes:weights length:24];mesh.indexData=[NSData dataWithBytes:indices length:6];mesh.sectionData=[NSData dataWithBytes:&section length:sizeof(section)];mesh.paletteData=[NSData dataWithBytes:palettes length:512];mesh.inverseData=[NSData dataWithBytes:&inverse length:sizeof(inverse)];mesh.boneInfo=[NSData dataWithBytes:info length:16];
    CMLTarget *target=[CMLTarget new];target.component=GameComponent;target.actor=GameActor;target.asset=Asset;target.poseTime=target.metadataTime=TestTime;target.count=1;target.stride=48;target.transform=identity;
    uint8_t raw[48]={0};memcpy(raw,&identity,44);target.pose=[NSData dataWithBytes:raw length:48];target.meshes=@[mesh];
    NSMutableData *gpu=[NSMutableData dataWithLength:sizeof(CMGDescriptors)],*binding=[NSMutableData dataWithLength:sizeof(CMBBinding)],*refs=[NSMutableData dataWithLength:sizeof(CMRReferences)],*names=[NSMutableData dataWithLength:sizeof(CMNNames)];
    CMGDescriptors *g=gpu.mutableBytes;g->count=1;g->descriptors[0].asset_address=Asset;g->descriptors[0].asset_index=0;g->descriptors[0].lod_count=2;g->descriptors[0].name_index=100;
    CMBBinding *b=binding.mutableBytes;b->assets.count=1;b->assets.references[0].address=Asset;b->link_count=1;b->links[0]=(CMBLink){GameComponent,Asset,0};
    target.gpu=gpu;target.binding=binding;target.refs=refs;target.names=names;[CMLTargets addObject:target];
}
static void Draw(void){errno=ERANGE;CoreModelRenderDispatch(Context,TestBase+0x21BC4);assert(errno==ERANGE&&!LockDepth&&!CMInside&&DrawList[0x30]==4);}
int main(void){@autoreleasepool {
    Setup();Draw();assert(OriginalCalls==1&&Fills==9&&WhiteFills>0&&RedFills>0&&SceneLocks==3&&RawCalls==51);
    Setup();BlockScene=2;Draw();assert(Fills==9&&WhiteFills>0&&RedFills>0&&SceneLocks==3&&RawCalls==63);
    Setup();CMLIntersect=NULL;Draw();assert(Fills==9&&!SceneLocks&&!RawCalls);
    Setup();WrongRaw=true;Draw();assert(Fills==9&&WhiteFills>0&&RedFills>0&&SceneLocks==1&&atomic_load(&CMLBatchDisabled));
    Setup();UnreadableScene=true;Draw();assert(!Fills&&SceneLocks==1&&!RawCalls);
    Setup();uintptr_t noHandle=0;memcpy(SceneData[1]+8,&noHandle,8);Draw();assert(Fills==9&&SceneLocks==3&&RawCalls==36);
    Setup();CMLTarget *pinnedTarget=CMLTargets.firstObject;CMLMesh *pinned=pinnedTarget.meshes.firstObject,*old=[CMLMesh new],*recent=[CMLMesh new];
    pinned.byteCost=12*1024*1024;old.byteCost=8*1024*1024;recent.byteCost=4*1024*1024;[CMLCache addObjectsFromArray:@[old,pinned,recent]];CMLCachedBytes=CML_CACHE_BYTES;
    assert(CMLMakeCacheRoom(12*1024*1024)&&CMLCache.count==1&&CMLCache.firstObject==pinned&&CMLCachedBytes==pinned.byteCost);
    assert(!CMLMakeCacheRoom(13*1024*1024)&&pinnedTarget.meshes.count==1);
    CMLTrimStaleTargets(TestTime+2000000001ULL);assert(!pinnedTarget.meshes&&!pinnedTarget.binding&&CMLMakeCacheRoom(13*1024*1024)&&!CMLCache.count&&!CMLCachedBytes);
    for(unsigned i=0;i<96;i++){CMLMesh *entry=[CMLMesh new];entry.byteCost=1;[CMLCache addObject:entry];CMLCachedBytes++;}
    assert(CMLMakeCacheRoom(1)&&CMLCache.count==96); // The observed build-28 ceiling no longer blocks a new outfit.
    Setup();Context[0x23]=0;Draw();assert(OriginalCalls==1&&!Fills);
    Setup();ComponentData[0x101]=1;Draw();assert(!Fills);
    Setup();uint32_t team=1;memcpy(ActorRecord+8,&team,4);Draw();assert(!Fills);
    Setup();TestTime+=250000001ULL;Draw();assert(!Fills);
    Setup();WrongProjection=true;Draw();assert(!Fills);
    Setup();BadRay=true;Draw();assert(!Fills);
    Setup();FailQueryBudget=true;Draw();assert(!Fills);
    Setup();uint32_t lod=1;memcpy(ComponentData+0x900,&lod,4);Draw();assert(!Fills);
    Setup();uint64_t wrong=Asset+0x10000;memcpy(ComponentData+0x828,&wrong,8);Draw();assert(!Fills);
    Setup();CMLTarget *target=CMLTargets.firstObject;target.poseTime=0;
    uintptr_t f=(uintptr_t)(Frame+256),ret=TestBase+0xD8980,ctx=(uintptr_t)Context,record=(uintptr_t)SourceRecord;uint32_t count=1;
    memcpy((void *)(f+8),&ret,8);memcpy((void *)(f-8),&ctx,8);memcpy((void *)(f-0x48),&GameComponent,8);memcpy((void *)(f-0x20),&record,8);memcpy((void *)(f+16+0x110),&GamePose,8);memcpy((void *)(f+16+0x118),&count,4);
    CoreModelBzeroCapture((void *)(f+16+0xD0),44,f,GameComponent+0x1F0,TestBase+0xE1960);assert(target.poseTime==TestTime&&Reads==3&&target.pose.length==48&&!atomic_load(&CMEnabled));
    errno=ERANGE;unsigned calls=OriginalCalls;assert(CoreModelRenderDispatch(NULL,TestBase+0x1234)==(uintptr_t)getpid()&&errno==ERANGE&&OriginalCalls==calls);
    NSString *file=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];CMLWriteStatus([NSURL fileURLWithPath:file]);NSDictionary *status=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:file] options:0 error:NULL];assert([status[@"silhouette_renderer_connected"] boolValue]&&![status[@"full_requested_effect_verified"] boolValue]);[[NSFileManager defaultManager]removeItemAtPath:file error:NULL];
    puts("PASS: original frame forwarding; real mesh skinning, pixel rays and native red/white spans; exactly three separate scene locks; 16-byte native ray ABI; later-scene hits; raw/wrapper parity and scalar fallback; missing scene handles and unreadable scenes; flag restoration; hidden/team/LOD/stale/replaced targets; mismatch/malformed/budget fail closed; continuous pose after capture ends; getpid and errno; honest runtime status");
}return 0;}
