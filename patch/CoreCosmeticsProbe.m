#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <ptrauth.h>
#import <stdatomic.h>
#import <math.h>
#import <stdio.h>
#import <unistd.h>
#import <string.h>
#import "CoreCosmeticsRead.h"
#import "CoreCosmeticsVehicleRead.h"
#import "CoreCosmeticsWriteCheck.h"
#import "CoreCosmeticsTrial.h"
#import "CoreCosmeticsVehicleTrial.h"
#import "CoreCosmeticsVehicleEnqueue.h"
#import "CoreCosmeticsPage.h"
#import "CoreCosmeticsUIProfile.h"

// r1/r2 established the character read path and checked native writes on device.
// r3 changes one coat and uses the game's own deferred refresh, then restores it.
typedef struct { uint64_t words[17]; } CCPSnapshot;
typedef bool (*CCPSnapshotFunction)(CCPSnapshot *);
typedef bool (*CCPReadFunction)(uint32_t, uint64_t, size_t, void *);
typedef CCPNativeWriteResult (*CCPWriteFunction)(uint32_t, uint64_t, const void *, const void *, size_t);
typedef struct { uint32_t pid; CCPReadFunction read; CCPWriteFunction write; } CCPTarget;
typedef struct {
    uintptr_t coreBase;
    uint64_t game, pawn, avatar, controller, vehicle, vehicleUser, vehicleComponentOffset;
    uint64_t vehicleComponentHeap, vehicleComponentBitmapWord;
    uint32_t vehicleComponentBit;
    CCPSnapshot snapshot;
    uint8_t binding[0x30];
    CCPSnapshotFunction getSnapshot;
    CCPTarget target;
    bool sessionFromPublishedGlobals;
} CCPResolvedTarget;
static CCPResolvedTarget TrialContext;
static CCPCoatTrial TrialState;
static CCPResolvedTarget VehicleContext;
static CCPVehicleTrial VehicleState;
static atomic_int MenuStatus = CCPMenuIdle;
static atomic_bool MenuBusy = false;
static atomic_bool MenuOwned = false;
static atomic_uint MenuPresetIndex = 0;
static const uint8_t CoreUUID[16] = {
    0x4e,0xd1,0xfd,0x29,0xa4,0x6a,0x36,0xb3,
    0x8d,0xfc,0x36,0xf6,0x7e,0xa9,0x29,0xd1
};
static const uint8_t SnapshotPrefix[32] = {
    0xe0,0x03,0x00,0xb4,0x7f,0x23,0x03,0xd5,0xf4,0x4f,0xbe,0xa9,0xfd,0x7b,0x01,0xa9,
    0xfd,0x43,0x00,0x91,0xf3,0x03,0x00,0xaa,0x94,0x5b,0x00,0xb0,0x94,0xe2,0x06,0x91
};
static const uint8_t ReadPrefix[32] = {
    0xc5,0xee,0xff,0x17,0x7f,0x23,0x03,0xd5,0xf6,0x57,0xbd,0xa9,0xf4,0x4f,0x01,0xa9,
    0xfd,0x7b,0x02,0xa9,0xfd,0x83,0x00,0x91,0xf3,0x03,0x00,0xaa,0x40,0x00,0x00,0xb4
};
static const uint8_t WritePrefix[32] = {
    0x7f,0x23,0x03,0xd5,0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,
    0xf6,0x57,0x03,0xa9,0xf4,0x4f,0x04,0xa9,0xfd,0x7b,0x05,0xa9,0xfd,0x43,0x01,0x91
};
static const uint8_t ProcessLookupPrefix[32] = {
    0x7f,0x23,0x03,0xd5,0xf8,0x5f,0xbc,0xa9,0xf6,0x57,0x01,0xa9,0xf4,0x4f,0x02,0xa9,
    0xfd,0x7b,0x03,0xa9,0xfd,0xc3,0x00,0x91,0xf3,0x03,0x00,0xaa,0x83,0xff,0xff,0x97
};
typedef uint64_t (*CCPProcessLookup)(const char *);
static const uint8_t ProcessLookupByPidPrefix[32] = {
    0x7f,0x23,0x03,0xd5,0xf6,0x57,0xbd,0xa9,0xf4,0x4f,0x01,0xa9,0xfd,0x7b,0x02,0xa9,
    0xfd,0x83,0x00,0x91,0xf3,0x03,0x00,0xaa,0xae,0xff,0xff,0x97,0xf4,0x03,0x00,0xaa
};
typedef uint64_t (*CCPProcessLookupByPid)(uint32_t);
static const uint8_t RemoteOpenPrefix[32] = {
    0x0b,0x24,0xff,0x17,0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,
    0xf6,0x57,0x03,0xa9,0xf4,0x4f,0x04,0xa9,0xfd,0x7b,0x05,0xa9,0xfd,0x43,0x01,0x91
};
typedef uint64_t (*CCPRemoteOpen)(const char *, uint64_t);
// 0x3A3E8 is the wrapper the open body calls at 0x3AE80 with "getpid".
// It resolves the symbol locally, strips the pointer, and calls 0x39C74.
// Success is the low 32 bits equal to 1; the output word is the remote return.
// File bytes at RVA 0x3A3E8 in the pinned Core image.
static const uint8_t RemoteSymbolPrefix[32] = {
    0x7f,0x23,0x03,0xd5,0xff,0x43,0x01,0xd1,0xf6,0x57,0x02,0xa9,0xf4,0x4f,0x03,0xa9,
    0xfd,0x7b,0x04,0xa9,0xfd,0x03,0x01,0x91,0xf3,0x03,0x03,0xaa,0xf4,0x03,0x02,0xaa
};
typedef uint64_t (*CCPRemoteSymbolCall)(const char *, uint64_t, uint64_t, uint64_t *);
// Core 0x39A80: remote call by address. Eight argument words become the remote
// x0..x7; two trailing stack pointers receive the remote x0/x1 results.
static const uint8_t RemoteCallByAddressPrefix[32] = {
    0x7f,0x23,0x03,0xd5,0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,
    0xf6,0x57,0x03,0xa9,0xf4,0x4f,0x04,0xa9,0xfd,0x7b,0x05,0xa9,0xfd,0x43,0x01,0x91
};
typedef uint64_t (*CCPRemoteCallByAddress)(uint64_t timeout, uint64_t function, const char *label,
                                           uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                                           uint64_t a5, uint64_t a6, uint64_t a7,
                                           uint64_t *out0, uint64_t *out1);
