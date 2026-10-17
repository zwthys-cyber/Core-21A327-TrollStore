#ifndef CORE_COSMETICS_VEHICLE_READ_H
#define CORE_COSMETICS_VEHICLE_READ_H
#include "CoreCosmeticsComponents.h"
#include "CoreCosmeticsVehicleEntity.h"

// Reviewed only against the pinned 1.38.12/15915 binary in Actions. Vehicle
// SynData has a different stride and refresh path from character MeshSynData.
#define CCP_PLAYER_CONTROLLER_CLASS_RVA 0x11C5A650
#define CCP_VEHICLE_USER_CLASS_RVA 0x11CCB6E8
#define CCP_VEHICLE_USER_CURRENT_OFFSET 0x158
#define CCP_VEHICLE_AVATAR_OFFSET 0x1ED8
#define CCP_VEHICLE_SYNC_OFFSET 0x11E8
#define CCP_VEHICLE_STRIDE 0x30
#define CCP_MAX_VEHICLE_SLOTS 16
typedef struct { uint32_t type, item, updateCounter; } CCPVehicleItem;
typedef enum {
    CCPVehicleSampled, CCPVehicleUnavailable, CCPVehicleOwnerUnconfirmed,
    CCPVehicleMethodsUnconfirmed, CCPVehicleArrayUnconfirmed, CCPVehicleChanged,
    CCPVehicleSourceReadFailed, CCPVehicleSourceUnavailable, CCPVehicleSourceOwnerUnconfirmed,
    CCPVehiclePointerReadFailed, CCPVehiclePointerInvalid,
    CCPVehicleResolverUnconfirmed, CCPVehicleControllerClassUnconfirmed, CCPVehicleSourceActorUnconfirmed,
    CCPVehicleUserClassUnconfirmed, CCPVehicleComponentsReadFailed, CCPVehicleComponentsInvalid,
    CCPVehicleComponentsChanged, CCPVehicleComponentsAmbiguous, CCPVehicleCharacterUnconfirmed
} CCPVehicleStatus;
typedef struct {
    CCPVehicleStatus status;
    uint32_t skin, previousSkin, pendingSkin, realSkin, defaultSkin;
    uint32_t count;
    uint64_t onrepRVA, tickRVA, vehicleTableRVA, avatarTableRVA;
    uint64_t userTableRVA, resolverRVA, sourceTableRVA;
    bool resolverMatches, controllerClassAvailable, userClassAvailable, primaryRead, primaryNonzero, primaryClassMatches;
    bool fallbackRead, usedFallback, controllerPawnRead, controllerPawnMatches, sourceMatchesController, lookupAttempted;
    bool userAvailable, userOwnerMatches, userCharacterRead, userCharacterMatches, userCharacterNonzero, userCharacterPointerValid;
    bool vehicleFieldRead, vehicleNonzero, vehiclePointerValid;
    CCPComponentDiagnostic components;
    CCPVehicleEntityDiagnostic entity;
    uint8_t refreshPending;
    CCPVehicleItem items[CCP_MAX_VEHICLE_SLOTS];
} CCPVehicleSample;

static inline bool CCPVehicleAppearanceReady(const CCPVehicleSample *sample, uint32_t targetItem) {
    return sample && targetItem && sample->status == CCPVehicleSampled &&
        sample->skin == targetItem && sample->count > 0 && sample->items[0].item == targetItem &&
        sample->entity.status == CCPEntitySampled && sample->entity.handleKeyRead &&
        sample->entity.loadedID == targetItem && sample->entity.meshComponentAvailable &&
        sample->entity.meshPathPresent && sample->entity.renderFlagsRead &&
        !sample->entity.renderDirty && !sample->entity.delayedRenderDirty &&
        !sample->entity.appearanceDirty;
}

