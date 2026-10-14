// Optional, bounded acquisition of the transform array already located by Core.
// Only the opt-in capture build redirects Core's bzero import to this observer.
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
#include <stddef.h>
#include <strings.h>
#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif
#include "CoreModelCaptureMath.h"
#include "CoreModelReferences.h"

#define CM_BONE_LIMIT 256
#define CM_TARGET_LIMIT 3
#define CM_SAMPLES_PER_TARGET 2
#define CM_RAY_BUDGET_NS 10000000ULL
#define CM_COMPONENT_LAYOUT_BYTES 4096
#define CM_SAMPLE_INTERVAL_NS 10000000000ULL

typedef bool (*CMReadFunction)(uint64_t, void *, size_t);
typedef CMRayHit (*CMRayFunction)(void *, const CMVec3 *, const CMVec3 *);
typedef CMVec2 (*CMProjectFunction)(const CMVec3 *, const void *, int, int);
typedef void (*CMLockFunction)(void *);
typedef struct {
    uintptr_t component;
    unsigned samples;
    uint64_t last_time;
} CMTarget;
typedef struct {
    uint8_t queried, hit, malformed;
    float far_distance;
    uint32_t geometry, primitive;
} CMRaySample;

static uintptr_t CMBase;
static CMReadFunction CMReadRemote;
static CMRayFunction CMRayFunctions[3];
static CMProjectFunction CMProject;
static CMLockFunction CMLock, CMUnlock;
static atomic_bool CMEnabled;
static atomic_flag CMBusy = ATOMIC_FLAG_INIT;
static _Thread_local bool CMInside;
static CMTarget CMTargets[CM_TARGET_LIMIT];
static dispatch_queue_t CMReportQueue;
static NSMutableArray *CMFrames;
static NSURL *CMReportURL;
static dispatch_source_t CMStatusTimer;
static atomic_uint CMReaderCalls, CMComponentCalls, CMSkipReason;
static atomic_uint CMForwardCalls, CMUnmatchedCalls, CMDisabledCalls, CMRecursiveCalls;

static uint64_t CMNow(void) {
    struct timespec t = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;
}

static uintptr_t CMStripReturn(void *address) {
#if __has_feature(ptrauth_calls)
    address = ptrauth_strip(address, ptrauth_key_return_address);
#endif
    return (uintptr_t)address;
}

static void *CMCallable(uintptr_t address) {
#if __has_feature(ptrauth_calls)
    return ptrauth_sign_unauthenticated((void *)address, ptrauth_key_function_pointer, 0);
#else
    return (void *)address;
#endif
}

static bool CMSelfRead(uintptr_t address, void *out, size_t size) {
    vm_size_t copied = 0;
    return address && size && size <= 16384 &&
        vm_read_overwrite(mach_task_self(), address, size, (vm_address_t)out, &copied) == KERN_SUCCESS && copied == size;
}

static bool CMRemoteRange(uint64_t address, size_t size) {
    return address >= 0x100000000ULL && size && size <= 16384 &&
           address < (1ULL<<39) && size <= (1ULL<<39)-address;
}

static bool CMImageMatches(void) {
    const struct mach_header_64 *h = (const void *)_dyld_get_image_header(0);
    const uint8_t uuid[16] = {0x4e,0xd1,0xfd,0x29,0xa4,0x6a,0x36,0xb3,0x8d,0xfc,0x36,0xf6,0x7e,0xa9,0x29,0xd1};
    if (!h || h->magic != MH_MAGIC_64 || h->ncmds>512 || h->sizeofcmds>65536) return false;
    const uint8_t *cursor = (const void *)(h+1); size_t left = h->sizeofcmds;
    for (uint32_t i=0; i<h->ncmds; i++) {
        if (left<sizeof(struct load_command)) return false;
        const struct load_command *c = (const void *)cursor;
        if (c->cmdsize<sizeof(*c) || c->cmdsize>left) return false;
        if (c->cmd==LC_UUID && c->cmdsize==sizeof(struct uuid_command))
            return memcmp(((const struct uuid_command *)c)->uuid,uuid,16)==0;
        cursor += c->cmdsize; left -= c->cmdsize;
    }
    return false;
}

