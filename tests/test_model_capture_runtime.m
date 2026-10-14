// Independent frame/memory fixtures exercise the production reader boundary.
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

static const uintptr_t TestBase=0x100000000ULL;
static unsigned char TestFrame[2048], TestContext[1024], TestPose[256*48];
static uint64_t TestClock=10000000000ULL, TestClockStep;
static unsigned TestStride=48, TestCount=63, ReadCalls, RayCalls, LockDepth;
static bool FailRemote, Ready, BadRay, FailView;

static int ModelTestClock(clockid_t clock, struct timespec *t) {
    (void)clock; TestClock+=TestClockStep;
    t->tv_sec=(time_t)(TestClock/1000000000ULL);t->tv_nsec=(long)(TestClock%1000000000ULL);return 0;
}

static kern_return_t ModelTestSelfRead(vm_map_t task, vm_address_t address, vm_size_t size,
                                      vm_address_t destination, vm_size_t *copied) {
    (void)task; uint64_t scalar=0;
    if (FailView && address==(uintptr_t)TestContext+0x1F4 && size==52) return KERN_INVALID_ADDRESS;
    if (address==TestBase+0xBD4720) scalar=0xA5A55A5AULL;
    else if (address==TestBase+0xBD4978) scalar=0xA5A55A5AULL^TestStride;
    else if (address==TestBase+0xC54459) scalar=Ready;
    else if (address==TestBase+0xC15EC8 && size==24) {
        uintptr_t scenes[3]={1,2,3};memcpy((void *)destination,scenes,24);*copied=24;return KERN_SUCCESS;
    } else if ((address>=(uintptr_t)TestFrame && size<=sizeof(TestFrame) && address-(uintptr_t)TestFrame<=sizeof(TestFrame)-size) ||
               (address>=(uintptr_t)TestContext && size<=sizeof(TestContext) && address-(uintptr_t)TestContext<=sizeof(TestContext)-size)) {
        memcpy((void *)destination,(void *)address,size);*copied=size;return KERN_SUCCESS;
    } else return KERN_INVALID_ADDRESS;
    if (size>8) return KERN_INVALID_ADDRESS;
    memcpy((void *)destination,&scalar,size);*copied=size;return KERN_SUCCESS;
}

#define CORE_MODEL_CAPTURE_TESTING 1
#define vm_read_overwrite ModelTestSelfRead
#define clock_gettime ModelTestClock
#include "../patch/CoreModelCapture.m"
#undef clock_gettime
#undef vm_read_overwrite

static bool ModelTestRead(uint64_t address, void *destination, size_t size) {
    ReadCalls++;
    // Emulate the nested Core bzero while the observer's remote read is active.
    CoreModelBzeroCapture(destination,size,0,address,TestBase+0xE1960);
    errno=EIO;
    if (FailRemote) return false;
    if (address==0x200001200ULL && size==44) {
        CMTransform t={{0,0,0,1,10,20,30,NAN,2,2,2}};memcpy(destination,&t,44);return true;
    }
    if (address==0x200000000ULL && size==TestCount*TestStride) {
        memcpy(destination,TestPose,size);return true;
    }
    if (address==0x200001000ULL && size==CM_COMPONENT_LAYOUT_BYTES) {
        memset(destination,0,size);
        const unsigned offsets[]={0x848,0x858,0x958,0xBD0,0xCD0};
        for (unsigned i=0;i<5;i++) {
            uint64_t pointer=i==3 ? 0x200008000ULL : 0x200000000ULL;
            uint32_t count=i==1 ? TestCount-1 : TestCount,capacity=i==4 ? 257 : TestCount;
            memcpy((uint8_t *)destination+offsets[i],&pointer,8);
            memcpy((uint8_t *)destination+offsets[i]+8,&count,4);
            memcpy((uint8_t *)destination+offsets[i]+12,&capacity,4);
        }
        return true;
    }
    return false;
}

static CMVec2 ModelTestProject(const CMVec3 *world,const void *camera,int width,int height) {
    assert(camera==TestContext+0x1F4 && width==2048 && height==945);
    return (CMVec2){world->x+100,world->y+100};
}
static void ModelTestLock(void *lock) { assert(lock==(void *)(TestBase+0xBD4F78) && !LockDepth);LockDepth++; }
static void ModelTestUnlock(void *lock) { assert(lock==(void *)(TestBase+0xBD4F78) && LockDepth==1);LockDepth--; }
static CMRayHit ModelTestRay(void *scene,const CMVec3 *from,const CMVec3 *to) {
    assert(LockDepth==1);RayCalls++;
    float dx=to->x-from->x,dy=to->y-from->y,dz=to->z-from->z;
    CMRayHit r={0};r.far_distance=sqrtf(dx*dx+dy*dy+dz*dz)*((uintptr_t)scene==2?0.5f:1);
    r.geometry_id=(uintptr_t)scene==2?7:UINT32_MAX;
    if (BadRay) r.far_distance=INFINITY;
    return r;
}

