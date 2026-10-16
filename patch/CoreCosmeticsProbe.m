#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <ptrauth.h>
#import <stdatomic.h>
#import <math.h>
#import <stdio.h>
#import "CoreCosmeticsRead.h"
#import "CoreCosmeticsVehicleRead.h"
#import "CoreCosmeticsWriteCheck.h"
#import "CoreCosmeticsTrial.h"
#import "CoreCosmeticsVehicleTrial.h"
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
    uint64_t game, pawn, avatar, controller;
    CCPSnapshot snapshot;
    uint8_t binding[0x30];
    CCPSnapshotFunction getSnapshot;
    CCPTarget target;
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
static bool EntryMatches(uintptr_t address, const uint8_t expected[32]) {
    uint8_t bytes[32];
    return CCPReadBytes((CCPReader){LocalRead, NULL}, address, bytes, sizeof(bytes)) &&
        !memcmp(bytes, expected, sizeof(bytes));
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
        @"avatar_refresh_pending": @(sample.refreshPending),
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
static NSDictionary *Sample(BOOL writeCheck, CCPResolvedTarget *resolved, BOOL vehicleOnly) {
    CCPReader local = {LocalRead, NULL};
    uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
    if (!CCPIdentity(local, base, CoreUUID) || !EntryMatches(base + 0x64C7C, SnapshotPrefix) ||
        !EntryMatches(base + 0x64D04, ReadPrefix)) return @{@"status": @"core-identity-mismatch"};
    CCPSnapshotFunction snapshot = (CCPSnapshotFunction)SignedEntry(base + 0x64C7C);
    CCPSnapshot before = {0}, after = {0};
    if (!snapshot(&before) || !before.words[1]) return @{@"status": @"core-session-not-ready"};
    CCPTarget target = {.pid = CCPU32((const uint8_t *)&before + 0x18),
                        .read = (CCPReadFunction)SignedEntry(base + 0x64D04), .write = NULL};
    if (!target.pid || target.pid > INT32_MAX) return @{@"status": @"target-not-bound"};
    CCPReader remote = {TargetRead, &target};
    uint8_t config[0x3A8], configAfter[sizeof(config)];
    if (!CCPReadBytes(local, base + 0xC54270, config, sizeof(config))) return @{@"status": @"core-context-unavailable"};
    uint64_t game = CCPU64(config + 0x18), pawn = CCPU64(config + 0x3A0);
    if (!CCPIdentity(remote, game, CCPGameUUID)) return @{@"status": @"game-identity-unconfirmed"};
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
        if (!snapshot(&after) || memcmp(&before, &after, 0x28) ||
            !CCPReadBytes(local, base + 0xC54270, configAfter, sizeof(configAfter)) ||
            CCPU64(configAfter + 0x18) != game || CCPU64(configAfter + 0x3A0) != pawn ||
            CCPU64(configAfter + 0x390) != controller ||
            !CCPReadBytes(local, base + 0xC288B0, bindingAfter, sizeof(bindingAfter)) ||
            memcmp(binding, bindingAfter, sizeof(binding)))
            return @{@"status": @"session-or-character-changed"};
        if (resolved && [vehicle[@"sync_array_stable"] boolValue] && EntryMatches(base + 0x620B8, WritePrefix)) {
            *resolved = (CCPResolvedTarget){.coreBase = base, .game = game, .pawn = pawn, .controller = controller,
                .snapshot = before, .getSnapshot = snapshot, .target = target};
            memcpy(resolved->binding, binding, sizeof(binding));
            resolved->target.write = (CCPWriteFunction)SignedEntry(base + 0x620B8);
        }
        return @{@"status": @"vehicle-session-sampled", @"vehicle_candidate": vehicle,
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
    return target->coreBase == (uintptr_t)_dyld_get_image_header(0) &&
        EntryMatches(target->coreBase + 0x620B8, WritePrefix) &&
        target->getSnapshot(&now) && now.words[1] == target->snapshot.words[1] &&
        CCPU32((const uint8_t *)&now + 0x18) == target->target.pid &&
        CCPReadBytes(local, target->coreBase + 0xC288B0, binding, sizeof(binding)) &&
        !memcmp(binding, target->binding, sizeof(binding)) &&
        CCPReadBytes(local, target->coreBase + 0xC54270 + 0x18, &game, sizeof(game)) && game == target->game &&
        CCPReadBytes(local, target->coreBase + 0xC54270 + 0x3A0, &pawn, sizeof(pawn)) && pawn == target->pawn &&
        CCPReadBytes(local, target->coreBase + 0xC54270 + 0x390, &controller, sizeof(controller)) && controller == target->controller &&
        CCPIdentity(remote, target->game, CCPGameUUID);
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
static NSDictionary *VehicleTrialResultJSON(CCPVehicleTrialResult result) {
    NSArray *stages = @[@"not-started", @"source", @"membership", @"object-identity", @"sync-array", @"counter-cache",
        @"master-slot", @"stable-layout", @"counter-range", @"ready"];
    NSArray *statuses = @[@"request-written-awaiting-engine", @"restoring-awaiting-engine", @"original-fields-observed",
        @"memory-restored-refresh-unconfirmed", @"binding-changed", @"vehicle-layout-changed", @"matching-vehicle-required",
        @"earlier-change-still-owned", @"native-write-failed", @"vehicle-changed-by-game", @"no-active-change"];
    return @{@"status": statuses[result.status], @"native_status": @(result.nativeStatus),
        @"prepare_stage": stages[VehicleState.prepareStage], @"sync_item_count": @(VehicleState.sync.count),
        @"cached_counter_count": @(VehicleState.cached.count),
        @"written_bytes": @(result.writtenBytes), @"total_written_bytes": @(result.totalWrittenBytes),
        @"id_write_confirmed": @(result.idConfirmed), @"pending_request_written": @(result.requestWritten),
        @"memory_restored": @(result.dataRestored), @"native_restore_observed": @(result.nativeRestoreObserved),
        @"change_still_owned": @(VehicleState.active)};
}
static NSDictionary *VehicleTrialObservation(void) {
    CCPVehicleView view = {0};
    BOOL bound = TrialBindingValid(&VehicleContext);
    BOOL layout = bound && VehicleState.prepared &&
        CCPVehicleTrialLayout((CCPReader){TargetRead, &VehicleContext.target}, &VehicleState, &view);
    BOOL engineChanged = layout && view.pending == 0 && view.skin == CCP_VEHICLE_TARGET && view.real == CCP_VEHICLE_TARGET;
    VehicleState.engineApplyObserved |= engineChanged;
    return @{@"binding_matches": @(bound), @"layout_matches": @(layout),
        @"master_item_id": @(view.master), @"native_update_counter": @(view.counter),
        @"cached_update_counter": @(view.cachedCounter), @"pending_skin_id": @(view.pending),
        @"cached_update_counter_read": @(layout && view.cachedCounterRead),
        @"vehicle_skin_id": @(view.skin), @"previous_skin_id": @(view.previous),
        @"real_skin_id": @(view.real), @"default_skin_id": @(view.defaultSkin),
        @"pending_request_consumed": @(layout && VehicleState.requestSent && view.pending == 0),
        @"native_skin_matches_target": @(engineChanged)};
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
@property(nonatomic) NSUInteger pendingPresetIndex;
@property(nonatomic) BOOL vehicleCapturing;
@property(nonatomic) BOOL vehicleTrialMode;
- (void)startTrial;
- (void)prepareTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
- (void)restoreTrial;
- (void)selectPreset:(uint32_t)index enabled:(BOOL)enabled;
- (void)finishTrialOnQueue:(uint32_t)generation reason:(NSString *)reason;
- (void)finishGeneration:(uint32_t)generation;
- (void)startVehicleCapture;
- (void)captureVehicleOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
- (void)finishVehicleCaptureOnQueue:(uint32_t)generation reason:(NSString *)reason;
- (void)startVehicleTrial;
- (void)prepareVehicleTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
- (void)finishVehicleTrialOnQueue:(uint32_t)generation reason:(NSString *)reason;
- (void)observeVehicleRestoreOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
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
            dispatch_async(self.captureQueue, ^{ [self finishTrialOnQueue:generation reason:@"background-expiration"]; });
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
        self.trialReport = [@{@"schema_version": @5, @"trial_revision": @5, @"mode": @"single-slot-appearance-trial",
            @"game_version": @"1.38.12", @"game_build": @"15915",
            @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
            @"kernel_writes_implemented": @YES, @"appearance_changes_implemented": @YES,
            @"preset_visual_effect_previously_verified": @(preset.slot == 5 && preset.target == CCP_TRIAL_COAT),
            @"current_trial_visual_confirmed": @NO, @"ui_location": @"native-menu-below-position",
            @"preset": [NSString stringWithUTF8String:preset.label], @"category": [NSString stringWithUTF8String:preset.category],
            @"slot_index": @(preset.slot), @"target_id": @(preset.target), @"automatic_restore_seconds": @15} mutableCopy];
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
            self.trialReport[@"status"] = applied.idConfirmed ? @"id-applied-awaiting-visual-confirmation" : @"apply-not-confirmed";
            [self writeTrialReport];
            if (!applied.idConfirmed) {
                [self finishTrialOnQueue:generation reason:@"apply-failed"];
                return;
            }
            self.samples = [NSMutableArray array];
            self.trialTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self.captureQueue);
            dispatch_source_set_timer(self.trialTimer, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
                                      3 * NSEC_PER_SEC, NSEC_PER_SEC / 4);
            dispatch_source_set_event_handler(self.trialTimer, ^{
                @autoreleasepool {
                    if (self.trialGeneration != generation || self.trialFinishing) return;
                    uint32_t current = 0; uint8_t pending = 0;
                    BOOL bound = TrialBindingValid(&TrialContext);
                    BOOL sameLayout = bound && CCPTrialLayout(reader, &TrialState, &current);
                    BOOL flagRead = sameLayout && CCPReadBytes(reader, TrialState.avatar + CCP_REFRESH_PENDING, &pending, 1);
                    [self.samples addObject:@{@"binding_matches": @(bound), @"layout_matches": @(sameLayout),
                        @"current_item_id": @(current), @"refresh_flag_read": @(flagRead), @"refresh_pending": @(pending)}];
                    self.trialReport[@"observations"] = self.samples.copy;
                    [self writeTrialReport];
                    if (!bound || !sameLayout || self.samples.count >= 5)
                        [self finishTrialOnQueue:generation reason:bound && sameLayout ? @"automatic-restore" : @"context-changed"];
                }
            });
            dispatch_resume(self.trialTimer);
        });
}
- (void)restoreTrial {
    if (!self.captureQueue) return;
    uint32_t generation = self.generation;
    if (self.vehicleCapturing) {
        dispatch_async(self.captureQueue, ^{ [self finishVehicleCaptureOnQueue:generation reason:@"manual-stop"]; });
        return;
    }
    if (self.vehicleTrialMode || (atomic_load(&MenuOwned) && atomic_load(&MenuPresetIndex) == CCP_VEHICLE_TRIAL_INDEX)) {
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
- (void)selectPreset:(uint32_t)index enabled:(BOOL)enabled {
    if (index == CCP_VEHICLE_CAPTURE_INDEX) {
        if (enabled) [self startVehicleCapture];
        else if (self.vehicleCapturing) { self.pendingPresetIndex = NSNotFound; [self restoreTrial]; }
        return;
    }
    if (index == CCP_VEHICLE_TRIAL_INDEX) {
        if (!enabled) {
            if (!self.vehicleTrialMode && !(atomic_load(&MenuOwned) && atomic_load(&MenuPresetIndex) == index)) return;
            self.pendingPresetIndex = NSNotFound; [self restoreTrial];
        }
        else if (self.capturing || atomic_load(&MenuOwned)) {
            self.pendingPresetIndex = index; [self restoreTrial];
        } else { self.pendingPresetIndex = NSNotFound; [self startVehicleTrial]; }
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
    if (!atomic_load(&MenuOwned) && self.pendingPresetIndex == CCP_VEHICLE_TRIAL_INDEX) {
        self.pendingPresetIndex = NSNotFound;
        [self startVehicleTrial];
    } else if (!atomic_load(&MenuOwned) && self.pendingPresetIndex < CCP_PRESET_COUNT) {
        self.presetIndex = self.pendingPresetIndex;
        self.pendingPresetIndex = NSNotFound;
        [self startTrial];
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
        self.trialReport = [@{@"schema_version": @9, @"probe_revision": @9, @"mode": @"vehicle-read-adaptation",
            @"game_version": @"1.38.12", @"game_build": @"15915",
            @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
            @"vehicle_writes_implemented": @NO, @"vehicle_appearance_changes_implemented": @NO,
            @"vehicle_trial_available": @YES,
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
- (void)startVehicleTrial {
    if (self.capturing) return;
    self.capturing = YES; self.vehicleCapturing = NO; self.vehicleTrialMode = YES;
    atomic_store(&MenuBusy, true);
    atomic_store(&MenuPresetIndex, CCP_VEHICLE_TRIAL_INDEX);
    atomic_store(&MenuStatus, CCPMenuVehicleTrialWaiting);
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core vehicle trial"
        expirationHandler:^{
            if (self.generation != generation || !self.capturing) return;
            dispatch_async(self.captureQueue, ^{ [self finishVehicleTrialOnQueue:generation reason:@"background-expiration"]; });
        }];
    dispatch_async(self.captureQueue, ^{
        self.trialGeneration = generation; self.trialFinishing = NO;
        if (TrialState.active || VehicleState.active) {
            atomic_store(&MenuStatus, CCPMenuRestorePending);
            dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
            return;
        }
        VehicleContext = (CCPResolvedTarget){0}; VehicleState = (CCPVehicleTrial){0};
        self.trialReport = [@{@"schema_version": @9, @"trial_revision": @9, @"mode": @"single-vehicle-request-trial",
            @"game_version": @"1.38.12", @"game_build": @"15915",
            @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
            @"preset": @"宾利欧陆GT敞篷（火焰橘）", @"original_id": @(CCP_VEHICLE_ORIGINAL),
            @"target_id": @(CCP_VEHICLE_TARGET), @"automatic_restore_seconds": @15,
            @"vehicle_source_previously_device_verified": @YES, @"vehicle_writes_implemented": @YES,
            @"vehicle_appearance_trial_implemented": @YES, @"visual_effect_verified": @NO,
            @"refresh_strategy": @"native-pending-skin-field-request",
            @"native_refresh_consumption_verified": @NO, @"ui_location": @"native-menu-below-position"} mutableCopy];
        [self prepareVehicleTrialOnQueue:generation attempt:0];
    });
}
- (void)prepareVehicleTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    VehicleContext = (CCPResolvedTarget){0};
    NSDictionary *before = Sample(NO, &VehicleContext, YES);
    self.trialReport[@"before"] = before;
    if (!VehicleContext.game || !VehicleContext.target.write || !TrialBindingValid(&VehicleContext)) {
        self.trialReport[@"status"] = @"waiting-for-core-or-vehicle";
        self.trialReport[@"waiting_attempts"] = @(attempt + 1); [self writeTrialReport];
        if (attempt >= 60) [self finishVehicleTrialOnQueue:generation reason:@"readiness-timeout"];
        else dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), self.captureQueue,
            ^{ [self prepareVehicleTrialOnQueue:generation attempt:attempt + 1]; });
        return;
    }
    NSDictionary *vehicle = before[@"vehicle_candidate"];
    if ([vehicle[@"vehicle_skin_id"] unsignedIntValue] != CCP_VEHICLE_ORIGINAL ||
        [vehicle[@"sync_item_count"] unsignedIntValue] != 11) {
        self.trialReport[@"status"] = @"matching-vehicle-required";
        atomic_store(&MenuStatus, CCPMenuVehicleWrongCar);
        [self finishVehicleTrialOnQueue:generation reason:@"matching-vehicle-required"]; return;
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), self.captureQueue, ^{
        if (self.trialGeneration != generation || self.trialFinishing) return;
        VehicleState = (CCPVehicleTrial){.game = VehicleContext.game, .pawn = VehicleContext.pawn,
            .controller = VehicleContext.controller};
        CCPReader reader = {TargetRead, &VehicleContext.target};
        CCPVehicleTrialResult result = CCPApplyVehicleTrial(reader, TrialTargetWrite, &VehicleContext, TrialBindingValid, &VehicleState);
        self.trialReport[@"apply"] = VehicleTrialResultJSON(result);
        self.trialReport[@"original_update_counter"] = @(VehicleState.originalCounter);
        self.trialReport[@"applied_update_counter"] = @(VehicleState.appliedCounter);
        self.trialReport[@"status"] = result.requestWritten ? @"request-written-awaiting-engine-and-visual-confirmation" : @"apply-not-confirmed";
        atomic_store(&MenuOwned, VehicleState.active);
        atomic_store(&MenuStatus, result.requestWritten ? CCPMenuVehicleTrialApplied :
            (result.status == CCPVehicleTrialWrongVehicle ? CCPMenuVehicleWrongCar : CCPMenuFailed));
        [self writeTrialReport];
        if (!result.requestWritten) { [self finishVehicleTrialOnQueue:generation reason:@"apply-failed"]; return; }
        self.samples = [NSMutableArray array];
        self.trialTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self.captureQueue);
        dispatch_source_set_timer(self.trialTimer, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
                                  3 * NSEC_PER_SEC, NSEC_PER_SEC / 4);
        dispatch_source_set_event_handler(self.trialTimer, ^{
            @autoreleasepool {
                if (self.trialGeneration != generation || self.trialFinishing) return;
                NSDictionary *observation = VehicleTrialObservation();
                [self.samples addObject:observation]; self.trialReport[@"observations"] = self.samples.copy;
                self.trialReport[@"native_refresh_consumption_verified"] = @(VehicleState.engineApplyObserved);
                [self writeTrialReport];
                if (![observation[@"binding_matches"] boolValue] || ![observation[@"layout_matches"] boolValue] ||
                    !CCPVehicleTrialValuesOwned(&VehicleState, &(CCPVehicleView){
                        .master = [observation[@"master_item_id"] unsignedIntValue],
                        .counter = [observation[@"native_update_counter"] unsignedIntValue],
                        .pending = [observation[@"pending_skin_id"] unsignedIntValue],
                        .skin = [observation[@"vehicle_skin_id"] unsignedIntValue],
                        .real = [observation[@"real_skin_id"] unsignedIntValue],
                        .defaultSkin = [observation[@"default_skin_id"] unsignedIntValue]}) || self.samples.count >= 5)
                    [self finishVehicleTrialOnQueue:generation reason:self.samples.count >= 5 ? @"automatic-restore" : @"context-or-game-changed"];
            }
        });
        dispatch_resume(self.trialTimer);
    });
}
- (void)finishVehicleTrialOnQueue:(uint32_t)generation reason:(NSString *)reason {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    self.trialFinishing = YES;
    if (self.trialTimer) { dispatch_source_cancel(self.trialTimer); self.trialTimer = nil; }
    CCPReader reader = {TargetRead, &VehicleContext.target};
    CCPVehicleTrialResult result = CCPRestoreVehicleTrial(reader, TrialTargetWrite, &VehicleContext, TrialBindingValid, &VehicleState);
    self.trialReport[@"restore"] = VehicleTrialResultJSON(result);
    self.trialReport[@"finish_reason"] = reason;
    self.trialReport[@"restore_update_counter"] = @(VehicleState.restoreCounter);
    atomic_store(&MenuOwned, VehicleState.active);
    if (VehicleState.active) atomic_store(&MenuStatus, CCPMenuVehicleRestoring);
    self.samples = [NSMutableArray array]; [self writeTrialReport];
    [self observeVehicleRestoreOnQueue:generation attempt:[reason isEqualToString:@"background-expiration"] ? 6 : 0];
}
- (void)observeVehicleRestoreOnQueue:(uint32_t)generation attempt:(unsigned)attempt {
    if (self.trialGeneration != generation) return;
    NSDictionary *observation = VehicleTrialObservation();
    [self.samples addObject:observation]; self.trialReport[@"restore_observations"] = self.samples.copy;
    CCPVehicleTrialResult result = {.status = CCPVehicleTrialInactive};
    if (VehicleState.active) {
        result = CCPFinishVehicleRestore((CCPReader){TargetRead, &VehicleContext.target}, TrialTargetWrite,
            &VehicleContext, TrialBindingValid, &VehicleState, attempt >= 6);
        self.trialReport[@"restore_check"] = VehicleTrialResultJSON(result);
    }
    atomic_store(&MenuOwned, VehicleState.active);
    self.trialReport[@"change_still_owned"] = @(VehicleState.active);
    self.trialReport[@"native_refresh_consumption_verified"] = @(VehicleState.engineApplyObserved);
    BOOL restored = result.dataRestored || [self.trialReport[@"restore"][@"memory_restored"] boolValue];
    self.trialReport[@"status"] = VehicleState.active ? @"restore-not-confirmed" :
        (restored ? @"original-data-restored-awaiting-visual-confirmation" : self.trialReport[@"status"] ?: @"trial-finished");
    [self writeTrialReport];
    if (VehicleState.active && attempt < 6 && [observation[@"binding_matches"] boolValue] && [observation[@"layout_matches"] boolValue]) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), self.captureQueue,
            ^{ [self observeVehicleRestoreOnQueue:generation attempt:attempt + 1]; });
        return;
    }
    if (VehicleState.active) atomic_store(&MenuStatus, CCPMenuRestorePending);
    else if (restored) atomic_store(&MenuStatus, CCPMenuVehicleDataRestored);
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
    dispatch_async(dispatch_get_main_queue(), ^{
        CosmeticsController = [CCPCosmeticsController new];
        CosmeticsController.backgroundTask = UIBackgroundTaskInvalid;
        CosmeticsController.pendingPresetIndex = NSNotFound;
    });
}