static NSArray *CMFloats(const float *values, unsigned count) {
    NSMutableArray *a = [NSMutableArray arrayWithCapacity:count];
    for (unsigned i=0; i<count; i++) [a addObject:isfinite(values[i]) ? @(values[i]) : NSNull.null];
    return a;
}

static NSDictionary *CMTransformJSON(const CMTransform *t) {
    return @{ @"rotation_xyzw":CMFloats(t->values,4),
              @"translation":CMFloats(t->values+4,3), @"scale":CMFloats(t->values+8,3) };
}

// Called only on the serial writer queue; report contains at most six snapshots.
static void CMWriteReport(NSString *status) {
    NSDictionary *report = @{ @"capture_version":@3, @"status":status,
        @"date":[NSDate.date description], @"core_uuid":@"4ED1FD29-A46A-36B3-8DFC-36F67EA929D1",
        @"snapshots":CMFrames ?: @[], @"max_targets":@(CM_TARGET_LIMIT),
        @"max_snapshots":@(CM_TARGET_LIMIT*CM_SAMPLES_PER_TARGET),
        @"full_render_mesh_verified":@NO, @"runtime_mesh_resource_binding_verified":@NO,
        @"pixel_depth_texture_available":@NO, @"silhouette_implemented":@NO,
        @"target_files_modified":@NO, @"game_process_started":@NO, @"network_requests":@0,
        @"reader_bzero_calls_observed":@(atomic_load_explicit(&CMReaderCalls,memory_order_relaxed)),
        @"entry_bzero_calls_observed":@(atomic_load_explicit(&CMForwardCalls,memory_order_relaxed)),
        @"unmatched_caller_calls_observed":@(atomic_load_explicit(&CMUnmatchedCalls,memory_order_relaxed)),
        @"disabled_reader_calls_observed":@(atomic_load_explicit(&CMDisabledCalls,memory_order_relaxed)),
        @"recursive_reader_calls_observed":@(atomic_load_explicit(&CMRecursiveCalls,memory_order_relaxed)),
        @"component_read_calls_observed":@(atomic_load_explicit(&CMComponentCalls,memory_order_relaxed)),
        @"last_skip_code":@(atomic_load_explicit(&CMSkipReason,memory_order_relaxed)),
        @"skip_codes":@{@"1":@"frame-layout-or-local-read", @"2":@"transform-count-or-stride",
                       @"3":@"remote-read-or-camera-validation", @"4":@"sampling-limit-or-spacing"} };
    NSData *data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:NULL];
    if (data.length && data.length<=2*1024*1024 && CMReportURL)
        [data writeToURL:CMReportURL options:NSDataWritingAtomic error:NULL];
}

static NSDictionary *CMHeaderJSON(const CMRHeader *h) {
    return @{ @"vtable_address":@(h->vtable), @"flags":@(h->flags), @"internal_index":@(h->internal_index),
        @"class_address":@(h->class_address), @"name_index":@(h->name_index), @"name_number":@(h->name_number),
        @"outer_address":@(h->outer_address) };
}