enum {
    CCPGameIsSafeRVA = 0x42c4c9c,
    CCPGameTaskGraphGetRVA = 0x8af6894,
    CCPGameCreateAndDispatchRVA = 0x3765444,
    CCPGamePutOnByResRVA = 0x47f2824,
    CCPTargetSkinItemID = 3349002,
    CCPTargetSkinResourceID = 334900200,
    CCPSourceTrialSkinID = 3349001
};
static bool LocalRead(void *context, uint64_t address, void *out, size_t length) {
    vm_size_t copied = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)address, (vm_size_t)length,
        (vm_address_t)out, &copied) == KERN_SUCCESS && copied == length;
}
static bool TargetRead(void *context, uint64_t address, void *out, size_t length) {
    CCPTarget *target = context;
    return target->read(target->pid, address, length, out);
}
static CCPNativeWriteResult TargetWrite(void *context, uint64_t address, const void *expected,
                                        const void *replacement, size_t length) {
    CCPTarget *target = context;
    return target->write(target->pid, address, expected, replacement, length);
}
static void *SignedEntry(uintptr_t address) {
#if __has_feature(ptrauth_calls)
    return ptrauth_sign_unauthenticated((void *)address, ptrauth_key_function_pointer, 0);
#else
    return (void *)address;
#endif
}
static NSString *QuarantineReason(uintptr_t base) {
    CCPReader local = {LocalRead, NULL};
    uint64_t pointer = 0;
    if (!CCPReadBytes(local, base + 0xC1D300, &pointer, sizeof(pointer)) || pointer == 0) return @"none";
    if (pointer < base + 0x4000 || pointer >= base + 0xB88000) return @"outside-image";
    uint8_t bytes[96];
    if (!CCPReadBytes(local, pointer, bytes, sizeof(bytes))) return @"unreadable";
    unsigned length = 0;
    while (length < sizeof(bytes) && bytes[length] != 0) {
        if (bytes[length] < 0x20 || bytes[length] > 0x7e) return @"unreadable";
        length++;
    }
    if (length == 0 || length == sizeof(bytes)) return @"unreadable";
    return [[NSString alloc] initWithBytes:bytes length:length encoding:NSUTF8StringEncoding] ?: @"unreadable";
}
static bool EntryMatches(uintptr_t address, const uint8_t expected[32]) {
    uint8_t bytes[32];
    return CCPReadBytes((CCPReader){LocalRead, NULL}, address, bytes, sizeof(bytes)) &&
        !memcmp(bytes, expected, sizeof(bytes));
}
// After getpid matches the published game session: read-only diagnostics only.
// Never remoteCall into game code here — r30 froze the GameThread when open-time
// paths reached CAD/PutOn, and open-time IsSafe/Get still execute arbitrary game
// functions. PutOn stays menu-explicit with split RX/RW pages.
static NSDictionary *RunGameThreadProbe(uintptr_t base) {
    NSMutableDictionary *out = [@{
        @"attempted": @NO, @"remote_call_entry": @NO, @"game_base_read": @NO,
        @"game_identity": @"not-checked", @"is_safe_call": @"skipped-open-time-remote-call",
        @"is_safe_on_worker": @NO, @"taskgraph_get_call": @"skipped-open-time-remote-call",
        @"taskgraph_present": @NO, @"create_and_dispatch_ready": @NO,
        @"create_and_dispatch_blocked_by": @"menu-explicit-only",
        @"vehicle_resolve": @"not-attempted", @"enqueue_status": @"not-attempted",
        @"put_on_attempted": @NO, @"vehicle_appearance_changes_implemented": @NO,
        @"status": @"not-started"
    } mutableCopy];
    if (!EntryMatches(base + 0x39A80, RemoteCallByAddressPrefix) ||
        !EntryMatches(base + 0x64D04, ReadPrefix) ||
        !EntryMatches(base + 0x64C7C, SnapshotPrefix)) {
        out[@"status"] = @"remote-call-entry-mismatch";
        return out;
    }
    out[@"remote_call_entry"] = @YES;
    out[@"attempted"] = @YES;
    CCPReader local = {LocalRead, NULL};
    // r26/r29 device path publishes pid/ready/port without filling snapshot words[1].
    // Prefer the same published globals that already validated getpid.
    uint32_t publishedPid = 0, remoteReady = 0;
    uint64_t remotePort = 0;
    BOOL pidOK = CCPReadBytes(local, base + 0xC53460, &publishedPid, sizeof(publishedPid)) && publishedPid != 0;
    BOOL readyOK = CCPReadBytes(local, base + 0xBD4C5C, &remoteReady, sizeof(remoteReady)) && remoteReady != 0;
    BOOL portOK = CCPReadBytes(local, base + 0xC534B0, &remotePort, sizeof(remotePort)) && remotePort != 0;
    CCPSnapshotFunction snapshot = (CCPSnapshotFunction)SignedEntry(base + 0x64C7C);
    CCPSnapshot snap = {0};
    BOOL snapOK = snapshot(&snap) && snap.words[1] != 0;
    uint32_t pid = 0;
    if (snapOK) {
        pid = CCPU32((const uint8_t *)&snap + 0x18);
        out[@"session_source"] = @"snapshot";
    }
    if ((!snapOK || pid == 0) && pidOK && readyOK && portOK) {
        pid = publishedPid;
        out[@"session_source"] = snapOK ? @"published-pid-override" : @"published-globals";
    }
    if (pid == 0) {
        out[@"status"] = @"core-session-not-ready";
        out[@"snapshot_ready"] = @(snapOK);
        out[@"published_pid"] = @(pidOK);
        out[@"published_ready"] = @(readyOK);
        out[@"published_port"] = @(portOK);
        return out;
    }
    out[@"session_pid"] = @(pid);
    uint64_t game = 0, pawn = 0, controller = 0;
    if (!CCPReadBytes(local, base + 0xC54270 + 0x18, &game, sizeof(game)) || !CCPPointer(game)) {
        out[@"status"] = @"game-base-unavailable";
        return out;
    }
    out[@"game_base_read"] = @YES;
    CCPReadBytes(local, base + 0xC54270 + 0x3A0, &pawn, sizeof(pawn));
    CCPReadBytes(local, base + 0xC54270 + 0x390, &controller, sizeof(controller));
    CCPTarget target = {.pid = pid, .read = (CCPReadFunction)SignedEntry(base + 0x64D04), .write = NULL};
    CCPReader remote = {TargetRead, &target};
    if (!CCPIdentity(remote, game, CCPGameUUID)) {
        out[@"game_identity"] = @"mismatch";
        out[@"status"] = @"game-identity-mismatch";
        return out;
    }
    out[@"game_identity"] = @"matched";
    out[@"create_and_dispatch_rva"] = @"0x3765444";
    out[@"put_on_rva"] = @"0x47f2824";
    out[@"create_and_dispatch_ready"] = @YES;
    out[@"create_and_dispatch_blocked_by"] = @"menu-explicit-only";
    if (CCPPointer(pawn) && CCPPointer(controller)) {
        CCPVehicleSample sample = {0};
        uint64_t actor = 0, user = 0, vehicle = 0, avatar = 0;
        out[@"vehicle_resolve"] = @"attempted";
        if (CCPVehicleSource(remote, game, pawn, controller, &actor, &user, &vehicle, &sample) &&
            CCPVehicleOwner(remote, vehicle, &avatar)) {
            out[@"vehicle_resolve"] = @"avatar-ready";
            out[@"vehicle_skin_id"] = @(sample.skin);
            out[@"source_skin_is_trial"] = @(sample.skin == CCPSourceTrialSkinID);
            out[@"target_skin_item_id"] = @(CCPTargetSkinItemID);
            out[@"target_skin_resource_id"] = @(CCPTargetSkinResourceID);
        } else if (sample.status == CCPVehicleUnavailable) {
            out[@"vehicle_resolve"] = @"no-current-vehicle";
        } else {
            out[@"vehicle_resolve"] = @"unconfirmed";
        }
    } else {
        out[@"vehicle_resolve"] = @"pawn-or-controller-missing";
    }
    out[@"put_on_attempted"] = @NO;
    out[@"enqueue_status"] = @"menu-explicit-only";
    out[@"enqueue_disabled_reason"] =
        @"session-open-never-enqueues;use-menu-puton-split-rx-rw";
    out[@"status"] = @"game-thread-probe-ready";
    return out;
}
static NSArray *ItemsJSON(const CCPItems *items) {
    NSMutableArray *result = [NSMutableArray arrayWithCapacity:items->count];
    for (uint32_t i = 0; i < items->count; i++) {
        CCPItem item = items->items[i];
        if (item.type == 0 && item.item == 0) continue;
        [result addObject:@{@"slot_index": @(i), @"type": @(item.type), @"type_specific_id": @(item.item),
            @"gender": @(item.gender), @"operation": @(item.operation), @"version": @(item.version)}];
    }
    return result;
}
static NSDictionary *VehicleEntityJSON(CCPVehicleEntityDiagnostic entity) {
    NSArray *statuses = @[@"not-attempted", @"vehicle-entity-state-sampled", @"entity-map-read-failed",
        @"entity-map-layout-unconfirmed", @"master-entity-not-found", @"master-entity-ambiguous",
        @"entity-class-unavailable", @"entity-class-unconfirmed", @"entity-owner-unconfirmed",
        @"entity-state-read-failed", @"entity-state-layout-unconfirmed", @"entity-changed-during-sample"];
    return @{@"status": statuses[entity.status], @"diagnostic_revision": @10,
        @"read_only": @YES, @"target_resource_availability": @"not-checked",
        @"resource_scope": @"current-entity-loaded-state",
        @"entity_map_header_read": @(entity.headerRead), @"entity_bitmap_read": @(entity.bitmapRead),
        @"entity_bitmap_on_heap": @(entity.heapBitmap), @"entity_map_slots": @(entity.mapSlots),
        @"entity_map_occupied": @(entity.mapOccupied), @"master_entity_matches": @(entity.masterMatches),
        @"native_hash_lookup_matches": @(entity.hashMatches), @"entity_state_stable": @(entity.stable),
        @"base_entity_class_available": @(entity.baseClassAvailable), @"vehicle_entity_class_available": @(entity.vehicleClassAvailable),
        @"master_entity_class_available": @(entity.masterClassAvailable), @"base_entity_class_matches": @(entity.baseClassMatches),
        @"vehicle_entity_class_matches": @(entity.vehicleClassMatches), @"master_entity_class_matches": @(entity.masterClassMatches),
        @"entity_owner_field_read": @(entity.ownerRead), @"entity_avatar_matches": @(entity.ownerMatches),
        @"entity_slot": @(entity.slot), @"entity_subslot": @(entity.subslot),
        @"loaded_handle_available": @(entity.handleAvailable), @"loaded_handle_class_available": @(entity.handleClassAvailable),
        @"loaded_handle_class_matches": @(entity.handleClassMatches), @"loaded_handle_key_read": @(entity.handleKeyRead),
        @"loaded_handle_item_type": @(entity.loadedType), @"loaded_handle_item_id": @(entity.loadedID),
        @"loaded_handle_matches_trial_target": @(entity.handleKeyRead && entity.loadedID == 3349002),
        @"cached_handle_available": @(entity.cachedHandleAvailable), @"delay_render_handle_available": @(entity.delayedHandleAvailable),
        @"cached_handle_matches_loaded": @(entity.cachedMatchesLoaded), @"mesh_component_available": @(entity.meshComponentAvailable),
        @"mesh_asset_path_present": @(entity.meshPathPresent), @"last_mesh_path_present": @(entity.lastMeshPathPresent),
        @"render_flags_read": @(entity.renderFlagsRead), @"render_dirty": @(entity.renderDirty),
        @"delay_render_dirty": @(entity.delayedRenderDirty), @"appearance_gender_dirty": @(entity.appearanceDirty),
        @"cached_counter_count": @(entity.cachedCounterCount), @"cached_update_counter_read": @(entity.cachedCounterRead),
        @"cached_update_counter": @(entity.cachedCounter),
        @"entity_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)entity.tableRVA],
        @"entity_install_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)entity.installRVA],
        @"entity_material_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)entity.materialRVA],
        @"entity_behavior_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)entity.behaviorRVA]};
}
static NSDictionary *VehicleCandidate(CCPReader remote, uint64_t game, uint64_t pawn, uint64_t controller) {
    CCPVehicleSample sample = {0};
    BOOL sampled = CCPReadVehicle(remote, game, pawn, controller, &sample);
    NSArray *statuses = @[@"vehicle-layout-candidate-sampled", @"current-vehicle-unavailable",
        @"vehicle-avatar-owner-unconfirmed", @"vehicle-refresh-methods-unconfirmed",
        @"vehicle-sync-array-unconfirmed", @"vehicle-changed-during-sample",
        @"vehicle-user-read-failed", @"vehicle-user-component-unavailable", @"vehicle-user-owner-unconfirmed",
        @"current-vehicle-read-failed", @"current-vehicle-pointer-invalid",
        @"character-resolver-unconfirmed", @"controller-class-unavailable", @"source-controller-unconfirmed",
        @"vehicle-user-class-unavailable", @"component-set-read-failed", @"component-set-layout-unconfirmed",
        @"component-set-changed", @"vehicle-user-components-ambiguous", @"vehicle-user-character-unconfirmed"];
    NSArray *lookupStatuses = @[@"found", @"not-found", @"read-failed", @"layout-unconfirmed", @"changed", @"ambiguous"];
    NSDictionary *path = @{@"source_resolver_matches": @(sample.resolverMatches),
        @"controller_class_available": @(sample.controllerClassAvailable), @"vehicle_user_class_available": @(sample.userClassAvailable),
        @"pawn_controller_field_read": @(sample.primaryRead), @"pawn_controller_nonzero": @(sample.primaryNonzero),
        @"pawn_controller_class_matches": @(sample.primaryClassMatches), @"fallback_controller_field_read": @(sample.fallbackRead),
        @"fallback_controller_used": @(sample.usedFallback), @"source_matches_core_controller": @(sample.sourceMatchesController),
        @"controller_pawn_field_read": @(sample.controllerPawnRead), @"controller_pawn_matches": @(sample.controllerPawnMatches),
        @"component_lookup_attempted": @(sample.lookupAttempted),
        @"component_lookup_status": sample.lookupAttempted ? lookupStatuses[sample.components.status] : @"not-attempted",
        @"component_set_header_read": @(sample.components.headerRead), @"component_bitmap_read": @(sample.components.bitmapRead),
        @"component_set_stable": @(sample.components.stable), @"component_bitmap_on_heap": @(sample.components.heapBitmap),
        @"component_slots": @(sample.components.slots), @"occupied_component_slots": @(sample.components.occupied),
        @"component_array_count": @(sample.components.dataCount), @"component_array_capacity": @(sample.components.dataCapacity),
        @"component_bitmap_count": @(sample.components.bitCount), @"component_bitmap_capacity": @(sample.components.bitCapacity),
        @"vehicle_user_available": @(sample.userAvailable), @"vehicle_user_owner_matches": @(sample.userOwnerMatches),
        @"vehicle_user_character_field_read": @(sample.userCharacterRead), @"vehicle_user_character_matches": @(sample.userCharacterMatches),
        @"vehicle_user_character_nonzero": @(sample.userCharacterNonzero),
        @"vehicle_user_character_pointer_valid": @(sample.userCharacterPointerValid),
        @"current_vehicle_field_read": @(sample.vehicleFieldRead), @"current_vehicle_nonzero": @(sample.vehicleNonzero),
        @"current_vehicle_pointer_valid": @(sample.vehiclePointerValid)};
    if (!sampled) return @{@"status": statuses[sample.status], @"vehicle_writes_implemented": @NO,
        @"vehicle_path": @"controller-owned-vehicle-user-component", @"path_diagnostics": path,
        @"source_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.sourceTableRVA],
        @"source_resolver_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.resolverRVA],
        @"vehicle_user_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.userTableRVA],
        @"vehicle_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.vehicleTableRVA],
        @"avatar_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.avatarTableRVA],
        @"avatar_onrep_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.onrepRVA],
        @"avatar_tick_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.tickRVA]};
    NSMutableArray *items = [NSMutableArray arrayWithCapacity:sample.count];
    for (uint32_t i = 0; i < sample.count; i++) {
        CCPVehicleItem item = sample.items[i];
        [items addObject:@{@"slot_index": @(i), @"type": @(item.type), @"type_specific_id": @(item.item),
            @"native_update_counter": @(item.updateCounter)}];
    }
    return @{@"status": statuses[sample.status], @"vehicle_writes_implemented": @NO,
        @"vehicle_path": @"controller-owned-vehicle-user-component", @"path_diagnostics": path,
        @"source_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.sourceTableRVA],
        @"source_resolver_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.resolverRVA],
        @"vehicle_user_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.userTableRVA],
        @"component_owner_matches": @YES, @"sync_array_stable": @YES,
        @"vehicle_skin_id": @(sample.skin), @"previous_skin_id": @(sample.previousSkin),
        @"pending_skin_id": @(sample.pendingSkin), @"real_skin_id": @(sample.realSkin),
        @"default_skin_id": @(sample.defaultSkin), @"sync_item_count": @(sample.count), @"sync_items": items,
        @"master_item_matches_vehicle_skin": @(sample.count > 0 && sample.items[0].item == sample.skin),
        @"avatar_refresh_pending": @(sample.refreshPending), @"refresh_diagnostics": VehicleEntityJSON(sample.entity),
        @"vehicle_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.vehicleTableRVA],
        @"avatar_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.avatarTableRVA],
        @"avatar_onrep_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.onrepRVA],
        @"avatar_tick_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)sample.tickRVA]};
}
static NSDictionary *WeaponCandidate(CCPReader remote, uint64_t game, uint64_t pawn) {
    uint64_t manager = 0, weapon = 0, avatar = 0, owner = 0, table = 0;
    if (!CCPReadBytes(remote, pawn + 0x37A0, &manager, 8) || !CCPPointer(manager) ||
        !CCPReadBytes(remote, manager + 0x990, &weapon, 8) || !CCPPointer(weapon))
        return @{@"status": @"held-weapon-unavailable", @"writes_implemented": @NO};
    if (!CCPReadBytes(remote, weapon + 0xA50, &avatar, 8) || !CCPPointer(avatar) ||
        !CCPReadBytes(remote, avatar + 0x20, &owner, 8) || owner != weapon ||
        !CCPReadBytes(remote, avatar, &table, 8) || table < game || table - game >= 0x120E0000)
        return @{@"status": @"weapon-avatar-owner-unconfirmed", @"writes_implemented": @NO};
    CCPItems client = {0}, display = {0};
    bool clientOK = CCPReadItems(remote, avatar + 0x1400, &client);
    bool displayOK = CCPReadItems(remote, avatar + 0x1418, &display);
    uint64_t weaponAfter = 0, avatarAfter = 0;
    if (!CCPReadBytes(remote, manager + 0x990, &weaponAfter, 8) || weaponAfter != weapon ||
        !CCPReadBytes(remote, weapon + 0xA50, &avatarAfter, 8) || avatarAfter != avatar)
        return @{@"status": @"held-weapon-changed", @"writes_implemented": @NO};
    return @{@"status": @"weapon-layout-candidate-sampled", @"writes_implemented": @NO,
        @"component_owner_matches": @YES, @"vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)(table - game)],
        @"client_array_stable": @(clientOK), @"display_array_stable": @(displayOK),
        @"client_items": ItemsJSON(&client), @"display_items": ItemsJSON(&display)};
}
static bool ResolveSessionPid(uintptr_t base, CCPSnapshotFunction snapshotFn, CCPSnapshot *snapOut,
                              uint32_t *pidOut, bool *fromPublished) {
    CCPReader local = {LocalRead, NULL};
    CCPSnapshot snap = {0};
    BOOL snapOK = snapshotFn(&snap) && snap.words[1] != 0;
    uint32_t publishedPid = 0, remoteReady = 0;
    uint64_t remotePort = 0;
    BOOL pidOK = CCPReadBytes(local, base + 0xC53460, &publishedPid, sizeof(publishedPid)) && publishedPid != 0;
    BOOL readyOK = CCPReadBytes(local, base + 0xBD4C5C, &remoteReady, sizeof(remoteReady)) && remoteReady != 0;
    BOOL portOK = CCPReadBytes(local, base + 0xC534B0, &remotePort, sizeof(remotePort)) && remotePort != 0;
    uint32_t pid = 0;
    bool published = false;
    if (snapOK) pid = CCPU32((const uint8_t *)&snap + 0x18);
    if ((!snapOK || pid == 0) && pidOK && readyOK && portOK) {
        pid = publishedPid;
        published = true;
    }
    if (pid == 0 || pid > INT32_MAX) return false;
    if (snapOut) *snapOut = snap;
    if (pidOut) *pidOut = pid;
    if (fromPublished) *fromPublished = published;
    return true;
}
static NSDictionary *Sample(BOOL writeCheck, CCPResolvedTarget *resolved, BOOL vehicleOnly) {
    CCPReader local = {LocalRead, NULL};
    uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
    if (!CCPIdentity(local, base, CoreUUID) || !EntryMatches(base + 0x64C7C, SnapshotPrefix) ||
        !EntryMatches(base + 0x64D04, ReadPrefix)) return @{@"status": @"core-identity-mismatch"};
    CCPSnapshotFunction snapshot = (CCPSnapshotFunction)SignedEntry(base + 0x64C7C);
    CCPSnapshot before = {0}, after = {0};
    uint32_t sessionPid = 0;
    bool fromPublished = false;
    if (!ResolveSessionPid(base, snapshot, &before, &sessionPid, &fromPublished))
        return @{@"status": @"core-session-not-ready"};
    CCPTarget target = {.pid = sessionPid,
                        .read = (CCPReadFunction)SignedEntry(base + 0x64D04), .write = NULL};
    if (!target.pid || target.pid > INT32_MAX) return @{@"status": @"target-not-bound"};
    CCPReader remote = {TargetRead, &target};
    uint8_t config[0x3A8], configAfter[sizeof(config)];
    if (!CCPReadBytes(local, base + 0xC54270, config, sizeof(config))) return @{@"status": @"core-context-unavailable"};
    uint64_t game = CCPU64(config + 0x18), pawn = CCPU64(config + 0x3A0);
    const char *gameIdentity = CCPIdentityDetail(remote, game, CCPGameUUID);
    if (gameIdentity) return @{@"status": [NSString stringWithUTF8String:gameIdentity],
        @"identity_scope": @"core-cached-game-image"};
    if (!CCPPointer(pawn)) return @{@"status": @"local-character-unavailable", @"game_uuid_matches": @YES,
        @"core_controller_available": @(CCPPointer(CCPU64(config + 0x390))),
        @"core_world_available": @(CCPPointer(CCPU64(config + 0x370)))};
    if (vehicleOnly) {
        uint64_t controller = CCPU64(config + 0x390), controllerPawn = 0, pawnTable = 0, getter = 0;
        uint8_t binding[0x30], bindingAfter[sizeof(binding)];
        if (!CCPReadBytes(local, base + 0xC288B0, binding, sizeof(binding)) || CCPU32(binding + 0x28) != target.pid)
            return @{@"status": @"vehicle-binding-unconfirmed"};
        NSDictionary *vehicle = VehicleCandidate(remote, game, pawn, controller);
        uint64_t characterAvatar = 0, characterOwner = 0;
        BOOL controllerRead = CCPPointer(controller) && CCPReadBytes(remote, controller + 0x3540, &controllerPawn, 8);
        BOOL characterOwnerMatches = CCPReadBytes(remote, pawn + CCP_AVATAR_OFFSET, &characterAvatar, 8) &&
            CCPPointer(characterAvatar) && CCPReadBytes(remote, characterAvatar + 0x20, &characterOwner, 8) && characterOwner == pawn;
        BOOL pawnTableRead = CCPReadBytes(remote, pawn, &pawnTable, 8) && pawnTable >= game && pawnTable - game < 0x120E0000;
        BOOL getterRead = pawnTableRead && CCPReadBytes(remote, pawnTable + 0x1528, &getter, 8) &&
            getter >= game && getter - game < 0xAF23BE4;
        if ((!fromPublished && (!snapshot(&after) || memcmp(&before, &after, 0x28))) ||
            !CCPReadBytes(local, base + 0xC54270, configAfter, sizeof(configAfter)) ||
            CCPU64(configAfter + 0x18) != game || CCPU64(configAfter + 0x3A0) != pawn ||
            CCPU64(configAfter + 0x390) != controller ||
            !CCPReadBytes(local, base + 0xC288B0, bindingAfter, sizeof(bindingAfter)) ||
            memcmp(binding, bindingAfter, sizeof(binding)))
            return @{@"status": @"session-or-character-changed"};
        if (resolved && [vehicle[@"sync_array_stable"] boolValue] && EntryMatches(base + 0x620B8, WritePrefix)) {
            uint64_t actor = 0, user = 0, currentVehicle = 0, vehicleAvatar = 0;
            uint64_t componentHeap = 0;
            CCPVehicleSample diagnostic = {0};
            BOOL owned = CCPVehicleSource(remote, game, pawn, controller, &actor, &user, &currentVehicle, &diagnostic) &&
                CCPVehicleOwner(remote, currentVehicle, &vehicleAvatar);
            if (owned) owned = CCPReadBytes(remote, controller + 0x460, &componentHeap, sizeof(componentHeap)) &&
                (!componentHeap || CCPPointer(componentHeap));
            uint32_t selected = diagnostic.components.selectedIndex;
            uint64_t bitmap = componentHeap ? componentHeap : controller + 0x450;
            *resolved = (CCPResolvedTarget){.coreBase = base, .game = game, .pawn = pawn, .controller = controller,
                .avatar = owned ? vehicleAvatar : 0, .vehicle = owned ? currentVehicle : 0,
                .vehicleUser = owned ? user : 0,
                .vehicleComponentOffset = owned ? (uint64_t)selected * 16u : 0,
                .vehicleComponentHeap = owned ? componentHeap : 0,
                .vehicleComponentBitmapWord = owned ? bitmap + (uint64_t)(selected / 32u) * 4u : 0,
                .vehicleComponentBit = owned ? UINT32_C(1) << (selected % 32u) : 0,
                .snapshot = before, .getSnapshot = snapshot, .target = target,
                .sessionFromPublishedGlobals = fromPublished};
            memcpy(resolved->binding, binding, sizeof(binding));
            resolved->target.write = (CCPWriteFunction)SignedEntry(base + 0x620B8);
        }
        return @{@"status": @"vehicle-session-sampled", @"vehicle_candidate": vehicle,
            @"session_from_published_globals": @(fromPublished),
            @"core_context": @{@"controller_pawn_field_read": @(controllerRead),
                @"controller_pawn_available": @(controllerRead && CCPPointer(controllerPawn)),
                @"controller_pawn_matches_cached": @(controllerRead && controllerPawn == pawn),
                @"character_avatar_owner_matches": @(characterOwnerMatches),
                @"pawn_vtable_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)(pawnTableRead ? pawnTable - game : 0)],
                @"character_resolver_rva": [NSString stringWithFormat:@"0x%llx", (unsigned long long)(getterRead ? getter - game : 0)]}};
    }
    uint64_t avatar = 0, outer = 0, vtable = 0;
    if (!CCPReadBytes(remote, pawn + CCP_AVATAR_OFFSET, &avatar, sizeof(avatar)) || !CCPPointer(avatar))
        return @{@"status": @"avatar-component-unavailable", @"game_uuid_matches": @YES};
    if (!CCPReadBytes(remote, avatar, &vtable, sizeof(vtable)) || vtable < game || vtable - game >= 0x120E0000 ||
        !CCPReadBytes(remote, avatar + 0x20, &outer, sizeof(outer)) || outer != pawn)
        return @{@"status": @"avatar-owner-unconfirmed", @"game_uuid_matches": @YES};
    CCPItems sync = {0}, client = {0};
    bool syncOK = CCPReadItems(remote, avatar + CCP_SYNC_OFFSET, &sync);
    bool clientOK = CCPReadItems(remote, avatar + CCP_CLIENT_OFFSET, &client);
    uint64_t avatarAfter = 0;
    if (!snapshot(&after) || memcmp(&before, &after, 0x28) ||
        !CCPReadBytes(local, base + 0xC54270, configAfter, sizeof(configAfter)) ||
        CCPU64(configAfter + 0x18) != game || CCPU64(configAfter + 0x3A0) != pawn ||
        !CCPReadBytes(remote, pawn + CCP_AVATAR_OFFSET, &avatarAfter, sizeof(avatarAfter)) || avatarAfter != avatar)
        return @{@"status": @"session-or-character-changed"};
    NSMutableDictionary *result = [@{@"status": syncOK || clientOK ? @"candidate-layout-sampled" : @"avatar-arrays-unconfirmed",
        @"game_uuid_matches": @YES, @"component_owner_matches": @YES,
        @"sync_array_stable": @(syncOK), @"client_array_stable": @(clientOK),
        @"sync_item_count": @(sync.count), @"client_item_count": @(client.count), @"zero_slots_omitted": @YES,
        @"sync_items": ItemsJSON(&sync), @"client_items": ItemsJSON(&client)} mutableCopy];
    result[@"weapon_candidate"] = WeaponCandidate(remote, game, pawn);
    result[@"vehicle_candidate"] = VehicleCandidate(remote, game, pawn, CCPU64(config + 0x390));
    uint64_t onrep = 0;
    if (CCPReadBytes(remote, vtable + 0x1320, &onrep, sizeof(onrep)) && onrep >= game && onrep - game < 0xAF23BE4)
        result[@"avatar_onrep_rva"] = [NSString stringWithFormat:@"0x%llx", (unsigned long long)(onrep - game)];
    if (resolved && syncOK && onrep == game + 0x43D2EB0 && EntryMatches(base + 0x620B8, WritePrefix)) {
        uint8_t binding[0x30], repeated[0x30];
        if (CCPReadBytes(local, base + 0xC288B0, binding, sizeof(binding)) &&
            CCPReadBytes(local, base + 0xC288B0, repeated, sizeof(repeated)) &&
            !memcmp(binding, repeated, sizeof(binding)) && CCPU32(binding + 0x28) == target.pid) {
            *resolved = (CCPResolvedTarget){.coreBase = base, .game = game, .pawn = pawn, .avatar = avatar,
                .controller = CCPU64(config + 0x390),
                .snapshot = before, .getSnapshot = snapshot, .target = target};
            memcpy(resolved->binding, binding, sizeof(binding));
            resolved->target.write = (CCPWriteFunction)SignedEntry(base + 0x620B8);
        }
    }
    if (writeCheck && syncOK) {
        if (!EntryMatches(base + 0x620B8, WritePrefix)) {
            result[@"write_check"] = @{@"attempted": @NO, @"status": @"core-write-entry-unconfirmed"};
        } else {
            target.write = (CCPWriteFunction)SignedEntry(base + 0x620B8);
            uint8_t capabilityA = 0, capabilityB = 0, primitiveReady = 0;
            BOOL capabilitiesRead = CCPReadBytes(local, base + 0xC27B2C, &capabilityA, 1) &&
                CCPReadBytes(local, base + 0xC27CA4, &capabilityB, 1) &&
                CCPReadBytes(local, base + 0xC3D59D, &primitiveReady, 1);
            CCPWriteCheck check = CCPCheckInitialOutfitWrite(remote, TargetWrite, &target, avatar + CCP_SYNC_OFFSET);
            result[@"write_check"] = @{@"attempted": @(check.attempted),
                @"status": !check.attempted ? @"initial-outfit-required" :
                    (check.nativeCommitted && check.readbackStable ? @"same-value-write-confirmed" : @"native-write-not-confirmed"),
                @"native_status": @(check.nativeStatus), @"written_bytes": @(check.writtenBytes),
                @"readback_stable": @(check.readbackStable), @"appearance_value_changed": @NO,
                @"capability_snapshot_read": @(capabilitiesRead), @"capability_a": @(capabilityA),
                @"capability_b": @(capabilityB), @"primitive_ready_bit": @(primitiveReady & 1)};
        }
    }
    return result;
}

static bool TrialBindingValid(void *context) {
    CCPResolvedTarget *target = context;
    CCPReader local = {LocalRead, NULL}, remote = {TargetRead, &target->target};
    CCPSnapshot now = {0}; uint8_t binding[0x30]; uint64_t game = 0, pawn = 0, controller = 0;
    uint32_t pid = 0;
    bool published = false;
    if (target->coreBase != (uintptr_t)_dyld_get_image_header(0) ||
        !EntryMatches(target->coreBase + 0x620B8, WritePrefix) ||
        !ResolveSessionPid(target->coreBase, target->getSnapshot, &now, &pid, &published) ||
        pid != target->target.pid ||
        !CCPReadBytes(local, target->coreBase + 0xC288B0, binding, sizeof(binding)) ||
        memcmp(binding, target->binding, sizeof(binding)) != 0 ||
        !CCPReadBytes(local, target->coreBase + 0xC54270 + 0x18, &game, sizeof(game)) || game != target->game ||
        !CCPReadBytes(local, target->coreBase + 0xC54270 + 0x3A0, &pawn, sizeof(pawn)) || pawn != target->pawn ||
        !CCPReadBytes(local, target->coreBase + 0xC54270 + 0x390, &controller, sizeof(controller)) ||
        controller != target->controller ||
        !CCPIdentity(remote, target->game, CCPGameUUID)) return false;
    if (!target->sessionFromPublishedGlobals && !published &&
        now.words[1] != target->snapshot.words[1]) return false;
    return true;
}
static bool VehicleContextSample(CCPResolvedTarget *target, uint32_t expectedItem,
                                 bool requireAppearanceReady, CCPVehicleSample *sample) {
    if (!target || !sample || !TrialBindingValid(target)) return false;
    CCPReader reader = {TargetRead, &target->target};
    uint64_t actor = 0, user = 0, vehicle = 0, avatar = 0, componentHeap = 0;
    CCPVehicleSample chain = {0};
    if (!CCPVehicleSource(reader, target->game, target->pawn, target->controller,
                          &actor, &user, &vehicle, &chain) ||
        (uint64_t)chain.components.selectedIndex * 16u != target->vehicleComponentOffset ||
        user != target->vehicleUser || vehicle != target->vehicle ||
        !CCPVehicleOwner(reader, vehicle, &avatar) || avatar != target->avatar ||
        !CCPReadBytes(reader, target->controller + 0x460, &componentHeap, sizeof(componentHeap)) ||
        componentHeap != target->vehicleComponentHeap ||
        !CCPReadVehicle(reader, target->game, target->pawn, target->controller, sample))
        return false;
    if (expectedItem && (sample->skin != expectedItem || sample->count == 0 ||
                         sample->items[0].item != expectedItem)) return false;
    return !requireAppearanceReady || CCPVehicleAppearanceReady(sample, expectedItem);
}
static CCPNativeWriteResult TrialTargetWrite(void *context, uint64_t address, const void *expected,
                                            const void *replacement, size_t length) {
    return TargetWrite(&((CCPResolvedTarget *)context)->target, address, expected, replacement, length);
}
static NSDictionary *TrialResultJSON(CCPTrialResult result) {
    NSArray *statuses = @[@"applied", @"restored", @"already-restored", @"binding-changed",
        @"layout-changed", @"matching-equipped-item-required", @"refresh-mode-unsupported", @"native-write-failed",
        @"readback-failed", @"slot-changed-by-game", @"no-active-change"];
    return @{@"status": statuses[result.status], @"native_status": @(result.nativeStatus),
        @"written_bytes": @(result.writtenBytes), @"id_write_confirmed": @(result.idConfirmed),
        @"refresh_queued": @(result.refreshQueued), @"change_still_owned": @(TrialState.active)};
}
@interface CCPCosmeticsController : NSObject
@property(nonatomic, strong) dispatch_queue_t captureQueue;
@property(nonatomic, strong) NSMutableArray *samples;
@property(nonatomic, strong) NSMutableArray *vehicleAttempts;
@property(nonatomic, strong) dispatch_source_t trialTimer;
@property(nonatomic, strong) NSMutableDictionary *trialReport;
@property(nonatomic) BOOL trialFinishing;
@property(nonatomic) uint32_t trialGeneration;
@property(nonatomic) UIBackgroundTaskIdentifier backgroundTask;
@property(nonatomic) BOOL capturing;
@property(nonatomic) uint32_t generation;
@property(nonatomic) NSUInteger presetIndex;
@property(nonatomic) NSUInteger trialPresetIndex;
@property(nonatomic) NSUInteger vehiclePresetIndex;
@property(nonatomic) NSUInteger pendingPresetIndex;
@property(nonatomic) BOOL vehicleCapturing;
@property(nonatomic) BOOL vehicleTrialMode;
@property(nonatomic) BOOL earlyAttachStarted;
@property(nonatomic) BOOL earlyAttachFinished;
- (void)startEarlySession;
- (void)pollEarlyAttachAttempt:(unsigned)attempt;
- (void)startTrial;
- (void)prepareTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
- (void)restoreTrial;
- (void)selectPreset:(uint32_t)index enabled:(BOOL)enabled;
- (void)finishTrialOnQueue:(uint32_t)generation reason:(NSString *)reason;
- (void)finishGeneration:(uint32_t)generation;
- (void)startVehicleCapture;
- (void)captureVehicleOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
- (void)finishVehicleCaptureOnQueue:(uint32_t)generation reason:(NSString *)reason;
- (void)startVehicleTrial:(NSUInteger)presetIndex;
- (void)prepareVehicleTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
- (void)finishVehicleTrialOnQueue:(uint32_t)generation reason:(NSString *)reason;
@end

@implementation CCPCosmeticsController
- (NSURL *)reportURL {
    NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                             inDomains:NSUserDomainMask].firstObject;
    return [documents URLByAppendingPathComponent:@"Core-cosmetics-probe.json"];
}
- (void)writeTrialReport {
    if (!self.trialReport) return;
    self.trialReport[@"captured_at_unix"] = @([NSDate date].timeIntervalSince1970);
    NSData *json = [NSJSONSerialization dataWithJSONObject:self.trialReport options:NSJSONWritingPrettyPrinted error:NULL];
    if (json) [json writeToURL:[self reportURL] options:NSDataWritingAtomic error:NULL];
}
- (void)startTrial {
    if (self.capturing) return;
    if (self.presetIndex >= CCP_PRESET_COUNT) return;
    NSUInteger presetIndex = self.presetIndex;
    self.capturing = YES;
    self.vehicleCapturing = NO;
    self.vehicleTrialMode = NO;
    atomic_store(&MenuBusy, true);
    atomic_store(&MenuStatus, CCPMenuWaiting);
    atomic_store(&MenuPresetIndex, (unsigned)presetIndex);
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core cosmetics trial"
        expirationHandler:^{
            if (self.generation != generation || !self.capturing) return;
            dispatch_async(self.captureQueue, ^{
                if (self.trialGeneration != generation || self.trialFinishing) return;
                // Never auto-restore appearance. An owned change stays until the user cancels.
                if (TrialState.active) {
                    dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
                    return;
                }
                self.trialReport[@"finish_reason"] = @"background-expiration";
                self.trialReport[@"status"] = @"background-expiration-before-apply";
                atomic_store(&MenuStatus, CCPMenuFailed);
                [self writeTrialReport];
                dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
            });
        }];
    dispatch_async(self.captureQueue, ^{
        self.trialGeneration = generation;
        self.trialFinishing = NO;
        if (TrialState.active || VehicleState.active) {
            atomic_store(&MenuStatus, CCPMenuRestorePending);
            dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
            return; // Preserve the journal of an earlier change until restored.
        }
        self.trialPresetIndex = presetIndex;
        CCPAppearancePreset preset = CCPAppearancePresets[presetIndex];
        self.trialReport = [@{@"schema_version": @6, @"trial_revision": @6, @"mode": @"explicit-menu-appearance-apply",
            @"game_version": @"1.38.12", @"game_build": @"15915",
            @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
            @"kernel_writes_implemented": @YES, @"appearance_changes_implemented": @YES,
            @"preset_visual_effect_previously_verified": @(preset.slot == 5 && preset.target == CCP_TRIAL_COAT),
            @"current_trial_visual_confirmed": @NO, @"ui_location": @"native-menu-below-position",
            @"preset": [NSString stringWithUTF8String:preset.label], @"category": [NSString stringWithUTF8String:preset.category],
            @"slot_index": @(preset.slot), @"target_id": @(preset.target),
            @"automatic_restore_seconds": @0, @"restore_mode": @"manual-cancel-only"} mutableCopy];
        [self prepareTrialOnQueue:generation attempt:0];
    });
}
- (void)prepareTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt {
        if (self.trialGeneration != generation || self.trialFinishing) return;
        TrialContext = (CCPResolvedTarget){0};
        NSDictionary *before = Sample(NO, &TrialContext, NO);
        self.trialReport[@"before"] = before;
        if (!TrialContext.avatar || !TrialBindingValid(&TrialContext)) {
            self.trialReport[@"status"] = @"waiting-for-core-or-character";
            self.trialReport[@"waiting_attempts"] = @(attempt + 1);
            [self writeTrialReport];
            if (attempt >= 60) [self finishTrialOnQueue:generation reason:@"readiness-timeout"];
            else dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), self.captureQueue,
                ^{ [self prepareTrialOnQueue:generation attempt:attempt + 1]; });
            return;
        }
        // Leave time to switch back to the training ground. Revalidate the
        // binding and object chain at the actual point of use.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), self.captureQueue, ^{
            if (self.trialGeneration != generation || self.trialFinishing) return;
            CCPAppearancePreset preset = CCPAppearancePresets[self.trialPresetIndex];
            TrialState = (CCPCoatTrial){.game = TrialContext.game, .pawn = TrialContext.pawn, .avatar = TrialContext.avatar,
                .slot = preset.slot, .target = preset.target};
            CCPReader reader = {TargetRead, &TrialContext.target};
            CCPTrialResult applied = CCPApplyCoat(reader, TrialTargetWrite, &TrialContext, TrialBindingValid, &TrialState);
            self.trialReport[@"apply"] = TrialResultJSON(applied);
            self.trialReport[@"original_id"] = @(TrialState.original);
            atomic_store(&MenuOwned, TrialState.active);
            atomic_store(&MenuStatus, applied.idConfirmed ? CCPMenuApplied :
                (applied.status == CCPTrialWrongOutfit ? CCPMenuWrongOutfit : CCPMenuFailed));
            self.trialReport[@"status"] = applied.idConfirmed ? @"appearance-applied-awaiting-manual-restore" : @"apply-not-confirmed";
            [self writeTrialReport];
            if (!applied.idConfirmed) {
                [self finishTrialOnQueue:generation reason:@"apply-failed"];
                return;
            }
            // Keep the owned appearance until the user cancels the menu option.
            // Do not auto-restore after a timed trial window.
            dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
        });
}
- (void)restoreTrial {
    if (!self.captureQueue) return;
    uint32_t generation = self.generation;
    if (self.vehicleCapturing) {
        dispatch_async(self.captureQueue, ^{ [self finishVehicleCaptureOnQueue:generation reason:@"manual-stop"]; });
        return;
    }
    uint32_t selected = atomic_load(&MenuPresetIndex);
    if (self.vehicleTrialMode || (atomic_load(&MenuOwned) &&
        selected >= CCP_VEHICLE_PRESET_BASE && selected < CCP_VEHICLE_CAPTURE_INDEX)) {
        dispatch_async(self.captureQueue, ^{
            if (VehicleState.active && !self.capturing) self.trialFinishing = NO;
            [self finishVehicleTrialOnQueue:generation reason:@"manual-restore"];
        });
        return;
    }
    dispatch_async(self.captureQueue, ^{
        if (TrialState.active) self.trialFinishing = NO;
        [self finishTrialOnQueue:generation reason:@"manual-restore"];
    });
}
- (void)startProcessProbe {
    if (self.capturing || atomic_load(&MenuOwned)) return;
    self.capturing = YES;
    atomic_store(&MenuBusy, true);
    atomic_store(&MenuPresetIndex, CCP_PROCESS_PROBE_INDEX);
    atomic_store(&MenuStatus, CCPMenuProcessChecking);
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    dispatch_async(self.captureQueue, ^{
        if (self.generation != generation) return;
        CCPReader local = {LocalRead, NULL};
        uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
        uint32_t quarantineBefore = 0, quarantineAfter = 0;
        uint8_t fatalBefore = 0, fatalAfter = 0;
        BOOL quarantineRead = CCPReadBytes(local, base + 0xC1D2F8, &quarantineBefore, sizeof(quarantineBefore));
        BOOL fatalRead = CCPReadBytes(local, base + 0xC1D2E8, &fatalBefore, sizeof(fatalBefore));
        NSString *result = @"unavailable";
        NSString *status = @"core-identity-mismatch";
        if (CCPIdentity(local, base, CoreUUID) && EntryMatches(base + 0x35F08, ProcessLookupPrefix)) {
            CCPSnapshotFunction snapshot = (CCPSnapshotFunction)SignedEntry(base + 0x64C7C);
            CCPSnapshot ready = {0};
            if (!EntryMatches(base + 0x64C7C, SnapshotPrefix) || !snapshot(&ready) || !ready.words[1]) {
                status = @"core-session-not-ready";
            } else {
                uint64_t lookup = ((CCPProcessLookup)SignedEntry(base + 0x35F08))("ShadowTrackerExtra");
                if (lookup == 0) result = @"zero";
                else if (lookup == ~(uint64_t)0) result = @"not-found";
                else result = @"located";
                status = [result isEqualToString:@"located"] ? @"game-process-visible" : @"game-process-not-visible";
            }
        }
        CCPReadBytes(local, base + 0xC1D2F8, &quarantineAfter, sizeof(quarantineAfter));
        CCPReadBytes(local, base + 0xC1D2E8, &fatalAfter, sizeof(fatalAfter));
        self.trialReport = [@{@"schema_version": @12, @"probe_revision": @12,
            @"mode": @"process-name-visibility", @"game_version": @"1.38.12", @"game_build": @"15915",
            @"process_name": @"ShadowTrackerExtra", @"lookup_result": result, @"status": status,
            @"session_open_attempted": @NO, @"vehicle_appearance_changes_implemented": @NO,
            @"quarantine_before": @(quarantineBefore), @"quarantine_after": @(quarantineAfter),
            @"fatal_before": @(fatalBefore), @"fatal_after": @(fatalAfter),
            @"quarantine_read": @(quarantineRead), @"fatal_read": @(fatalRead),
            @"quarantine_changed": @(quarantineRead && quarantineBefore != quarantineAfter),
            @"ui_location": @"native-menu-below-position"} mutableCopy];
        [self writeTrialReport];
        CCPMenuStatus shown = CCPMenuFailed;
        if ([result isEqualToString:@"located"]) shown = CCPMenuProcessVisible;
        else if ([result isEqualToString:@"not-found"] || [result isEqualToString:@"zero"]) shown = CCPMenuProcessAbsent;
        atomic_store(&MenuStatus, shown);
        dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
    });
}
- (void)startEarlySession {
    // Silent attach-only: never claim "正在打开游戏", never MenuBusy, never block the
    // cosmetics serial queue with a long usleep loop. User-initiated menu "打开游戏"
    // is the only path that may show CCPMenuGameOpening.
    if (self.earlyAttachStarted) return;
    self.earlyAttachStarted = YES;
    self.earlyAttachFinished = NO;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    [self pollEarlyAttachAttempt:0];
}
- (void)pollEarlyAttachAttempt:(unsigned)attempt {
    if (self.earlyAttachFinished) return;
    dispatch_async(self.captureQueue, ^{
        if (self.earlyAttachFinished) return;
        // Yield while a menu operation owns the controller/report.
        if (self.capturing || atomic_load(&MenuOwned)) {
            if (attempt < 3000)
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC), self.captureQueue,
                    ^{ [self pollEarlyAttachAttempt:attempt + 1]; });
            else self.earlyAttachFinished = YES;
            return;
        }
        CCPReader local = {LocalRead, NULL};
        uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
        BOOL identity = CCPIdentity(local, base, CoreUUID);
        BOOL prefixes = identity && EntryMatches(base + 0x35F08, ProcessLookupPrefix) &&
            EntryMatches(base + 0x35E60, ProcessLookupByPidPrefix) &&
            EntryMatches(base + 0x64C7C, SnapshotPrefix);
        uint8_t seenEntry[32] = {0};
        BOOL entry = CCPReadBytes(local, base + 0x3A3E8, seenEntry, sizeof(seenEntry)) &&
            !memcmp(seenEntry, RemoteSymbolPrefix, sizeof(seenEntry));
        NSString *status = @"early-window-missed";
        NSString *waitBlocker = @"not-started";
        NSString *lookup = @"unavailable";
        BOOL pidMatches = NO;
        uint32_t quarantine = 0, pid = 0, port = 0, targetPid = 0, remotePort = 0;
        uint8_t fatal = 0, remoteReady = 0;
        BOOL startRead = CCPReadBytes(local, base + 0xC1D2F8, &quarantine, sizeof(quarantine));
        CCPReadBytes(local, base + 0xC1D2E8, &fatal, sizeof(fatal));
        if (!identity) status = @"core-identity-mismatch";
        else if (!prefixes) status = @"core-entry-mismatch";
        else if (!startRead || quarantine != 0 || fatal != 0) {
            waitBlocker = @"recorded-fault";
            status = @"open-blocked-by-recorded-fault";
        } else {
            BOOL pidRead = CCPReadBytes(local, base + 0xC53460, &pid, sizeof(pid));
            BOOL portRead = CCPReadBytes(local, base + 0xC534B0, &port, sizeof(port));
            if (!portRead || port == 0 || !pidRead || pid == 0) {
                waitBlocker = @"waiting-for-published-game-session";
                status = @"switch-to-game";
            } else {
                uint64_t named = ((CCPProcessLookup)SignedEntry(base + 0x35F08))("ShadowTrackerExtra");
                uint64_t published = ((CCPProcessLookupByPid)SignedEntry(base + 0x35E60))(pid);
                if (named == 0) lookup = @"zero";
                else if (named == ~(uint64_t)0) lookup = @"not-found";
                else lookup = @"located";
                if (named == 0 || named == ~(uint64_t)0 || published == 0 ||
                    published == ~(uint64_t)0 || named != published || !entry) {
                    waitBlocker = @"remote-port-in-progress";
                    status = @"springboard-port-held";
                } else {
                    pidMatches = YES;
                    waitBlocker = @"game-session-published";
                    status = @"existing-game-session";
                }
            }
        }
        BOOL retry = !pidMatches &&
            ([status isEqualToString:@"switch-to-game"] ||
             [status isEqualToString:@"springboard-port-held"]) &&
            attempt < 3000;
        if (retry) {
            if ((attempt % 25) == 0 &&
                (!self.trialReport || [self.trialReport[@"mode"] isEqualToString:@"early-game-session"])) {
                self.trialReport = [@{@"schema_version": @42, @"probe_revision": @42,
                    @"mode": @"early-game-session", @"status": status,
                    @"wait_blocker": waitBlocker, @"wait_ticks": @(attempt),
                    @"open_result": @"attach-only-never-remote-open",
                    @"call_result": @"skipped-open-time-remote-call",
                    @"session_open_attempted": @NO, @"menu_status_policy": @"silent-attach-keep-idle",
                    @"game_version": @"1.38.12", @"game_build": @"15915",
                    @"process_name": @"ShadowTrackerExtra",
                    @"vehicle_appearance_changes_implemented": @NO,
                    @"ui_location": @"native-menu-below-position"} mutableCopy];
                [self writeTrialReport];
            }
            int64_t delay = [status isEqualToString:@"springboard-port-held"]
                ? 20 * NSEC_PER_MSEC : 200 * NSEC_PER_MSEC;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay), self.captureQueue,
                ^{ [self pollEarlyAttachAttempt:attempt + 1]; });
            return;
        }
        self.earlyAttachFinished = YES;
        CCPReadBytes(local, base + 0xC53460, &targetPid, sizeof(targetPid));
        CCPReadBytes(local, base + 0xBD4C5C, &remoteReady, sizeof(remoteReady));
        CCPReadBytes(local, base + 0xC534B0, &remotePort, sizeof(remotePort));
        NSDictionary *gameThread = pidMatches ? RunGameThreadProbe(base)
            : @{@"attempted": @NO, @"status": @"skipped-pid-mismatch"};
        if (pidMatches && [gameThread[@"status"] isEqualToString:@"game-thread-probe-ready"])
            status = @"existing-game-session-thread-ready";
        else if (pidMatches && [gameThread[@"status"] isEqualToString:@"game-thread-probe-partial"])
            status = @"existing-game-session-thread-partial";
        if (!self.capturing && !atomic_load(&MenuOwned) &&
            (!self.trialReport || [self.trialReport[@"mode"] isEqualToString:@"early-game-session"])) {
            self.trialReport = [@{@"schema_version": @42, @"probe_revision": @42,
                @"mode": @"early-game-session", @"game_version": @"1.38.12", @"game_build": @"15915",
                @"process_name": @"ShadowTrackerExtra", @"lookup_result": lookup,
                @"open_result": @"attach-only-never-remote-open",
                @"status": status, @"session_open_attempted": @NO, @"wait_ticks": @(attempt),
                @"wait_blocker": waitBlocker,
                @"app_state_at_open": UIApplication.sharedApplication.applicationState == UIApplicationStateBackground
                    ? @"background" : @"active",
                @"symbol": @"getpid", @"call_result": @"skipped-open-time-remote-call",
                @"exec_call_attempted": @NO, @"entry_matches": @(entry),
                @"pid_matches_published": @(pidMatches), @"game_thread_probe": gameThread,
                @"menu_status_policy": @"silent-attach-keep-idle",
                @"vehicle_appearance_changes_implemented": @NO,
                @"target_pid_published": @(targetPid != 0),
                @"remote_port_present": @(remotePort != 0), @"remote_ready": @(remoteReady != 0),
                @"quarantine_at_start": @(quarantine), @"quarantine_start_read": @(startRead),
                @"fatal_at_start": @(fatal), @"quarantine_reason": QuarantineReason(base),
                @"ui_location": @"native-menu-below-position"} mutableCopy];
            [self writeTrialReport];
        }
        // Keep title at "选择外观" unless attach succeeded and the menu is still idle.
        if (pidMatches) {
            CCPMenuStatus current = (CCPMenuStatus)atomic_load(&MenuStatus);
            if (current == CCPMenuIdle) atomic_store(&MenuStatus, CCPMenuExecMatched);
        }
    });
}
- (void)startGameOpen {
    if (self.capturing || atomic_load(&MenuOwned)) return;
    self.capturing = YES;
    atomic_store(&MenuBusy, true);
    atomic_store(&MenuPresetIndex, CCP_GAME_OPEN_INDEX);
    atomic_store(&MenuStatus, CCPMenuGameOpening);
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core game open attempt"
        expirationHandler:^{}];
    dispatch_async(self.captureQueue, ^{
        if (self.generation != generation) return;
        CCPReader local = {LocalRead, NULL};
        uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
        uint32_t quarantineBefore = 0, quarantineAfter = 0, targetPid = 0;
        uint8_t fatalBefore = 0, fatalAfter = 0, remoteReady = 0;
        BOOL quarantineRead = CCPReadBytes(local, base + 0xC1D2F8, &quarantineBefore, sizeof(quarantineBefore));
        BOOL fatalRead = CCPReadBytes(local, base + 0xC1D2E8, &fatalBefore, sizeof(fatalBefore));
        NSString *lookup = @"unavailable";
        NSString *openResult = @"not-called";
        NSString *status = @"core-identity-mismatch";
        BOOL called = NO;
        if (CCPIdentity(local, base, CoreUUID) && EntryMatches(base + 0x35F08, ProcessLookupPrefix) &&
            EntryMatches(base + 0x64C7C, SnapshotPrefix) && EntryMatches(base + 0x3A7D4, RemoteOpenPrefix)) {
            CCPSnapshotFunction snapshot = (CCPSnapshotFunction)SignedEntry(base + 0x64C7C);
            CCPSnapshot ready = {0};
            if (!snapshot(&ready) || !ready.words[1]) status = @"core-session-not-ready";
            else if (!quarantineRead || quarantineBefore != 0 || !fatalRead || fatalBefore != 0) status = @"open-blocked-by-recorded-fault";
            else {
                uint64_t found = ((CCPProcessLookup)SignedEntry(base + 0x35F08))("ShadowTrackerExtra");
                if (found == 0) lookup = @"zero";
                else if (found == ~(uint64_t)0) lookup = @"not-found";
                else lookup = @"located";
                if (![lookup isEqualToString:@"located"]) status = @"game-process-not-visible";
                else {
                    called = YES;
                    uint64_t returned = ((CCPRemoteOpen)SignedEntry(base + 0x3A7D4))("ShadowTrackerExtra", 0);
                    uint32_t code = (uint32_t)returned;
                    if (code == 0) openResult = @"returned-zero";
                    else if (code == 0xFFFFFFFFu) openResult = @"returned-failure";
                    else openResult = @"returned-other";
                    status = @"game-open-attempt-finished";
                }
            }
        }
        CCPReadBytes(local, base + 0xC1D2F8, &quarantineAfter, sizeof(quarantineAfter));
        CCPReadBytes(local, base + 0xC1D2E8, &fatalAfter, sizeof(fatalAfter));
        BOOL pidRead = CCPReadBytes(local, base + 0xC53460, &targetPid, sizeof(targetPid));
        BOOL readyRead = CCPReadBytes(local, base + 0xBD4C5C, &remoteReady, sizeof(remoteReady));
        self.trialReport = [@{@"schema_version": @13, @"probe_revision": @13,
            @"mode": @"game-process-open-attempt", @"game_version": @"1.38.12", @"game_build": @"15915",
            @"process_name": @"ShadowTrackerExtra", @"lookup_result": lookup, @"open_result": openResult,
            @"status": status, @"session_open_attempted": @(called),
            @"vehicle_appearance_changes_implemented": @NO, @"target_pid_published": @(pidRead && targetPid != 0),
            @"remote_ready": @(readyRead && remoteReady != 0),
            @"quarantine_before": @(quarantineBefore), @"quarantine_after": @(quarantineAfter),
            @"quarantine_reason": QuarantineReason(base),
            @"fatal_before": @(fatalBefore), @"fatal_after": @(fatalAfter),
            @"quarantine_read": @(quarantineRead), @"fatal_read": @(fatalRead),
            @"quarantine_changed": @(quarantineRead && quarantineBefore != quarantineAfter),
            @"ui_location": @"native-menu-below-position"} mutableCopy];
        [self writeTrialReport];
        CCPMenuStatus shown = CCPMenuGameOpenReturned;
        if ([status isEqualToString:@"open-blocked-by-recorded-fault"]) shown = CCPMenuGameOpenBlocked;
        else if (!called && ![status isEqualToString:@"game-open-attempt-finished"]) shown = CCPMenuFailed;
        atomic_store(&MenuStatus, shown);
        dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
    });
}
- (void)startRemoteExecCheck {
    if (self.capturing || atomic_load(&MenuOwned)) return;
    self.capturing = YES;
    atomic_store(&MenuBusy, true);
    atomic_store(&MenuPresetIndex, CCP_REMOTE_EXEC_INDEX);
    atomic_store(&MenuStatus, CCPMenuExecChecking);
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core remote getpid check"
        expirationHandler:^{}];
    dispatch_async(self.captureQueue, ^{
        if (self.generation != generation) return;
        CCPReader local = {LocalRead, NULL};
        uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
        uint32_t quarantineBefore = 0, quarantineAfter = 0, targetPid = 0, remotePort = 0;
        uint8_t fatalBefore = 0, fatalAfter = 0, remoteReady = 0, stableMode = 0;
        BOOL quarantineRead = CCPReadBytes(local, base + 0xC1D2F8, &quarantineBefore, sizeof(quarantineBefore));
        BOOL fatalRead = CCPReadBytes(local, base + 0xC1D2E8, &fatalBefore, sizeof(fatalBefore));
        BOOL pidRead = CCPReadBytes(local, base + 0xC53460, &targetPid, sizeof(targetPid));
        BOOL readyRead = CCPReadBytes(local, base + 0xBD4C5C, &remoteReady, sizeof(remoteReady));
        BOOL portRead = CCPReadBytes(local, base + 0xC534B0, &remotePort, sizeof(remotePort));
        BOOL stableRead = CCPReadBytes(local, base + 0xC53464, &stableMode, sizeof(stableMode));
        NSString *callResult = @"not-called";
        NSString *status = @"core-identity-mismatch";
        BOOL called = NO, pidMatches = NO;
        BOOL identity = CCPIdentity(local, base, CoreUUID);
        uint8_t seenEntry[32] = {0};
        BOOL entryRead = CCPReadBytes(local, base + 0x3A3E8, seenEntry, sizeof(seenEntry));
        BOOL entry = entryRead && !memcmp(seenEntry, RemoteSymbolPrefix, sizeof(seenEntry));
        NSMutableString *seenHex = [NSMutableString stringWithCapacity:64];
        if (entryRead) for (unsigned i = 0; i < sizeof(seenEntry); i++) [seenHex appendFormat:@"%02x", seenEntry[i]];
        if (!identity) status = @"core-identity-mismatch";
        else if (!entryRead) status = @"remote-symbol-entry-unreadable";
        else if (!entry) status = @"remote-symbol-entry-mismatch";
        else if (!quarantineRead || quarantineBefore != 0 || !fatalRead || fatalBefore != 0 ||
                 !readyRead || remoteReady == 0 || !pidRead || targetPid == 0 || !portRead || remotePort == 0) {
            status = @"remote-session-not-ready";
        } else {
            called = YES;
            uint64_t output = 0;
            uint64_t returned = ((CCPRemoteSymbolCall)SignedEntry(base + 0x3A3E8))("getpid", 0, 0, &output);
            uint32_t code = (uint32_t)returned;
            if (code == 1) {
                callResult = @"returned-success";
                pidMatches = output == (uint64_t)targetPid;
            } else if (code == 0) callResult = @"returned-zero";
            else callResult = @"returned-other";
            status = @"remote-getpid-finished";
        }
        CCPReadBytes(local, base + 0xC1D2F8, &quarantineAfter, sizeof(quarantineAfter));
        CCPReadBytes(local, base + 0xC1D2E8, &fatalAfter, sizeof(fatalAfter));
        uint8_t remoteReadyAfter = 0;
        BOOL readyAfterRead = CCPReadBytes(local, base + 0xBD4C5C, &remoteReadyAfter, sizeof(remoteReadyAfter));
        self.trialReport = [@{@"schema_version": @16, @"probe_revision": @16,
            @"mode": @"remote-getpid-check", @"game_version": @"1.38.12", @"game_build": @"15915",
            @"symbol": @"getpid", @"call_result": callResult, @"status": status,
            @"entry_matches": @(entry), @"entry_prefix": seenHex,
            @"session_open_attempted": @NO, @"exec_call_attempted": @(called),
            @"vehicle_appearance_changes_implemented": @NO,
            @"target_pid_published": @(pidRead && targetPid != 0),
            @"remote_port_present": @(portRead && remotePort != 0),
            @"remote_ready": @(readyRead && remoteReady != 0),
            @"remote_ready_after": @(readyAfterRead && remoteReadyAfter != 0),
            @"stable_mode_set": @(stableRead && (stableMode & 1) != 0),
            @"pid_matches_published": @(pidMatches),
            @"quarantine_before": @(quarantineBefore), @"quarantine_after": @(quarantineAfter),
            @"quarantine_reason": QuarantineReason(base),
            @"fatal_before": @(fatalBefore), @"fatal_after": @(fatalAfter),
            @"quarantine_read": @(quarantineRead), @"fatal_read": @(fatalRead),
            @"quarantine_changed": @(quarantineRead && quarantineBefore != quarantineAfter),
            @"ui_location": @"native-menu-below-position"} mutableCopy];
        [self writeTrialReport];
        CCPMenuStatus shown = CCPMenuFailed;
        if ([status isEqualToString:@"remote-session-not-ready"]) shown = CCPMenuExecBlocked;
        else if (pidMatches) shown = CCPMenuExecMatched;
        else if (called && [callResult isEqualToString:@"returned-success"]) shown = CCPMenuExecMismatch;
        atomic_store(&MenuStatus, shown);
        dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
    });
}
- (void)selectPreset:(uint32_t)index enabled:(BOOL)enabled {
    if (index >= CCP_VEHICLE_PRESET_BASE && index < CCP_VEHICLE_CAPTURE_INDEX) {
        if (!enabled) {
            if (index != atomic_load(&MenuPresetIndex)) return;
            self.pendingPresetIndex = NSNotFound;
            [self restoreTrial];
        } else if (self.capturing || atomic_load(&MenuOwned)) {
            self.pendingPresetIndex = index;
            [self restoreTrial];
        } else {
            self.vehiclePresetIndex = index - CCP_VEHICLE_PRESET_BASE;
            self.pendingPresetIndex = NSNotFound;
            [self startVehicleTrial:self.vehiclePresetIndex];
        }
        return;
    }
    if (index == CCP_REMOTE_EXEC_INDEX) {
        if (enabled) [self startRemoteExecCheck];
        return;
    }
    if (index == CCP_GAME_OPEN_INDEX) {
        if (enabled) [self startGameOpen];
        return;
    }
    if (index == CCP_PROCESS_PROBE_INDEX) {
        if (enabled) [self startProcessProbe];
        return;
    }
    if (index == CCP_VEHICLE_CAPTURE_INDEX) {
        if (enabled) [self startVehicleCapture];
        else if (self.vehicleCapturing) { self.pendingPresetIndex = NSNotFound; [self restoreTrial]; }
        return;
    }
    if (index >= CCP_PRESET_COUNT) return;
    if (!enabled) {
        if (index != self.presetIndex) return;
        self.pendingPresetIndex = NSNotFound;
        [self restoreTrial];
    } else if (self.capturing || atomic_load(&MenuOwned)) {
        self.pendingPresetIndex = index;
        [self restoreTrial];
    } else {
        self.presetIndex = index;
        self.pendingPresetIndex = NSNotFound;
        [self startTrial];
    }
}
- (void)finishTrialOnQueue:(uint32_t)generation reason:(NSString *)reason {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    self.trialFinishing = YES;
    if (self.trialTimer) { dispatch_source_cancel(self.trialTimer); self.trialTimer = nil; }
    CCPReader reader = {TargetRead, &TrialContext.target};
    CCPTrialResult restored = CCPRestoreCoat(reader, TrialTargetWrite, &TrialContext, TrialBindingValid, &TrialState);
    self.trialReport[@"restore"] = TrialResultJSON(restored);
    atomic_store(&MenuOwned, TrialState.active);
    if (restored.idConfirmed) atomic_store(&MenuStatus, CCPMenuRestored);
    else if (TrialState.active) atomic_store(&MenuStatus, CCPMenuRestorePending);
    else if (![reason isEqualToString:@"apply-failed"]) atomic_store(&MenuStatus,
        [reason isEqualToString:@"readiness-timeout"] ? CCPMenuFailed : CCPMenuIdle);
    self.trialReport[@"finish_reason"] = reason;
    [self writeTrialReport];
    dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
}
- (void)finishGeneration:(uint32_t)generation {
    if (self.generation != generation) return;
    self.capturing = NO;
    self.vehicleCapturing = NO;
    self.vehicleTrialMode = NO;
    atomic_store(&MenuBusy, false);
    if (self.backgroundTask != UIBackgroundTaskInvalid) {
        [UIApplication.sharedApplication endBackgroundTask:self.backgroundTask];
        self.backgroundTask = UIBackgroundTaskInvalid;
    }
    if (!atomic_load(&MenuOwned) && self.pendingPresetIndex != NSNotFound) {
        NSUInteger pending = self.pendingPresetIndex;
        self.pendingPresetIndex = NSNotFound;
        if (pending >= CCP_VEHICLE_PRESET_BASE && pending < CCP_VEHICLE_CAPTURE_INDEX) {
            self.vehiclePresetIndex = pending - CCP_VEHICLE_PRESET_BASE;
            [self startVehicleTrial:self.vehiclePresetIndex];
        } else if (pending < CCP_PRESET_COUNT) {
            self.presetIndex = pending;
            [self startTrial];
        }
    }
}
- (void)startVehicleCapture {
    if (self.capturing || atomic_load(&MenuOwned)) return;
    self.capturing = YES;
    self.vehicleCapturing = YES;
    self.pendingPresetIndex = NSNotFound;
    atomic_store(&MenuBusy, true);
    atomic_store(&MenuPresetIndex, CCP_VEHICLE_CAPTURE_INDEX);
    atomic_store(&MenuStatus, CCPMenuVehicleWaiting);
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core vehicle appearance capture"
        expirationHandler:^{
            if (self.generation != generation || !self.capturing) return;
            dispatch_async(self.captureQueue, ^{ [self finishVehicleCaptureOnQueue:generation reason:@"background-expiration"]; });
        }];
    dispatch_async(self.captureQueue, ^{
        self.trialGeneration = generation;
        self.trialFinishing = NO;
        self.samples = [NSMutableArray array];
        self.vehicleAttempts = [NSMutableArray array];
        self.trialReport = [@{@"schema_version": @12, @"probe_revision": @12, @"mode": @"vehicle-entity-resource-diagnostics",
            @"game_version": @"1.38.12", @"game_build": @"15915",
            @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
            @"vehicle_writes_implemented": @NO, @"vehicle_appearance_changes_implemented": @NO,
            @"vehicle_trial_available": @NO, @"target_resource_availability": @"not-checked",
            @"diagnostic_scope": @"current-vehicle-entity-and-loaded-handle",
            @"runtime_layout_validated": @NO, @"ui_location": @"native-menu-below-position"} mutableCopy];
        [self captureVehicleOnQueue:generation attempt:0];
    });
}
- (void)captureVehicleOnQueue:(uint32_t)generation attempt:(unsigned)attempt {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    NSDictionary *sample = Sample(NO, NULL, YES);
    NSDictionary *vehicle = sample[@"vehicle_candidate"] ?: @{@"status": sample[@"status"] ?: @"core-session-unavailable"};
    self.trialReport[@"latest_vehicle"] = vehicle;
    self.trialReport[@"capture_attempts"] = @(attempt + 1);
    if (sample[@"core_context"]) self.trialReport[@"core_context"] = sample[@"core_context"];
    NSMutableDictionary *entry = [@{@"attempt": @(attempt + 1), @"status": vehicle[@"status"]} mutableCopy];
    if (vehicle[@"path_diagnostics"]) entry[@"path_diagnostics"] = vehicle[@"path_diagnostics"];
    [self.vehicleAttempts addObject:entry];
    if (self.vehicleAttempts.count > 12) [self.vehicleAttempts removeObjectAtIndex:0];
    self.trialReport[@"attempt_history"] = self.vehicleAttempts.copy;
    if ([vehicle[@"sync_array_stable"] boolValue]) [self.samples addObject:vehicle];
    self.trialReport[@"samples"] = self.samples.copy;
    self.trialReport[@"status"] = self.samples.count ? @"sampling-current-vehicle" : @"waiting-for-current-vehicle";
    [self writeTrialReport];
    NSString *vehicleStatus = vehicle[@"status"] ?: @"";
    if ([vehicleStatus hasPrefix:@"image-"] && attempt >= 1) {
        self.trialReport[@"game_identity_status"] = vehicleStatus;
        [self finishVehicleCaptureOnQueue:generation reason:@"game-image-unconfirmed"];
        return;
    }
    if (self.samples.count >= 8 || attempt >= 60) {
        [self finishVehicleCaptureOnQueue:generation reason:self.samples.count >= 8 ? @"capture-complete" : @"readiness-timeout"];
    } else dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), self.captureQueue,
        ^{ [self captureVehicleOnQueue:generation attempt:attempt + 1]; });
}
- (void)finishVehicleCaptureOnQueue:(uint32_t)generation reason:(NSString *)reason {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    self.trialFinishing = YES;
    self.trialReport[@"finish_reason"] = reason;
    self.trialReport[@"status"] = self.samples.count ? @"vehicle-candidates-saved" : @"vehicle-not-sampled";
    atomic_store(&MenuStatus, self.samples.count ? CCPMenuVehicleCaptured : CCPMenuFailed);
    [self writeTrialReport];
    dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
}
- (void)startVehicleTrial:(NSUInteger)presetIndex {
    if (self.capturing) return;
    if (presetIndex >= CCP_VEHICLE_PRESET_COUNT) return;
    uint32_t menuIndex = CCP_VEHICLE_PRESET_BASE + (uint32_t)presetIndex;
    // Already applied — keep originalCounter; user must 恢复原车 first.
    if (atomic_load(&MenuOwned) && atomic_load(&MenuPresetIndex) == menuIndex &&
        VehicleState.active) {
        atomic_store(&MenuStatus, CCPMenuVehicleTrialApplied);
        return;
    }
    self.vehiclePresetIndex = presetIndex;
    self.capturing = YES; self.vehicleCapturing = NO; self.vehicleTrialMode = YES;
    atomic_store(&MenuBusy, true);
    atomic_store(&MenuPresetIndex, menuIndex);
    atomic_store(&MenuStatus, CCPMenuVehicleTrialWaiting);
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core vehicle puton"
        expirationHandler:^{
            if (self.generation != generation || !self.capturing) return;
            dispatch_async(self.captureQueue, ^{ [self finishVehicleTrialOnQueue:generation reason:@"background-expiration"]; });
        }];
    dispatch_async(self.captureQueue, ^{
        self.trialGeneration = generation; self.trialFinishing = NO;
        if (TrialState.active) {
            atomic_store(&MenuStatus, CCPMenuRestorePending);
            dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
            return;
        }
        VehicleContext = (CCPResolvedTarget){0};
        VehicleState = (CCPVehicleTrial){0};
        CCPVehiclePreset preset = CCPVehiclePresets[self.vehiclePresetIndex];
        self.trialReport = [@{@"schema_version": @41, @"probe_revision": @41,
            @"mode": @"explicit-menu-puton-enqueue", @"game_version": @"1.38.12", @"game_build": @"15915",
            @"preset": [NSString stringWithUTF8String:preset.targetLabel],
            @"source_item_id": @(preset.sourceItem),
            @"source_resource_id": @(preset.sourceResource),
            @"target_item_id": @(preset.targetItem),
            @"target_resource_id": @(preset.targetResource),
            @"refresh_strategy": @"create-and-dispatch-puton-split-rx-rw",
            @"vehicle_appearance_changes_implemented": @YES, @"visual_effect_verified": @NO,
            @"auto_enqueue_on_session_open": @NO, @"ui_location": @"native-menu-below-position"} mutableCopy];
        [self prepareVehicleTrialOnQueue:generation attempt:0];
    });
}
- (void)prepareVehicleTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    VehicleContext = (CCPResolvedTarget){0};
    NSDictionary *before = Sample(NO, &VehicleContext, YES);
    self.trialReport[@"before"] = before;
    uintptr_t base = VehicleContext.coreBase ? VehicleContext.coreBase : (uintptr_t)_dyld_get_image_header(0);
    BOOL entries = EntryMatches(base + 0x39A80, RemoteCallByAddressPrefix) &&
        EntryMatches(base + 0x620B8, WritePrefix);
    if (!VehicleContext.game || !VehicleContext.avatar || !VehicleContext.vehicleUser ||
        !VehicleContext.vehicle ||
        !VehicleContext.target.write || !TrialBindingValid(&VehicleContext) || !entries) {
        NSDictionary *vehicle = before[@"vehicle_candidate"];
        BOOL noCar = [vehicle[@"status"] isEqualToString:@"current-vehicle-unavailable"] ||
            [before[@"status"] isEqualToString:@"current-vehicle-unavailable"];
        self.trialReport[@"status"] = noCar ? @"waiting-for-current-vehicle" : @"waiting-for-core-or-vehicle";
        self.trialReport[@"waiting_attempts"] = @(attempt + 1);
        [self writeTrialReport];
        if (attempt >= 60) {
            atomic_store(&MenuStatus, noCar ? CCPMenuVehicleWrongCar : CCPMenuFailed);
            [self finishVehicleTrialOnQueue:generation reason:@"readiness-timeout"];
        } else dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), self.captureQueue,
            ^{ [self prepareVehicleTrialOnQueue:generation attempt:attempt + 1]; });
        return;
    }
    NSDictionary *vehicle = before[@"vehicle_candidate"];
    uint32_t skinBefore = [vehicle[@"vehicle_skin_id"] unsignedIntValue];
    CCPVehiclePreset preset = CCPVehiclePresets[self.vehiclePresetIndex];
    if (skinBefore != preset.sourceItem) {
        self.trialReport[@"status"] = @"source-vehicle-skin-mismatch";
        self.trialReport[@"observed_source_item_id"] = @(skinBefore);
        atomic_store(&MenuStatus, CCPMenuVehicleWrongCar);
        [self writeTrialReport];
        [self finishVehicleTrialOnQueue:generation reason:@"source-incompatible"];
        return;
    }
    CCPVehicleSample source = {0};
    if (!VehicleContextSample(&VehicleContext, preset.sourceItem, true, &source)) {
        self.trialReport[@"status"] = @"waiting-for-stable-source-render-state";
        self.trialReport[@"waiting_attempts"] = @(attempt + 1);
        [self writeTrialReport];
        if (attempt >= 60) {
            atomic_store(&MenuStatus, CCPMenuFailed);
            [self finishVehicleTrialOnQueue:generation reason:@"source-readiness-timeout"];
        } else dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), self.captureQueue,
            ^{ [self prepareVehicleTrialOnQueue:generation attempt:attempt + 1]; });
        return;
    }
    VehicleState = (CCPVehicleTrial){
        .game = VehicleContext.game, .pawn = VehicleContext.pawn, .controller = VehicleContext.controller,
        .user = VehicleContext.vehicleUser, .vehicle = VehicleContext.vehicle, .avatar = VehicleContext.avatar,
        .originalCounter = skinBefore, .active = YES, .prepared = YES
    };
    CCPRemoteCallByAddress remoteCall = (CCPRemoteCallByAddress)SignedEntry(base + 0x39A80);
    CCPVehicleEnqueueResult enqueued = CCPEnqueuePutOnByResID(
        VehicleContext.target.pid, VehicleContext.game, VehicleContext.avatar, VehicleContext.controller,
        VehicleContext.vehicleComponentOffset, VehicleContext.vehicleComponentHeap,
        VehicleContext.vehicleComponentBitmapWord, VehicleContext.vehicleComponentBit,
        VehicleContext.vehicleUser, VehicleContext.vehicle, skinBefore, 0,
        preset.targetItem, preset.targetResource, false,
        VehicleContext.target.read, VehicleContext.target.write, remoteCall);
    VehicleState.putOnTaskCompleted = enqueued.taskCompleted;
    if (enqueued.putOnAttempted && !enqueued.mappingReleased &&
        enqueued.codePage && enqueued.dataPage && enqueued.mappingLength) {
        VehicleState.pendingApplyMapping = enqueued.codePage;
        VehicleState.pendingApplyDataPage = enqueued.dataPage;
        VehicleState.pendingApplyMappingLength = enqueued.mappingLength;
    }
    self.trialReport[@"enqueue"] = @{
        @"status": [NSString stringWithUTF8String:enqueued.status ?: "unknown"],
        @"remote_status": @(enqueued.remoteStatus),
        @"put_on_attempted": @(enqueued.putOnAttempted),
        @"target_item_id": @(enqueued.targetItem),
        @"target_resource_id": @(enqueued.targetResource),
        @"skin_before": @(enqueued.skinBefore),
        @"skin_after": @(enqueued.skinAfter),
        @"skin_id_changed": @(enqueued.skinChanged),
        @"task_completed": @(enqueued.taskCompleted),
        @"task_rejected_context": @(enqueued.taskRejectedContext),
        @"mapping_released": @(enqueued.mappingReleased),
        @"mapping_length": @(enqueued.mappingLength),
        @"code_page": [NSString stringWithFormat:@"0x%llx", (unsigned long long)enqueued.codePage],
        @"data_page": [NSString stringWithFormat:@"0x%llx", (unsigned long long)enqueued.dataPage]
    };
    BOOL stateObserved = CCPVehicleTargetObserved(&enqueued);
    CCPVehicleSample rendered = {0};
    BOOL confirmed = NO;
    if (stateObserved) {
        for (unsigned settle = 0; settle < 20 && !confirmed; settle++) {
            confirmed = VehicleContextSample(&VehicleContext, preset.targetItem, true, &rendered);
            if (!confirmed) usleep(250000);
        }
    }
    BOOL dispatched = CCPVehiclePutOnDispatched(&enqueued);
    self.trialReport[@"target_state_observed"] = @(stateObserved);
    self.trialReport[@"render_entity_observed"] = @(confirmed);
    self.trialReport[@"loaded_handle_item_id"] = @(rendered.entity.loadedID);
    self.trialReport[@"master_sync_item_id"] = @(rendered.count ? rendered.items[0].item : 0);
    self.trialReport[@"mesh_path_present"] = @(rendered.entity.meshPathPresent);
    self.trialReport[@"state_change_confirmed"] = @(stateObserved);
    self.trialReport[@"appearance_ready_confirmed"] = @(confirmed);
    self.trialReport[@"status"] = confirmed ? @"target-render-state-observed" :
        (stateObserved ? @"target-item-observed-render-not-ready" :
        (dispatched ? @"puton-dispatched-target-not-observed" : @"puton-enqueue-failed"));
    atomic_store(&MenuOwned, dispatched);
    atomic_store(&MenuStatus, confirmed ? CCPMenuVehicleTrialApplied :
        (dispatched ? CCPMenuRestorePending : CCPMenuFailed));
    [self writeTrialReport];
    if (!dispatched) VehicleState.active = NO;
    dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
}
- (void)finishVehicleTrialOnQueue:(uint32_t)generation reason:(NSString *)reason {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    self.trialFinishing = YES;
    if (self.trialTimer) { dispatch_source_cancel(self.trialTimer); self.trialTimer = nil; }
    self.trialReport[@"finish_reason"] = reason;
    uintptr_t base = VehicleContext.coreBase ? VehicleContext.coreBase : (uintptr_t)_dyld_get_image_header(0);
    BOOL restored = NO;
    BOOL abandonedForExternalChange = NO;
    uint32_t original = VehicleState.originalCounter;
    if (VehicleState.active && original != 0 && VehicleContext.game && VehicleContext.avatar &&
        VehicleContext.vehicleUser && VehicleContext.vehicle && VehicleContext.target.write &&
        EntryMatches(base + 0x39A80, RemoteCallByAddressPrefix)) {
        atomic_store(&MenuStatus, CCPMenuVehicleRestoring);
        CCPVehiclePreset preset = CCPVehiclePresets[self.vehiclePresetIndex];
        CCPRemoteCallByAddress remoteCall = (CCPRemoteCallByAddress)SignedEntry(base + 0x39A80);
        CCPVehicleMappingRelease applyRelease = CCPReleaseCompletedVehicleMapping(
            VehicleContext.target.pid, VehicleState.pendingApplyMapping,
            VehicleState.pendingApplyDataPage, VehicleState.pendingApplyMappingLength,
            VehicleContext.target.read, remoteCall);
        if (applyRelease.completionValue == 1) VehicleState.putOnTaskCompleted = true;
        if (applyRelease.mappingReleased) {
            VehicleState.pendingApplyMapping = 0;
            VehicleState.pendingApplyDataPage = 0;
            VehicleState.pendingApplyMappingLength = 0;
        }
        CCPVehicleSample current = {0};
        BOOL restoreBindingValid = TrialBindingValid(&VehicleContext);
        if (!restoreBindingValid || !VehicleContextSample(&VehicleContext, 0, false, &current)) {
            BOOL releaseOwnership = !restoreBindingValid ||
                VehicleState.putOnTaskCompleted ||
                applyRelease.completionValue == 1 || applyRelease.completionValue == 2;
            if (releaseOwnership) VehicleState.active = NO;
            self.trialReport[@"restore"] = @{@"status": @"restore-context-or-state-unconfirmed",
                @"original_skin_id": @(original), @"state_observed": @NO,
                @"render_entity_observed": @NO,
                @"binding_valid": @(restoreBindingValid),
                @"ownership_released": @(releaseOwnership)};
            atomic_store(&MenuOwned, VehicleState.active);
            atomic_store(&MenuStatus, VehicleState.active ? CCPMenuRestorePending : CCPMenuIdle);
            self.trialReport[@"status"] = releaseOwnership ?
                @"changed-context-abandoned-ownership-released" :
                @"restore-not-confirmed-change-still-owned";
            [self writeTrialReport];
            dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
            return;
        }
        uint32_t skinNow = current.skin;
        CCPVehicleEnqueueResult enqueued = {0};
        CCPVehicleMappingRelease restoreRelease = CCPReleaseCompletedVehicleMapping(
            VehicleContext.target.pid, VehicleState.pendingRestoreMapping,
            VehicleState.pendingRestoreDataPage, VehicleState.pendingRestoreMappingLength,
            VehicleContext.target.read, remoteCall);
        BOOL restoreFinished = restoreRelease.completionValue == 1;
        BOOL restoreStillPending = VehicleState.pendingRestoreMapping &&
            restoreRelease.completionValue == 0;
        BOOL restoreCleanupPending = VehicleState.pendingRestoreMapping &&
            restoreRelease.completionValue == 2 && !restoreRelease.mappingReleased;
        if (restoreRelease.mappingReleased) {
            VehicleState.pendingRestoreMapping = 0;
            VehicleState.pendingRestoreDataPage = 0;
            VehicleState.pendingRestoreMappingLength = 0;
        }
        BOOL conflict = skinNow != preset.targetItem && skinNow != original;
        if (conflict) {
            enqueued.status = "restore-conflict-third-party-skin";
            enqueued.skinBefore = skinNow;
            enqueued.skinAfter = skinNow;
            enqueued.targetItem = original;
            enqueued.targetResource = preset.sourceResource;
        } else if (restoreStillPending) {
            enqueued.status = "task-not-completed";
            enqueued.putOnAttempted = true;
            enqueued.skinBefore = skinNow;
            enqueued.skinAfter = skinNow;
            enqueued.targetItem = original;
            enqueued.targetResource = preset.sourceResource;
        } else if (restoreCleanupPending) {
            enqueued.status = "completed-task-mapping-release-pending";
            enqueued.taskRejectedContext = true;
            enqueued.skinBefore = skinNow;
            enqueued.skinAfter = skinNow;
            enqueued.targetItem = original;
            enqueued.targetResource = preset.sourceResource;
        } else if (restoreFinished) {
            enqueued.status = skinNow == original ? "target-observed" : "target-not-observed";
            enqueued.putOnAttempted = true;
            enqueued.taskCompleted = true;
            enqueued.skinBefore = skinNow;
            enqueued.skinAfter = skinNow;
            enqueued.targetItem = original;
            enqueued.targetResource = preset.sourceResource;
        } else {
            BOOL forceRestore = (!VehicleState.putOnTaskCompleted &&
                                 applyRelease.completionValue != 2) ||
                                skinNow == preset.targetItem;
            enqueued = CCPEnqueuePutOnByResID(
                VehicleContext.target.pid, VehicleContext.game, VehicleContext.avatar, VehicleContext.controller,
                VehicleContext.vehicleComponentOffset, VehicleContext.vehicleComponentHeap,
                VehicleContext.vehicleComponentBitmapWord, VehicleContext.vehicleComponentBit,
                VehicleContext.vehicleUser, VehicleContext.vehicle, skinNow,
                !VehicleState.putOnTaskCompleted ? preset.targetItem : 0,
                original, preset.sourceResource, forceRestore,
                VehicleContext.target.read, VehicleContext.target.write, remoteCall);
        }
        if (enqueued.putOnAttempted && !enqueued.mappingReleased && enqueued.codePage && enqueued.dataPage &&
            enqueued.mappingLength && !VehicleState.pendingRestoreMapping) {
            VehicleState.pendingRestoreMapping = enqueued.codePage;
            VehicleState.pendingRestoreDataPage = enqueued.dataPage;
            VehicleState.pendingRestoreMappingLength = enqueued.mappingLength;
        }
        BOOL restoreStateObserved = CCPVehicleRestoreObserved(&enqueued);
        CCPVehicleSample rendered = {0};
        if (restoreStateObserved) {
            for (unsigned settle = 0; settle < 20 && !restored; settle++) {
                restored = VehicleContextSample(&VehicleContext, original, true, &rendered);
                if (!restored) usleep(250000);
            }
        }
        abandonedForExternalChange = conflict &&
            (VehicleState.putOnTaskCompleted || applyRelease.completionValue == 2);
        self.trialReport[@"restore"] = @{
            @"status": [NSString stringWithUTF8String:enqueued.status ?: "unknown"],
            @"original_skin_id": @(original),
            @"restore_resource_id": @(enqueued.targetResource),
            @"skin_before": @(skinNow),
            @"put_on_attempted": @(enqueued.putOnAttempted),
            @"skin_after": @(enqueued.skinAfter),
            @"task_completed": @(enqueued.taskCompleted),
            @"task_rejected_context": @(enqueued.taskRejectedContext),
            @"mapping_released": @(enqueued.mappingReleased),
            @"prior_apply_mapping_released": @(applyRelease.mappingReleased),
            @"prior_restore_mapping_released": @(restoreRelease.mappingReleased),
            @"conflict": @(conflict),
            @"ownership_abandoned_for_external_change": @(abandonedForExternalChange),
            @"state_observed": @(restoreStateObserved),
            @"render_entity_observed": @(restored),
            @"loaded_handle_item_id": @(rendered.entity.loadedID),
            @"master_sync_item_id": @(rendered.count ? rendered.items[0].item : 0),
            @"mesh_path_present": @(rendered.entity.meshPathPresent)
        };
    }
    if (restored || abandonedForExternalChange) VehicleState.active = NO;
    atomic_store(&MenuOwned, VehicleState.active);
    if ([reason isEqualToString:@"readiness-timeout"] && !VehicleState.active)
        atomic_store(&MenuStatus, CCPMenuFailed);
    else if (restored) atomic_store(&MenuStatus, CCPMenuVehicleDataRestored);
    else if (VehicleState.active) atomic_store(&MenuStatus, CCPMenuRestorePending);
    else if (self.trialReport[@"enqueue"]) atomic_store(&MenuStatus, CCPMenuIdle);
    self.trialReport[@"status"] = restored ? @"original-item-observed" :
        (abandonedForExternalChange ? @"external-vehicle-skin-preserved-ownership-released" :
        (VehicleState.active ? @"restore-not-confirmed-change-still-owned" :
        (self.trialReport[@"status"] ?: @"trial-finished")));
    [self writeTrialReport];
    dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
}
@end

