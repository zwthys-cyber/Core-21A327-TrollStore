#ifndef CORE_COSMETICS_VEHICLE_READ_H
#define CORE_COSMETICS_VEHICLE_READ_H
#include "CoreCosmeticsRead.h"

// Reviewed only against the pinned 1.38.12/15915 binary in Actions. Vehicle
// SynData has a different stride and refresh path from character MeshSynData.
#define CCP_VEHICLE_USER_OFFSET 0x47F8
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
    CCPVehiclePointerReadFailed, CCPVehiclePointerInvalid
} CCPVehicleStatus;
typedef struct {
    CCPVehicleStatus status;
    uint32_t skin, previousSkin, pendingSkin, realSkin, defaultSkin;
    uint32_t count;
    uint64_t onrepRVA, tickRVA, vehicleTableRVA, avatarTableRVA;
    uint64_t userTableRVA;
    bool userFieldRead, userAvailable, userOwnerMatches, vehicleFieldRead, vehicleNonzero, vehiclePointerValid;
    uint8_t refreshPending;
    CCPVehicleItem items[CCP_MAX_VEHICLE_SLOTS];
} CCPVehicleSample;

static inline bool CCPVehicleSource(CCPReader reader, uint64_t pawn, uint64_t *user,
                                   uint64_t *vehicle, CCPVehicleSample *diagnostic) {
    CCPVehicleSample scratch = {0};
    CCPVehicleSample *out = diagnostic ? diagnostic : &scratch;
    uint64_t outer = 0;
    // Native helpers 0x4C86960 and 0x3A77E8C resolve a vehicle-user component
    // before reading its current-vehicle field. Pawn+0x3710 is not this path.
    out->userFieldRead = CCPPointer(pawn) && CCPReadBytes(reader, pawn + CCP_VEHICLE_USER_OFFSET, user, 8);
    if (!out->userFieldRead) { out->status = CCPVehicleSourceReadFailed; return false; }
    out->userAvailable = CCPPointer(*user);
    if (!out->userAvailable) { out->status = CCPVehicleSourceUnavailable; return false; }
    if (!CCPReadBytes(reader, *user + 0x20, &outer, 8)) {
        out->status = CCPVehicleSourceReadFailed; return false;
    }
    out->userOwnerMatches = outer == pawn;
    if (!out->userOwnerMatches) { out->status = CCPVehicleSourceOwnerUnconfirmed; return false; }
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
static inline bool CCPReadVehicle(CCPReader reader, uint64_t game, uint64_t pawn, CCPVehicleSample *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    uint64_t user = 0, vehicle = 0, avatar = 0, table = 0, vehicleTable = 0, userTable = 0, onrep = 0, tick = 0;
    bool sourceOK = CCPVehicleSource(reader, pawn, &user, &vehicle, out);
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
    uint64_t userAfter = 0, vehicleAfter = 0, avatarAfter = 0;
    uint8_t pending = 0;
    if (!CCPReadBytes(reader, avatar + CCP_VEHICLE_SYNC_OFFSET, &first, sizeof(first)) ||
        first.count < 1 || first.count > CCP_MAX_VEHICLE_SLOTS || first.capacity < first.count ||
        first.capacity > 128 || !CCPPointer(first.data)) {
        out->status = CCPVehicleArrayUnconfirmed; return false;
    }
    size_t length = (size_t)first.count * CCP_VEHICLE_STRIDE;
    if (!CCPReadBytes(reader, vehicle + 0x20AC, skin, sizeof(skin)) ||
        !CCPReadBytes(reader, avatar + 0x10A0 + 4, &defaultSkin, 4) ||
        !CCPReadBytes(reader, avatar + 0xF50, &pending, 1) ||
        !CCPReadBytes(reader, first.data, entries, length) ||
        !CCPReadBytes(reader, first.data, repeated, length) || memcmp(entries, repeated, length) ||
        !CCPReadBytes(reader, avatar + CCP_VEHICLE_SYNC_OFFSET, &after, sizeof(after)) ||
        memcmp(&first, &after, sizeof(first)) ||
        !CCPReadBytes(reader, vehicle + 0x20AC, skinAfter, sizeof(skinAfter)) ||
        memcmp(skin, skinAfter, sizeof(skin)) ||
        !CCPVehicleSource(reader, pawn, &userAfter, &vehicleAfter, NULL) || userAfter != user || vehicleAfter != vehicle ||
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