static void CMQueueSnapshot(NSData *transforms, NSData *worldData, NSData *projectionData, NSData *raysData, NSData *componentLayout, NSData *referenceData,
                            CMTransform componentTransform, CMVec3 camera, float width, float height,
                            uintptr_t component, unsigned count, unsigned stride, uint64_t start,
                            uint64_t end, bool geometryReady, bool rayTruncated) {
    dispatch_async(CMReportQueue, ^{
        @autoreleasepool {
            if (CMFrames.count>=CM_TARGET_LIMIT*CM_SAMPLES_PER_TARGET) return;
            const uint8_t *raw = transforms.bytes;
            const CMVec3 *worlds = worldData.bytes;
            const CMVec2 *screens = projectionData.bytes;
            const CMRaySample *rays = raysData.bytes;
            NSMutableArray *bones = [NSMutableArray arrayWithCapacity:count];
            NSMutableArray *references = [NSMutableArray array];
            const CMRReferences *evidence=referenceData.length==sizeof(CMRReferences) ? referenceData.bytes : NULL;
            if (evidence) for (uint32_t i=0;i<evidence->count;i++) {
                const CMRReference *ref=&evidence->references[i];
                [references addObject:@{ @"source_component_offset":@(ref->source_offset),
                    @"source_array_index":ref->array_index==UINT32_MAX ? (id)NSNull.null : @(ref->array_index),
                    @"candidate_address":@(ref->address), @"header_candidate":CMHeaderJSON(&ref->header),
                    @"class_header_candidate_valid":@(ref->class_header_valid),
                    @"class_header_candidate":ref->class_header_valid ? CMHeaderJSON(&ref->class_header) : (id)NSNull.null,
                    @"prefix_sample_bytes":@(CMR_PREFIX_BYTES),
                    @"prefix_sample_base64":[[NSData dataWithBytes:ref->prefix length:CMR_PREFIX_BYTES] base64EncodedStringWithOptions:0],
                    @"semantic_class_verified":@NO, @"mesh_resource_identity_verified":@NO }];
            }
            unsigned valid = 0, queries = 0;
            for (unsigned i=0; i<count; i++) {
                CMTransform bone; memcpy(&bone,raw+i*stride,sizeof(bone));
                bool boneValid = CMValidTransform(&bone) && isfinite(worlds[i].x);
                if (boneValid) valid++;
                NSMutableArray *hits = [NSMutableArray arrayWithCapacity:3];
                for (unsigned j=0; j<3; j++) {
                    const CMRaySample *r = &rays[i*3+j];
                    if (r->queried) queries++;
                    [hits addObject:@{ @"scene_slot":@(j), @"queried":@(r->queried!=0),
                        @"hit":r->queried && !r->malformed ? @(r->hit!=0) : (id)NSNull.null,
                        @"far_distance_native_units":r->queried && !r->malformed ? @(r->far_distance) : (id)NSNull.null,
                        @"malformed_result":@(r->malformed!=0) }];
                }
                [bones addObject:@{ @"index":@(i), @"component_space_transform":CMTransformJSON(&bone),
                    @"transform_valid":@(boneValid), @"calculated_world_position":CMFloats((const float *)&worlds[i],3),
                    @"core_projected_xy":CMFloats((const float *)&screens[i],2), @"scene_ray_samples":hits }];
            }
            NSDictionary *frame = @{ @"sample_start_monotonic_ns":@(start), @"sample_end_monotonic_ns":@(end),
                @"component_id":[NSString stringWithFormat:@"0x%llx",(unsigned long long)component],
                @"declared_transform_count":@(count), @"captured_transform_count":@(count),
                @"captured_all_declared_component_transforms":@YES, @"valid_transform_count":@(valid),
                @"transform_stride":@(stride), @"component_transform":CMTransformJSON(&componentTransform),
                @"camera_world_position":CMFloats((const float *)&camera,3), @"screen_size":@[@(width),@(height)],
                @"engine_frame_id_available":@NO, @"atomic_pose_snapshot_verified":@NO,
                @"component_layout_sample_bytes":@(componentLayout.length),
                @"component_layout_sample_base64":[componentLayout base64EncodedStringWithOptions:0] ?: (id)NSNull.null,
                @"component_layout_type_interpretation_verified":@NO,
                @"component_reference_candidates":references,
                @"reference_probe":@{ @"read_attempts":@(evidence ? evidence->read_attempts : 0),
                    @"read_failures":@(evidence ? evidence->read_failures : 0),
                    @"rejected_headers":@(evidence ? evidence->rejected_headers : 0),
                    @"rejected_arrays":@(evidence ? evidence->rejected_arrays : 0),
                    @"truncated":@(evidence ? evidence->truncated : NO),
                    @"reference_limit":@(CMR_REFERENCE_LIMIT), @"read_limit":@(CMR_READ_LIMIT),
                    @"object_prefix_bytes":@(CMR_PREFIX_BYTES), @"recursive_reference_scan":@NO },
                @"geometry_ready":@(geometryReady), @"occlusion_source":@"Core-local-geometry-ray-intersection",
                @"queried_ray_count":@(queries), @"ray_queries_truncated":@(rayTruncated), @"bones":bones };
            [CMFrames addObject:frame];
            CMWriteReport(CMFrames.count==CM_TARGET_LIMIT*CM_SAMPLES_PER_TARGET ? @"capture-limit-reached" : @"snapshots-captured");
        }
    });
}