static CCPCosmeticsController *CosmeticsController;
static void NativeMenuAction(uint32_t index, bool enabled) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [CosmeticsController selectPreset:index enabled:enabled];
    });
}
__attribute__((visibility("default"))) void CoreCosmeticsRenderPage(void) {
    static bool checked, valid;
    static uintptr_t checkedBase;
    uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
    if (!checked) {
        checked = true; checkedBase = base;
        valid = CCPIdentity((CCPReader){LocalRead, NULL}, base, CoreUUID);
        for (size_t i = 0; valid && i < sizeof(CCPUIEntries) / sizeof(CCPUIEntries[0]); i++) {
            uint8_t expected[32];
            for (size_t j = 0; j < sizeof(expected); j++) {
                unsigned byte = 0;
                if (sscanf(CCPUIEntries[i].hex + j * 2, "%2x", &byte) != 1) { valid = false; break; }
                expected[j] = (uint8_t)byte;
            }
            if (valid) valid = EntryMatches(base + CCPUIEntries[i].rva, expected);
        }
    }
    float scale = 0;
    if (!valid || checkedBase != base || !LocalRead(NULL, base + 0xC15A80, &scale, sizeof(scale)) ||
        !isfinite(scale) || scale < 0.25f || scale > 8.0f) return;
    CCPUI ui = {
        .cursor = (void (*)(const CCPUIVec *))SignedEntry(base + 0x133168),
        .begin = (bool (*)(const char *, const CCPUIVec *, int, int))SignedEntry(base + 0xC88B0),
        .end = (void (*)(void))SignedEntry(base + 0x12E620),
        .space = (void (*)(const CCPUIVec *))SignedEntry(base + 0x14F980),
        .checkbox = (bool (*)(const char *, bool *, float))SignedEntry(base + 0xC8928),
        .button = (bool (*)(const char *, const char *, bool))SignedEntry(base + 0xC7A60),
    };
    static unsigned category, page;
    CCPDrawCosmeticsPage(ui, scale, (CCPMenuStatus)atomic_load(&MenuStatus),
        atomic_load(&MenuBusy) || atomic_load(&MenuOwned), atomic_load(&MenuPresetIndex), &category, &page, NativeMenuAction);
}
__attribute__((constructor)) static void StartCosmeticsProbe(void) {
    CCPCosmeticsController *controller = [CCPCosmeticsController new];
    controller.backgroundTask = UIBackgroundTaskInvalid;
    controller.pendingPresetIndex = NSNotFound;
    CosmeticsController = controller;
    [controller startEarlySession];
}