static uintptr_t ReaderFrame(void) { return (uintptr_t)(TestFrame+128); }
static void Drain(void) { dispatch_sync(CMReportQueue, ^{}); }
static void Reset(void) {
    Drain();memset(TestFrame,0,sizeof(TestFrame));memset(TestContext,0,sizeof(TestContext));memset(CMTargets,0,sizeof(CMTargets));
    memset(TestPose,0,sizeof(TestPose));ReadCalls=RayCalls=LockDepth=0;FailRemote=Ready=BadRay=FailView=false;TestClockStep=0;TestCount=63;TestStride=48;
    atomic_store(&CMEnabled,true);atomic_store(&CMSkipReason,0);atomic_store(&CMReaderCalls,0);atomic_store(&CMComponentCalls,0);
    atomic_store(&CMForwardCalls,0);atomic_store(&CMUnmatchedCalls,0);atomic_store(&CMDisabledCalls,0);atomic_store(&CMRecursiveCalls,0);
    uintptr_t frame=ReaderFrame(),ret=TestBase+0xD8980,context=(uintptr_t)TestContext,component=0x200001000ULL,bones=0x200000000ULL;
    memcpy((void *)(frame+8),&ret,8);memcpy((void *)(frame-8),&context,8);memcpy((void *)(frame-0x48),&component,8);
    memcpy((void *)(frame+16+0x110),&bones,8);memcpy((void *)(frame+16+0x118),&TestCount,4);
    float dimensions[2]={2048,945};CMVec3 camera={0,0,0};memcpy(TestContext+4,dimensions,8);memcpy(TestContext+0x1F4,&camera,sizeof(camera));
    for (unsigned i=0;i<TestCount;i++) { CMTransform b={{0,0,0,1,(float)i,2,3,NAN,1,1,1}};memcpy(TestPose+i*TestStride,&b,44); }
    dispatch_sync(CMReportQueue, ^{ [CMFrames removeAllObjects]; });
}
static void Capture(void) {
    uintptr_t f=ReaderFrame();int saved=errno;
    CoreModelBzeroCapture((void *)(f+16+0xD0),44,f,0x200001200ULL,TestBase+0xE1960);
    assert(errno==saved);Drain();
}