// Keep the large scratch arrays out of the forwarding function. Its prologue
// must not probe a large stack frame for every unrelated Core bzero call.
__attribute__((noinline)) static void CMTryCapture(void *destination, size_t length, uintptr_t readerFrame, uint64_t remoteAddress, uintptr_t caller) {
    if (!atomic_load_explicit(&CMEnabled,memory_order_acquire) || CMInside || caller!=CMBase+0xE1960) return;
    if (length!=44 ||
        !readerFrame || readerFrame>UINTPTR_MAX-0x200 || readerFrame<0x50 ||
        (uintptr_t)destination!=readerFrame+16+0xD0 || !CMRemoteRange(remoteAddress,44)) return;
    uintptr_t parentReturn=0, context=0, component=0, boneAddress=0;
    uint32_t count=0; uint64_t strideKey=0, strideValue=0;
    uintptr_t callerSP=readerFrame+16;
    if (!CMSelfRead(readerFrame+8,&parentReturn,8) || CMStripReturn((void *)parentReturn)!=CMBase+0xD8980 ||
        !CMSelfRead(readerFrame-8,&context,8) || !CMSelfRead(readerFrame-0x48,&component,8) ||
        !CMSelfRead(callerSP+0x110,&boneAddress,8) || !CMSelfRead(callerSP+0x118,&count,4) ||
        !CMSelfRead(CMBase+0xBD4720,&strideKey,8) || !CMSelfRead(CMBase+0xBD4978,&strideValue,8)) {
        atomic_store_explicit(&CMSkipReason,1,memory_order_relaxed); return;
    }
    atomic_fetch_add_explicit(&CMComponentCalls,1,memory_order_relaxed);
    unsigned stride=(unsigned)(strideKey^strideValue);
    if ((strideKey^strideValue)!=44 && (strideKey^strideValue)!=48) {
        atomic_store_explicit(&CMSkipReason,2,memory_order_relaxed); return;
    }
    if (!count || count>CM_BONE_LIMIT || !CMRemoteRange(boneAddress,count*stride) ||
        !CMRemoteRange(component,44) || remoteAddress<component || remoteAddress-component>16384) {
        atomic_store_explicit(&CMSkipReason,2,memory_order_relaxed); return;
    }
    if (atomic_flag_test_and_set_explicit(&CMBusy,memory_order_acquire)) return;
    CMInside=true;
    @autoreleasepool {
        NSData *componentLayout = nil;
        NSMutableData *referenceData = nil;
        uint64_t start=CMNow(); CMTarget *target=NULL;
        for (unsigned i=0; i<CM_TARGET_LIMIT; i++) if (CMTargets[i].component==component) target=&CMTargets[i];
        if (!target) for (unsigned i=0; i<CM_TARGET_LIMIT; i++) if (!CMTargets[i].component) { target=&CMTargets[i]; break; }
        if (!target || target->samples>=CM_SAMPLES_PER_TARGET ||
            (target->samples && start-target->last_time<CM_SAMPLE_INTERVAL_NS)) {
            atomic_store_explicit(&CMSkipReason,4,memory_order_relaxed); goto done;
        }
        CMTransform componentTransform; uint8_t raw[CM_BONE_LIMIT*48];
        CMVec3 camera; float size[2];
        if (!CMReadRemote(remoteAddress,&componentTransform,sizeof(componentTransform)) ||
            !CMValidTransform(&componentTransform) || !CMReadRemote(boneAddress,raw,count*stride) ||
            !context || context>UINTPTR_MAX-0x220 || !CMSelfRead(context+0x1F4,&camera,sizeof(camera)) ||
            !CMSelfRead(context+4,size,sizeof(size)) || !isfinite(camera.x) || !isfinite(camera.y) || !isfinite(camera.z) ||
            !isfinite(size[0]) || !isfinite(size[1]) || size[0]<1 || size[1]<1 || size[0]>32768 || size[1]>32768) {
            atomic_store_explicit(&CMSkipReason,3,memory_order_relaxed); goto done;
        }
        CMVec3 worlds[CM_BONE_LIMIT]; CMVec2 projections[CM_BONE_LIMIT];
        CMRaySample rays[CM_BONE_LIMIT*3]; memset(rays,0,sizeof(rays));
        for (unsigned i=0; i<count; i++) {
            CMTransform bone; memcpy(&bone,raw+i*stride,sizeof(bone));
            worlds[i]=(CMVec3){NAN,NAN,NAN}; projections[i]=(CMVec2){NAN,NAN};
            if (CMWorldPosition(&componentTransform,&bone,&worlds[i]))
                projections[i]=CMProject(&worlds[i],(const void *)(context+0x1F4),(int)size[0],(int)size[1]);
            else worlds[i]=(CMVec3){NAN,NAN,NAN};
        }
        uint8_t ready=0; uintptr_t scenes[3]={0}; bool geometryReady=false, truncated=false;
        if (CMSelfRead(CMBase+0xC54459,&ready,1) && ready==1 && CMLock && CMUnlock) {
            CMLock((void *)(CMBase+0xBD4F78));
            if (CMSelfRead(CMBase+0xC15EC8,scenes,sizeof(scenes)) && scenes[0] && scenes[1] && scenes[2]) {
                geometryReady=true; uint64_t deadline=CMNow()+CM_RAY_BUDGET_NS;
                for (unsigned i=0; i<count; i++) {
                    if (!isfinite(worlds[i].x)) continue;
                    CMVec3 delta={worlds[i].x-camera.x,worlds[i].y-camera.y,worlds[i].z-camera.z};
                    float distance=sqrtf(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
                    if (!isfinite(distance) || distance<0.001f) continue;
                    for (unsigned j=0; j<3; j++) {
                        if (CMNow()>=deadline) { truncated=true; break; }
                        CMRayHit hit=CMRayFunctions[j]((void *)scenes[j],&camera,&worlds[i]);
                        CMRaySample *r=&rays[i*3+j]; r->queried=1;
                        r->malformed=!isfinite(hit.far_distance) || hit.far_distance<0 || hit.far_distance>distance+1;
                        r->hit=hit.geometry_id!=UINT32_MAX; r->far_distance=hit.far_distance;
                        r->geometry=hit.geometry_id; r->primitive=r->hit ? hit.primitive_id : UINT32_MAX;
                    }
                    if (truncated) break;
                }
            }
            CMUnlock((void *)(CMBase+0xBD4F78));
        }
        target->component=component; target->samples++; target->last_time=start;
        atomic_store_explicit(&CMSkipReason,0,memory_order_relaxed);
        // Evidence for identifying the located component's resource/attachment
        // fields: bounded candidate prefixes and class headers, no recursive scan.
        uint8_t layout[CM_COMPONENT_LAYOUT_BYTES];
        componentLayout = CMRemoteRange(component,sizeof(layout)) && CMReadRemote(component,layout,sizeof(layout)) ?
            [NSData dataWithBytes:layout length:sizeof(layout)] : nil;
        referenceData=[NSMutableData dataWithLength:sizeof(CMRReferences)];
        if (componentLayout) CMRCollect(componentLayout.bytes,componentLayout.length,CMReadRemote,referenceData.mutableBytes);
        CMQueueSnapshot([NSData dataWithBytes:raw length:count*stride],
            [NSData dataWithBytes:worlds length:count*sizeof(*worlds)],
            [NSData dataWithBytes:projections length:count*sizeof(*projections)],
            [NSData dataWithBytes:rays length:count*3*sizeof(*rays)],componentLayout,referenceData,componentTransform,camera,size[0],size[1],
            component,count,stride,start,CMNow(),geometryReady,truncated);
    done: ;
    }
    CMInside=false;
    atomic_flag_clear_explicit(&CMBusy,memory_order_release);
}

// Capture LR before a C prologue can call the Darwin stack-probing helper.
// A tail branch leaves the original return address and stack intact.
#if defined(__arm64__) && !defined(CORE_MODEL_CAPTURE_TESTING)
__attribute__((naked)) void cmbzr(void *destination, size_t length) {
    __asm__ volatile("mov x2, x29\nmov x3, x23\nmov x4, x30\nb _CoreModelBzeroCapture\n");
}
#endif

__attribute__((noinline)) void CoreModelBzeroCapture(void *destination, size_t length, uintptr_t readerFrame, uint64_t remoteAddress, uintptr_t entryReturn) {
    bzero(destination,length);
    int saved=errno;
    uintptr_t caller=CMStripReturn((void *)entryReturn);
    atomic_fetch_add_explicit(&CMForwardCalls,1,memory_order_relaxed);
    if (caller!=CMBase+0xE1960) {
        atomic_fetch_add_explicit(&CMUnmatchedCalls,1,memory_order_relaxed);
    } else {
        atomic_fetch_add_explicit(&CMReaderCalls,1,memory_order_relaxed);
        if (!atomic_load_explicit(&CMEnabled,memory_order_acquire))
            atomic_fetch_add_explicit(&CMDisabledCalls,1,memory_order_relaxed);
        else if (CMInside)
            atomic_fetch_add_explicit(&CMRecursiveCalls,1,memory_order_relaxed);
        else CMTryCapture(destination,length,readerFrame,remoteAddress,caller);
    }
    errno=saved;
}

void CoreModelCaptureStartup(void) {
    if (!CMImageMatches()) return;
    CMBase=(uintptr_t)_dyld_get_image_header(0);
    CMReadRemote=(CMReadFunction)CMCallable(CMBase+0xE1908);
    CMProject=(CMProjectFunction)CMCallable(CMBase+0xDA704);
    CMRayFunctions[0]=(CMRayFunction)CMCallable(CMBase+0xE0BE8);
    CMRayFunctions[1]=(CMRayFunction)CMCallable(CMBase+0xE0CF0);
    CMRayFunctions[2]=CMRayFunctions[0];
    CMLock=(CMLockFunction)CMCallable(CMBase+0x725000);
    CMUnlock=(CMLockFunction)CMCallable(CMBase+0x725010);
    CMReportQueue=dispatch_queue_create("local.core.model-capture.writer",DISPATCH_QUEUE_SERIAL);
    NSURL *documents=[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    CMReportURL=[documents URLByAppendingPathComponent:@"Core-model-runtime-report.json"];
    dispatch_async(CMReportQueue, ^{
        @autoreleasepool {
            CMFrames=[NSMutableArray array]; CMWriteReport(@"waiting-for-eligible-component-read");
            CMStatusTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,CMReportQueue);
            if (CMStatusTimer) {
                __block unsigned ticks=0;
                dispatch_source_set_timer(CMStatusTimer,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),5*NSEC_PER_SEC,NSEC_PER_SEC);
                dispatch_source_set_event_handler(CMStatusTimer, ^{
                    if (++ticks>=36 || CMFrames.count>=CM_TARGET_LIMIT*CM_SAMPLES_PER_TARGET) {
                        atomic_store_explicit(&CMEnabled,false,memory_order_release);
                        CMWriteReport(ticks>=36 ? @"capture-window-ended" : @"capture-limit-reached");
                        dispatch_source_cancel(CMStatusTimer);
                    } else CMWriteReport(CMFrames.count ? @"snapshots-captured" : @"waiting-for-eligible-component-read");
                });
                dispatch_resume(CMStatusTimer);
            }
            atomic_store_explicit(&CMEnabled,true,memory_order_release);
        }
    });
}
