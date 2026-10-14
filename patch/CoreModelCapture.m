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
#include "CoreModelBinding.h"
#include "CoreModelNames.h"
#include "CoreModelMeshEvidence.h"

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
    NSDictionary *report = @{ @"capture_version":@6, @"status":status,
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

static NSDictionary *CMReferenceJSON(const CMRReference *ref) {
    return @{ @"source_component_offset":@(ref->source_offset),
        @"source_array_index":ref->array_index==UINT32_MAX ? (id)NSNull.null : @(ref->array_index),
        @"candidate_address":@(ref->address), @"header_candidate":CMHeaderJSON(&ref->header),
        @"class_header_candidate_valid":@(ref->class_header_valid),
        @"class_header_candidate":ref->class_header_valid ? CMHeaderJSON(&ref->class_header) : (id)NSNull.null,
        @"prefix_sample_bytes":@(ref->prefix_bytes),
        @"prefix_sample_base64":[[NSData dataWithBytes:ref->prefix length:ref->prefix_bytes] base64EncodedStringWithOptions:0],
        @"class_size_hint_valid":@(ref->class_size_hint_valid),
        @"class_size_hint":@(ref->class_size_hint), @"class_alignment_hint":@(ref->class_alignment_hint),
        @"semantic_class_verified":@NO, @"mesh_resource_identity_verified":@NO };
}

static void CMCollectNames(const CMRReferences *refs,const CMBBinding *binding,CMNNames *out) {
    uint64_t image=0,poolKey=0,poolValue=0,textKey=0,textValue=0;
    if (!CMSelfRead(CMBase+0xC54288,&image,8) ||
        !CMSelfRead(CMBase+0xBD4660,&poolKey,8) || !CMSelfRead(CMBase+0xBD48B8,&poolValue,8) ||
        !CMSelfRead(CMBase+0xBD4668,&textKey,8) || !CMSelfRead(CMBase+0xBD48C0,&textValue,8) ||
        !CMRemoteRange(image,8) || (poolKey^poolValue)>=(1ULL<<39)-image) return;
    CMNBegin(out,CMReadRemote,image+(poolKey^poolValue),textKey^textValue);
    if (!out->available) return;
    for (uint32_t group=0;group<2;group++) {
        const CMRReferences *source=group ? &binding->assets : refs;
        for (uint32_t i=0;i<source->count;i++) {
            const CMRReference *ref=&source->references[i];
            if (ref->class_header_valid) CMNAdd(out,CMReadRemote,ref->class_header.name_index);
            CMNAdd(out,CMReadRemote,ref->header.name_index);
        }
    }
    for (uint32_t i=0;i<binding->buffer_count;i++) {
        const CMBBuffer *buffer=&binding->buffers[i];
        if (!buffer->hierarchy_shape_valid) continue;
        for (uint32_t j=0;j<buffer->count;j++) {
            uint32_t index=0;memcpy(&index,buffer->data+j*buffer->stride_hint,4);
            CMNAdd(out,CMReadRemote,index);
        }
    }
}

static void CMCollectMeshNames(const CMEMeshEvidence *evidence,CMNNames *names) {
    for (uint32_t i=0;i<evidence->resource_count;i++) {
        const CMEResource *resource=&evidence->resources[i];
        if (!resource->hierarchy_valid) continue;
        for (uint32_t j=0;j<resource->count;j++) {
            uint32_t index=0;memcpy(&index,resource->info+j*16,4);CMNAdd(names,CMReadRemote,index);
        }
    }
    for (uint32_t i=0;i<evidence->native_count;i++) {
        const CMENative *node=&evidence->native[i];
        if (node->header_shape_valid) CMNAdd(names,CMReadRemote,node->header.name_index);
        if (node->class_header_shape_valid) CMNAdd(names,CMReadRemote,node->class_header.name_index);
    }
}

static NSDictionary *CMMeshEvidenceJSON(const CMEMeshEvidence *evidence) {
    if (!evidence) return @{};
    NSMutableArray *resources=[NSMutableArray array],*components=[NSMutableArray array],*native=[NSMutableArray array];
    for (uint32_t i=0;i<evidence->resource_count;i++) {
        const CMEResource *r=&evidence->resources[i];
        [resources addObject:@{ @"asset_candidate_index":@(r->asset_index), @"decoded_class_is_skeletal_mesh":@(r->skeletal_class_named),
            @"declared_bone_count":@(r->count), @"bone_info_bytes":@(r->info_bytes), @"reference_pose_bytes":@(r->pose_bytes),
            @"bone_record_stride":@16, @"reference_transform_stride":@48, @"hierarchy_shape_valid":@(r->hierarchy_valid),
            @"reference_pose_transform_shape_valid":@(r->reference_pose_valid), @"bone_identity_uses_name_index_and_number":@YES,
            @"bone_info_base64":[[NSData dataWithBytes:r->info length:r->info_bytes] base64EncodedStringWithOptions:0],
            @"reference_pose_base64":[[NSData dataWithBytes:r->pose length:r->pose_bytes] base64EncodedStringWithOptions:0] }];
    }
    for (uint32_t i=0;i<evidence->component_count;i++) {
        const CMEComponent *c=&evidence->components[i];NSMutableArray *maps=[NSMutableArray array],*poses=[NSMutableArray array];
        for (uint32_t j=0;j<2;j++) {
            const CMEMap *map=&c->maps[j];const CMEPose *pose=&c->poses[j];
            [maps addObject:@{ @"source_component_offset":@(map->offset), @"array_address":@(map->address), @"declared_count":@(map->count),
                @"sample_bytes":@(map->bytes), @"indices_in_primary_pose_range":@(map->index_range_valid),
                @"name_and_number_identity_matches":@(map->identity_matches), @"unmapped_count":@(map->unmapped),
                @"full_reference_bone_mapping_consistent":@(map->full_identity_match),
                @"sample_base64":[[NSData dataWithBytes:map->indices length:map->bytes] base64EncodedStringWithOptions:0] }];
            [poses addObject:@{ @"source_component_offset":@(pose->offset), @"array_address":@(pose->address), @"declared_count":@(pose->count),
                @"sample_bytes":@(pose->bytes), @"transform_stride_hypothesis":@48, @"transform_shape_valid":@(pose->transform_shape_valid),
                @"semantic_pose_space_verified":@NO,
                @"sample_base64":[[NSData dataWithBytes:pose->data length:pose->bytes] base64EncodedStringWithOptions:0] }];
        }
        [components addObject:@{ @"component_address":@(c->address), @"asset_candidate_index":@(c->asset_index),
            @"master_component_index_candidate":@(c->master_index), @"master_component_serial_candidate":@(c->master_serial),
            @"master_index_matches_primary_component":@(c->master_index_matches_primary),
            @"component_transform_valid":@(c->component_transform_valid), @"component_transform":CMTransformJSON(&c->component_transform),
            @"master_bone_map_candidates":maps, @"pose_array_candidates":poses }];
    }
    for (uint32_t i=0;i<evidence->native_count;i++) {
        const CMENative *node=&evidence->native[i];
        [native addObject:@{ @"candidate_address":@(node->address), @"source_offset":@(node->source_asset_offset),
            @"parent_native_node":node->parent_node==UINT32_MAX ? (id)NSNull.null : @(node->parent_node),
            @"source_array_index":node->array_slot==UINT32_MAX ? (id)NSNull.null : @(node->array_slot),
            @"prefix_sample_bytes":@(node->bytes), @"header_shape_valid":@(node->header_shape_valid),
            @"header_candidate":node->header_shape_valid ? (id)CMHeaderJSON(&node->header) : NSNull.null,
            @"class_header_candidate":node->class_header_shape_valid ? (id)CMHeaderJSON(&node->class_header) : NSNull.null,
            @"prefix_sample_base64":[[NSData dataWithBytes:node->data length:node->bytes] base64EncodedStringWithOptions:0],
            @"render_data_identity_verified":@NO }];
    }
    return @{ @"resource_reference_skeletons":resources, @"component_pose_bindings":components, @"native_render_source_candidates":native,
        @"read_attempts":@(evidence->read_attempts), @"read_failures":@(evidence->read_failures), @"read_limit":@(CME_READ_LIMIT),
        @"reference_sample_bytes":@(evidence->reference_bytes), @"reference_byte_limit":@(CME_REFERENCE_BYTE_LIMIT),
        @"pose_sample_bytes":@(evidence->pose_bytes), @"pose_byte_limit":@(CME_POSE_BYTE_LIMIT),
        @"rejected_arrays":@(evidence->rejected_arrays), @"truncated":@(evidence->truncated),
        @"resource_limit":@(CME_RESOURCE_LIMIT), @"native_node_limit":@(CME_NATIVE_LIMIT), @"arbitrary_pointer_recursion":@NO };
}

static void CMQueueSnapshot(NSData *transforms, NSData *worldData, NSData *projectionData, NSData *raysData, NSData *componentLayout, NSData *referenceData, NSData *bindingData, NSData *nameData, NSData *meshData, NSData *cameraViewData,
                            CMTransform componentTransform, CMVec3 camera, float width, float height,
                            uintptr_t component, uint64_t transformAddress, uint64_t componentTransformAddress, unsigned count, unsigned stride, uint64_t start,
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
                [references addObject:CMReferenceJSON(ref)];
            }
            NSMutableArray *assetCandidates=[NSMutableArray array],*links=[NSMutableArray array],*buffers=[NSMutableArray array],*names=[NSMutableArray array];
            const CMBBinding *binding=bindingData.length==sizeof(CMBBinding) ? bindingData.bytes : NULL;
            if (binding) {
                for (uint32_t i=0;i<binding->assets.count;i++) [assetCandidates addObject:CMReferenceJSON(&binding->assets.references[i])];
                for (uint32_t i=0;i<binding->link_count;i++) {
                    const CMBLink *link=&binding->links[i];
                    [links addObject:@{ @"component_address":@(link->component), @"resource_candidate_address":@(link->address),
                        @"source_offset":@0x828, @"asset_candidate_index":link->asset_index==UINT32_MAX ? (id)NSNull.null : @(link->asset_index) }];
                }
                for (uint32_t i=0;i<binding->buffer_count;i++) {
                    const CMBBuffer *buffer=&binding->buffers[i];
                    [buffers addObject:@{ @"source_asset_offset":@(buffer->offset), @"array_address":@(buffer->address),
                        @"declared_count":@(buffer->count), @"capacity":@(buffer->capacity), @"sample_bytes":@(buffer->bytes),
                        @"record_stride_hypothesis":@(buffer->stride_hint), @"hierarchy_shape_valid":@(buffer->hierarchy_shape_valid),
                        @"semantic_type_verified":@NO,
                        @"sample_base64":[[NSData dataWithBytes:buffer->data length:buffer->bytes] base64EncodedStringWithOptions:0] }];
                }
            }
            const CMNNames *catalog=nameData.length==sizeof(CMNNames) ? nameData.bytes : NULL;
            if (catalog) for (uint32_t i=0;i<catalog->count;i++) {
                const CMNName *name=&catalog->names[i];
                [names addObject:@{ @"name_index":@(name->index), @"entry_address":@(name->entry_address), @"valid_ascii_text":@(name->valid),
                    @"text":name->valid ? (id)[NSString stringWithUTF8String:name->text] : NSNull.null }];
            }
            unsigned valid = 0, queries = 0;
            NSMutableArray *arrayOffsets=[NSMutableArray array];
            const uint8_t *layoutBytes=componentLayout.bytes;
            for (size_t offset=0;offset+16<=componentLayout.length;offset+=8) {
                uint64_t address=0;uint32_t n=0,capacity=0;
                memcpy(&address,layoutBytes+offset,8);memcpy(&n,layoutBytes+offset+8,4);memcpy(&capacity,layoutBytes+offset+12,4);
                if (address==transformAddress && n==count && capacity>=count && capacity<=CM_BONE_LIMIT)
                    [arrayOffsets addObject:@(offset)];
            }
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
                @"captured_transform_array_address":@(transformAddress),
                @"component_transform_source_offset":@(componentTransformAddress-component),
                @"matching_transform_array_offsets":arrayOffsets,
                @"captured_all_declared_component_transforms":@YES, @"valid_transform_count":@(valid),
                @"transform_stride":@(stride), @"component_transform":CMTransformJSON(&componentTransform),
                @"camera_world_position":CMFloats((const float *)&camera,3), @"screen_size":@[@(width),@(height)],
                @"camera_view_source_sample_bytes":@(cameraViewData.length),
                @"camera_view_source_sample_base64":[cameraViewData base64EncodedStringWithOptions:0] ?: (id)NSNull.null,
                @"camera_view_position_matches_ray_origin":@(cameraViewData.length==52 && !memcmp(cameraViewData.bytes,&camera,sizeof(camera))),
                @"camera_view_layout_semantics_verified":@NO,
                @"engine_frame_id_available":@NO, @"atomic_pose_snapshot_verified":@NO,
                @"component_layout_sample_bytes":@(componentLayout.length),
                @"component_layout_sample_base64":[componentLayout base64EncodedStringWithOptions:0] ?: (id)NSNull.null,
                @"component_layout_type_interpretation_verified":@NO,
                @"component_reference_candidates":references,
                @"mesh_runtime_evidence":CMMeshEvidenceJSON(meshData.length==sizeof(CMEMeshEvidence) ? meshData.bytes : NULL),
                @"resource_binding_evidence":@{ @"asset_candidates":assetCandidates, @"component_resource_links":links,
                    @"root_asset_array_candidates":buffers, @"extra_asset_read_attempts":@(binding ? binding->assets.read_attempts : 0),
                    @"extra_asset_read_failures":@(binding ? binding->assets.read_failures : 0), @"extra_asset_read_limit":@(CMR_READ_LIMIT),
                    @"buffer_read_attempts":@(binding ? binding->read_attempts : 0), @"buffer_read_failures":@(binding ? binding->read_failures : 0),
                    @"buffer_read_limit":@(CMB_BUFFER_READ_LIMIT), @"rejected_arrays":@(binding ? binding->rejected_arrays : 0),
                    @"truncated":@(binding ? binding->truncated || binding->assets.truncated : NO), @"recursive_scan":@NO },
                @"name_lookup":@{ @"source":@"pinned-Core-D6794-live-profile-chunked-name-table", @"names":names,
                    @"available":@(catalog ? catalog->available : NO), @"read_attempts":@(catalog ? catalog->read_attempts : 0),
                    @"read_failures":@(catalog ? catalog->read_failures : 0), @"rejected_text":@(catalog ? catalog->rejected_text : 0),
                    @"truncated":@(catalog ? catalog->truncated : NO), @"name_limit":@(CMN_NAME_LIMIT), @"read_limit":@(CMN_READ_LIMIT) },
                @"reference_probe":@{ @"read_attempts":@(evidence ? evidence->read_attempts : 0),
                    @"read_failures":@(evidence ? evidence->read_failures : 0),
                    @"rejected_headers":@(evidence ? evidence->rejected_headers : 0),
                    @"rejected_arrays":@(evidence ? evidence->rejected_arrays : 0),
                    @"truncated":@(evidence ? evidence->truncated : NO),
                    @"reference_limit":@(CMR_REFERENCE_LIMIT), @"read_limit":@(CMR_READ_LIMIT),
                    @"object_prefix_bytes_max":@(CMR_PREFIX_BYTES), @"recursive_reference_scan":@NO },
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
        NSMutableData *bindingData = nil;
        NSMutableData *nameData = nil;
        NSMutableData *meshData = nil;
        NSData *cameraViewData = nil;
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
            !context || context>UINTPTR_MAX-0x228 || !CMSelfRead(context+0x1F4,&camera,sizeof(camera)) ||
            !CMSelfRead(context+4,size,sizeof(size)) || !isfinite(camera.x) || !isfinite(camera.y) || !isfinite(camera.z) ||
            !isfinite(size[0]) || !isfinite(size[1]) || size[0]<1 || size[1]<1 || size[0]>32768 || size[1]>32768) {
            atomic_store_explicit(&CMSkipReason,3,memory_order_relaxed); goto done;
        }
        // DA704's uncached projection path copies this exact 52-byte source.
        // Keep raw evidence; this does not assert an atomic engine camera frame.
        uint8_t cameraView[52];
        if (CMSelfRead(context+0x1F4,cameraView,sizeof(cameraView))) cameraViewData=[NSData dataWithBytes:cameraView length:sizeof(cameraView)];
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
        bindingData=[NSMutableData dataWithLength:sizeof(CMBBinding)];
        nameData=[NSMutableData dataWithLength:sizeof(CMNNames)];
        CMBCollect(component,referenceData.bytes,CMReadRemote,bindingData.mutableBytes);
        CMCollectNames(referenceData.bytes,bindingData.bytes,nameData.mutableBytes);
        meshData=[NSMutableData dataWithLength:sizeof(CMEMeshEvidence)];
        CMECollect(component,componentLayout.bytes,componentLayout.length,count,referenceData.bytes,bindingData.bytes,nameData.bytes,CMReadRemote,meshData.mutableBytes);
        CMCollectMeshNames(meshData.bytes,nameData.mutableBytes);
        CMQueueSnapshot([NSData dataWithBytes:raw length:count*stride],
            [NSData dataWithBytes:worlds length:count*sizeof(*worlds)],
            [NSData dataWithBytes:projections length:count*sizeof(*projections)],
            [NSData dataWithBytes:rays length:count*3*sizeof(*rays)],componentLayout,referenceData,bindingData,nameData,meshData,cameraViewData,componentTransform,camera,size[0],size[1],
            component,boneAddress,remoteAddress,count,stride,start,CMNow(),geometryReady,truncated);
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