int main(void) {
    @autoreleasepool {
        CMBase=TestBase;CMReadRemote=ModelTestRead;CMProject=ModelTestProject;CMLock=ModelTestLock;CMUnlock=ModelTestUnlock;
        for (unsigned i=0;i<3;i++) CMRayFunctions[i]=ModelTestRay;
        CMReportQueue=dispatch_queue_create("model.capture.test.writer",DISPATCH_QUEUE_SERIAL);CMFrames=[NSMutableArray array];
        NSString *directory=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        assert([[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:NULL]);
        CMReportURL=[NSURL fileURLWithPath:[directory stringByAppendingPathComponent:@"report.json"]];

        Reset();unsigned char buffer[64];memset(buffer,0x7F,64);errno=ERANGE;
        CoreModelBzeroCapture(buffer,64,0,0,TestBase+0x1234);assert(errno==ERANGE && !ReadCalls);
        for (unsigned i=0;i<64;i++) assert(buffer[i]==0);
        assert(atomic_load(&CMForwardCalls)==1 && atomic_load(&CMUnmatchedCalls)==1 && !atomic_load(&CMReaderCalls));

        Reset();atomic_store(&CMEnabled,false);Capture();
        assert(!ReadCalls && !CMFrames.count && atomic_load(&CMReaderCalls)==1 && atomic_load(&CMDisabledCalls)==1);
        Reset();uintptr_t f=ReaderFrame();errno=ERANGE;
        CoreModelBzeroCapture((void *)(f+16+0xD0),44,f,0x200001200ULL,TestBase+0xE1994);Drain();
        assert(errno==ERANGE && !ReadCalls && !CMFrames.count && atomic_load(&CMUnmatchedCalls)==1);

        Reset();errno=ERANGE;
        Capture();assert(CMFrames.count==1 && ReadCalls==3 && RayCalls==0 && !CMInside);
        assert(atomic_load(&CMForwardCalls)==4 && atomic_load(&CMReaderCalls)==4);
        assert(atomic_load(&CMRecursiveCalls)==3 && atomic_load(&CMComponentCalls)==1);
        NSDictionary *frame=CMFrames.firstObject;assert([frame[@"captured_transform_count"] unsignedIntValue]==63);
        assert([frame[@"valid_transform_count"] unsignedIntValue]==63 && [frame[@"bones"] count]==63);
        NSArray *xyz=frame[@"bones"][5][@"calculated_world_position"];
        assert([xyz[0] floatValue]==20 && [xyz[1] floatValue]==24 && [xyz[2] floatValue]==36);
        assert(![frame[@"geometry_ready"] boolValue]);
        assert([frame[@"component_layout_sample_bytes"] unsignedIntValue]==CM_COMPONENT_LAYOUT_BYTES);
        assert([frame[@"captured_transform_array_address"] unsignedLongLongValue]==0x200000000ULL);
        assert([frame[@"component_transform_source_offset"] unsignedIntValue]==0x200);
        assert(([frame[@"matching_transform_array_offsets"] isEqualToArray:@[@0x848,@0x958]]));
        assert([frame[@"camera_view_source_sample_bytes"] unsignedIntValue]==52 && [frame[@"camera_view_position_matches_ray_origin"] boolValue]);
        Capture();assert(CMFrames.count==1 && ReadCalls==3); // spacing prevents duplicate work
        TestClock+=9999999999ULL;Capture();assert(CMFrames.count==1);
        TestClock+=1;Capture();assert(CMFrames.count==2);
        TestClock+=10000000000ULL;Capture();assert(CMFrames.count==2); // per-target bound

        Reset();FailView=true;Capture();frame=CMFrames.firstObject;
        assert([frame[@"camera_view_source_sample_bytes"] unsignedIntValue]==0 && [frame[@"valid_transform_count"] unsignedIntValue]==63);

        Reset();Ready=true;Capture();assert(CMFrames.count==1 && RayCalls==63*3 && !LockDepth);
        frame=CMFrames.firstObject;NSArray *rays=frame[@"bones"][0][@"scene_ray_samples"];
        assert(![rays[0][@"hit"] boolValue] && [rays[1][@"hit"] boolValue] && ![rays[2][@"hit"] boolValue]);

        Reset();Ready=BadRay=true;Capture();frame=CMFrames.firstObject;
        assert(frame[@"bones"][0][@"scene_ray_samples"][0][@"hit"]==NSNull.null);
        Reset();Ready=true;TestClockStep=20000000ULL;Capture();frame=CMFrames.firstObject;
        assert([frame[@"ray_queries_truncated"] boolValue] && !RayCalls && !LockDepth);

        Reset();FailRemote=true;Capture();assert(!CMFrames.count && ReadCalls==1 && atomic_load(&CMSkipReason)==3 && !CMInside);
        Reset();TestCount=257;memcpy((void *)(ReaderFrame()+16+0x118),&TestCount,4);Capture();assert(!CMFrames.count && !ReadCalls);
        Reset();TestStride=64;Capture();assert(!CMFrames.count && !ReadCalls);
        Reset();uintptr_t wrong=TestBase+0xD8984;memcpy((void *)(ReaderFrame()+8),&wrong,8);Capture();assert(!CMFrames.count && !ReadCalls);

        CMTransform component={{0,0,0,0.70710678f,10,20,30,NAN,2,2,2}},bone={{0,0,0,1,3,0,0,NAN,1,1,1}};
        // Correct the quaternion to a 90-degree rotation about Z.
        component.values[2]=0.70710678f;CMVec3 world;
        assert(CMWorldPosition(&component,&bone,&world));assert(fabsf(world.x-10)<0.00001f && fabsf(world.y-26)<0.00001f);
        bone.values[8]=NAN;assert(!CMWorldPosition(&component,&bone,&world));

        NSDictionary *report=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:CMReportURL] options:0 error:NULL];
        assert(report && ![report[@"full_render_mesh_verified"] boolValue] && ![report[@"pixel_depth_texture_available"] boolValue]);
        assert([report[@"capture_version"] unsignedIntValue]==6 && report[@"entry_bzero_calls_observed"]);
        [[NSFileManager defaultManager] removeItemAtPath:directory error:NULL];
        puts("PASS: exact bzero/errno forwarding; nested reads; full 63-transform snapshots; spacing and limits; pinned frame boundary; ray locks, miss/hit/malformed/budget; quaternion/scale world positions; honest report capabilities");
    }
    return 0;
}