static inline bool CCPVehicleResolveActor(CCPReader reader, uint64_t game, uint64_t pawn, uint64_t controller,
                                         uint64_t *actor, CCPVehicleSample *out) {
    *actor = 0;
    uint64_t table = 0, resolver = 0, controllerClass = 0, controllerPawn = 0, primary = 0;
    out->status = CCPVehicleResolverUnconfirmed;
    if (!CCPPointer(pawn) || !CCPPointer(controller) || !CCPReadBytes(reader, pawn, &table, 8) ||
        table < game || table - game >= 0x120E0000 || !CCPReadBytes(reader, table + 0x1528, &resolver, 8)) return false;
    if (resolver >= game && resolver - game < 0xAF23BE4) out->resolverRVA = resolver - game;
    out->resolverMatches = resolver == game + 0x4C1158C;
    if (!out->resolverMatches) return false;
    out->controllerClassAvailable = CCPReadBytes(reader, game + CCP_PLAYER_CONTROLLER_CLASS_RVA, &controllerClass, 8) &&
        CCPPointer(controllerClass);
    if (!out->controllerClassAvailable) { out->status = CCPVehicleControllerClassUnconfirmed; return false; }
    out->controllerPawnRead = CCPReadBytes(reader, controller + 0x3540, &controllerPawn, 8);
    out->controllerPawnMatches = out->controllerPawnRead && controllerPawn == pawn;
    if (!out->controllerPawnMatches) { out->status = CCPVehicleSourceActorUnconfirmed; return false; }
    // Device-confirmed virtual +0x1528 is 0x4C1158C. Its base helper
    // 0x3DA2D20 takes Pawn+0x608 only after IsA(STExtraPlayerController).
    out->primaryRead = CCPReadBytes(reader, pawn + 0x608, &primary, 8);
    out->primaryNonzero = primary != 0;
    if (!out->primaryRead || (primary && !CCPPointer(primary))) {
        out->status = CCPVehicleSourceReadFailed; return false;
    }
    if (primary) {
        CCPClassResult type = CCPObjectIsA(reader, primary, controllerClass);
        if (type == CCPClassUnreadable) { out->status = CCPVehicleSourceReadFailed; return false; }
        out->primaryClassMatches = type == CCPClassMatches;
        if (out->primaryClassMatches) *actor = primary;
    }
    if (!*actor) {
        uint64_t fallback = 0;
        out->fallbackRead = CCPReadBytes(reader, pawn + 0x60C8, &fallback, 8);
        // The native resolver has additional virtual fallbacks. We accept
        // this reviewed field only when it is the same current Core controller.
        if (!out->fallbackRead || fallback != controller ||
            CCPObjectIsA(reader, fallback, controllerClass) != CCPClassMatches) {
            out->status = CCPVehicleSourceActorUnconfirmed; return false;
        }
        *actor = fallback; out->usedFallback = true;
    }
    out->sourceMatchesController = *actor == controller;
    if (!out->sourceMatchesController) { out->status = CCPVehicleSourceActorUnconfirmed; return false; }
    if (CCPReadBytes(reader, *actor, &table, 8) && table >= game && table - game < 0x120E0000)
        out->sourceTableRVA = table - game;
    return true;
}
static inline bool CCPVehicleSource(CCPReader reader, uint64_t game, uint64_t pawn, uint64_t controller,
                                   uint64_t *actor, uint64_t *user, uint64_t *vehicle, CCPVehicleSample *diagnostic) {
    CCPVehicleSample scratch = {0};
    CCPVehicleSample *out = diagnostic ? diagnostic : &scratch;
    *user = 0; *vehicle = 0;
    uint64_t outer = 0, character = 0, userClass = 0;
    if (!CCPVehicleResolveActor(reader, game, pawn, controller, actor, out)) return false;
    out->userClassAvailable = CCPReadBytes(reader, game + CCP_VEHICLE_USER_CLASS_RVA, &userClass, 8) && CCPPointer(userClass);
    if (!out->userClassAvailable) { out->status = CCPVehicleUserClassUnconfirmed; return false; }
    out->lookupAttempted = true;
    out->userAvailable = CCPFindOwnedComponent(reader, *actor, userClass, user, &out->components);
    if (!out->userAvailable) {
        const CCPVehicleStatus failures[] = {CCPVehicleSourceUnavailable, CCPVehicleSourceUnavailable,
            CCPVehicleComponentsReadFailed, CCPVehicleComponentsInvalid, CCPVehicleComponentsChanged, CCPVehicleComponentsAmbiguous};
        out->status = failures[out->components.status]; return false;
    }
    if (!CCPReadBytes(reader, *user + 0x20, &outer, 8)) {
        out->status = CCPVehicleSourceReadFailed; return false;
    }
    out->userOwnerMatches = outer == *actor;
    if (!out->userOwnerMatches) { out->status = CCPVehicleSourceOwnerUnconfirmed; return false; }
    out->userCharacterRead = CCPReadBytes(reader, *user + 0x168, &character, 8);
    out->userCharacterNonzero = character != 0;
    out->userCharacterPointerValid = CCPPointer(character);
    out->userCharacterMatches = out->userCharacterRead && character == pawn;
    if (!out->userCharacterMatches) { out->status = CCPVehicleCharacterUnconfirmed; return false; }
    out->vehicleFieldRead = CCPReadBytes(reader, *user + CCP_VEHICLE_USER_CURRENT_OFFSET, vehicle, 8);
    if (!out->vehicleFieldRead) { out->status = CCPVehiclePointerReadFailed; return false; }
    out->vehicleNonzero = *vehicle != 0;
    if (!out->vehicleNonzero) { out->status = CCPVehicleUnavailable; return false; }
    out->vehiclePointerValid = CCPPointer(*vehicle);
    if (!out->vehiclePointerValid) { out->status = CCPVehiclePointerInvalid; return false; }
    return true;
}
static inline bool CCPVehicleOwner(CCPReader reader, uint64_t vehicle, uint64_t *avatar) {
    uint64_t outer = 0, owner = 0;
    return CCPPointer(vehicle) && CCPReadBytes(reader, vehicle + CCP_VEHICLE_AVATAR_OFFSET, avatar, 8) && CCPPointer(*avatar) &&
        CCPReadBytes(reader, *avatar + 0x20, &outer, 8) && outer == vehicle &&
        CCPReadBytes(reader, *avatar + 0x10B8, &owner, 8) && owner == vehicle;
}
static inline bool CCPReadVehicle(CCPReader reader, uint64_t game, uint64_t pawn, uint64_t controller, CCPVehicleSample *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    uint64_t actor = 0, user = 0, vehicle = 0, avatar = 0, table = 0, vehicleTable = 0, userTable = 0, onrep = 0, tick = 0;
    bool sourceOK = CCPVehicleSource(reader, game, pawn, controller, &actor, &user, &vehicle, out);
    if (out->userAvailable && CCPReadBytes(reader, user, &userTable, 8) && userTable >= game && userTable - game < 0x120E0000)
        out->userTableRVA = userTable - game;
    if (!sourceOK) return false;
    if (!CCPVehicleOwner(reader, vehicle, &avatar)) {
        out->status = CCPVehicleOwnerUnconfirmed; return false;
    }
    if (CCPReadBytes(reader, vehicle, &vehicleTable, 8) && vehicleTable >= game && vehicleTable - game < 0x120E0000)
        out->vehicleTableRVA = vehicleTable - game;
    if (!CCPReadBytes(reader, avatar, &table, 8) || !CCPPointer(table) || table < game || table - game >= 0x120E0000 ||
        !CCPReadBytes(reader, table + 0xD18, &onrep, 8) ||
        !CCPReadBytes(reader, table + 0x4C0, &tick, 8)) {
        out->status = CCPVehicleMethodsUnconfirmed; return false;
    }
    out->avatarTableRVA = table - game;
    if (onrep >= game && onrep - game < 0xAF23BE4) out->onrepRVA = onrep - game;
    if (tick >= game && tick - game < 0xAF23BE4) out->tickRVA = tick - game;
    if ((onrep != game + 0x47EE7C8 && onrep != game + 0x881F248) || tick != game + 0x42E4F28) {
        out->status = CCPVehicleMethodsUnconfirmed; return false;
    }
    CCPArray first = {0}, after = {0};
    uint8_t entries[CCP_MAX_VEHICLE_SLOTS * CCP_VEHICLE_STRIDE], repeated[sizeof(entries)];
    uint32_t skin[4] = {0}, skinAfter[4] = {0}, defaultSkin = 0;
    uint64_t actorAfter = 0, userAfter = 0, vehicleAfter = 0, avatarAfter = 0;
    uint8_t pending = 0;
    if (!CCPReadBytes(reader, avatar + CCP_VEHICLE_SYNC_OFFSET, &first, sizeof(first)) ||
        first.count < 0 || first.count > CCP_MAX_VEHICLE_SLOTS || first.capacity < first.count ||
        first.capacity > 128 || (first.count && !CCPPointer(first.data))) {
        out->status = CCPVehicleArrayUnconfirmed; return false;
    }
    size_t length = (size_t)first.count * CCP_VEHICLE_STRIDE;
    CCPReadVehicleEntity(reader, game, avatar, CCP_ENTITY_ITEM_CLASS_RVA, &out->entity);
    if (!CCPReadBytes(reader, vehicle + 0x20AC, skin, sizeof(skin)) ||
        !CCPReadBytes(reader, avatar + 0x10A0 + 4, &defaultSkin, 4) ||
        !CCPReadBytes(reader, avatar + 0xF50, &pending, 1) ||
        (length && (!CCPReadBytes(reader, first.data, entries, length) ||
                    !CCPReadBytes(reader, first.data, repeated, length) || memcmp(entries, repeated, length))) ||
        !CCPReadBytes(reader, avatar + CCP_VEHICLE_SYNC_OFFSET, &after, sizeof(after)) ||
        memcmp(&first, &after, sizeof(first)) ||
        !CCPReadBytes(reader, vehicle + 0x20AC, skinAfter, sizeof(skinAfter)) ||
        memcmp(skin, skinAfter, sizeof(skin)) ||
        !CCPVehicleSource(reader, game, pawn, controller, &actorAfter, &userAfter, &vehicleAfter, NULL) ||
        actorAfter != actor || userAfter != user || vehicleAfter != vehicle ||
        !CCPVehicleOwner(reader, vehicleAfter, &avatarAfter) || avatarAfter != avatar) {
        out->status = CCPVehicleChanged; return false;
    }
    out->skin = skin[1]; out->previousSkin = skin[2]; out->pendingSkin = skin[0]; out->realSkin = skin[3];
    out->defaultSkin = defaultSkin; out->refreshPending = pending;
    out->onrepRVA = onrep - game; out->tickRVA = tick - game;
    for (int32_t i = 0; i < first.count; i++) {
        const uint8_t *entry = entries + i * CCP_VEHICLE_STRIDE;
        // Export only scalar appearance fields. Instance IDs, object pointers
        // and per-item additional-data pointers never leave this reader.
        out->items[i] = (CCPVehicleItem){CCPU32(entry), CCPU32(entry + 4), CCPU32(entry + 0x1C)};
    }
    out->count = (uint32_t)first.count;
    out->status = CCPVehicleSampled;
    return true;
}
#endif
