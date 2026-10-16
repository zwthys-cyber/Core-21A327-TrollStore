#ifndef CORE_COSMETICS_VEHICLE_ENTITY_H
#define CORE_COSMETICS_VEHICLE_ENTITY_H
#include "CoreCosmeticsComponents.h"

// Read-only diagnostics, pinned to the Actions-reviewed 1.38.12/15915 input.
// Native +0x8B8 mapping lookup: 0x42DD238 -> 0x3796F3C. The value is an
// AvatarEntity UObject. Its +0x30 is LoadedHandle, not another entity.
#define CCP_ENTITY_MAP_OFFSET 0x8B8
#define CCP_ENTITY_BASE_CLASS_RVA 0x11ABCB40
#define CCP_ENTITY_VEHICLE_CLASS_RVA 0x11CBB860
#define CCP_ENTITY_MASTER_CLASS_RVA 0x11BA1780
#define CCP_ENTITY_ITEM_CLASS_RVA 0x11CEAAC0
#define CCP_ENTITY_MAX_SLOTS 128
typedef enum {
    CCPEntityNotAttempted, CCPEntitySampled, CCPEntityMapReadFailed,
    CCPEntityMapInvalid, CCPEntityNotFound, CCPEntityAmbiguous, CCPEntityClassUnavailable,
    CCPEntityClassUnconfirmed, CCPEntityOwnerUnconfirmed, CCPEntityFieldsUnreadable,
    CCPEntityFieldsInvalid, CCPEntityChanged
} CCPVehicleEntityStatus;
typedef struct {
    CCPVehicleEntityStatus status;
    uint32_t mapSlots, mapOccupied, masterMatches, slot, subslot, loadedType, loadedID;
    uint64_t tableRVA, installRVA, materialRVA, behaviorRVA;
    bool headerRead, bitmapRead, heapBitmap, stable, baseClassAvailable, vehicleClassAvailable,
        masterClassAvailable, baseClassMatches, vehicleClassMatches, masterClassMatches,
        ownerRead, ownerMatches, handleAvailable, handleClassAvailable, handleClassMatches,
        handleKeyRead, cachedHandleAvailable, delayedHandleAvailable, cachedMatchesLoaded,
        meshComponentAvailable, meshPathPresent, lastMeshPathPresent, renderFlagsRead, hashMatches,
        cachedCounterRead;
    uint32_t cachedCounter, cachedCounterCount;
    uint8_t renderDirty, delayedRenderDirty, appearanceDirty;
} CCPVehicleEntityDiagnostic;

static inline uint64_t CCPEntityCodeRVA(uint64_t game, uint64_t value) {
    return value >= game && value - game < 0xAF23BE4 && !(value & 3) ? value - game : 0;
}
static inline bool CCPEntityPathPresent(const uint8_t path[24], bool *present) {
    int32_t length = (int32_t)CCPU32(path + 16), capacity = (int32_t)CCPU32(path + 20);
    if (length < 0 || length > 512 || capacity < length || capacity > 1024 ||
        (length && !CCPPointer(CCPU64(path + 8)))) return false;
    // Export existence only. Names, subpaths and process addresses stay private.
    *present = CCPU64(path) != 0 || length > 1;
    return true;
}
static inline bool CCPEntityClass(CCPReader reader, uint64_t game, uint64_t rva, uint64_t *value) {
    return CCPReadBytes(reader, game + rva, value, 8) && CCPPointer(*value);
}
static inline void CCPReadVehicleEntity(CCPReader reader, uint64_t game, uint64_t avatar,
                                       uint64_t itemClassRVA, CCPVehicleEntityDiagnostic *out) {
    memset(out, 0, sizeof(*out));
    out->status = CCPEntityMapReadFailed;
    CCPArray cache = {0}, cacheAfter = {0}; uint32_t counter = 0;
    if (CCPPointer(avatar) && CCPReadBytes(reader, avatar + 0x1208, &cache, sizeof(cache)) &&
        cache.count >= 0 && cache.count <= 16 && cache.capacity >= cache.count && cache.capacity <= 128 &&
        (!cache.count || CCPPointer(cache.data)) &&
        (!cache.count || CCPReadBytes(reader, cache.data, &counter, 4)) &&
        CCPReadBytes(reader, avatar + 0x1208, &cacheAfter, sizeof(cacheAfter)) && !memcmp(&cache, &cacheAfter, sizeof(cache))) {
        out->cachedCounterCount = (uint32_t)cache.count;
        out->cachedCounterRead = cache.count > 0; out->cachedCounter = counter;
    }
    uint8_t header[0x50], after[sizeof(header)];
    if (!CCPPointer(game) || !CCPPointer(avatar) ||
        !CCPReadBytes(reader, avatar + CCP_ENTITY_MAP_OFFSET, header, sizeof(header))) return;
    out->headerRead = true;
    uint64_t data = CCPU64(header), heap = CCPU64(header + 0x20);
    int32_t count = (int32_t)CCPU32(header + 8), capacity = (int32_t)CCPU32(header + 12);
    int32_t bits = (int32_t)CCPU32(header + 0x28), bitCapacity = (int32_t)CCPU32(header + 0x2C);
    int32_t freeCount = (int32_t)CCPU32(header + 0x34), hashes = (int32_t)CCPU32(header + 0x48);
    if (count < 0 || count > CCP_ENTITY_MAX_SLOTS || capacity < count || capacity > 256 ||
        bits != count || bitCapacity < bits || bitCapacity > 4096 || freeCount < 0 || freeCount > count ||
        hashes < 0 || hashes > 512 || (hashes && (hashes & (hashes - 1))) ||
        (count && !CCPPointer(data)) || (heap && !CCPPointer(heap)) || (!heap && bits > 128)) {
        out->status = CCPEntityMapInvalid; return;
    }
    out->mapSlots = (uint32_t)count; out->heapBitmap = heap != 0;
    uint64_t bitmap = heap ? heap : avatar + CCP_ENTITY_MAP_OFFSET + 0x10;
    uint32_t flags[CCP_ENTITY_MAX_SLOTS / 32] = {0}, flagsAfter[CCP_ENTITY_MAX_SLOTS / 32] = {0};
    uint8_t entries[CCP_ENTITY_MAX_SLOTS * 24], repeated[sizeof(entries)];
    size_t flagBytes = ((size_t)count + 31) / 32 * 4, bytes = (size_t)count * 24;
    if (count && (!CCPReadBytes(reader, bitmap, flags, flagBytes) ||
                  !CCPReadBytes(reader, data, entries, bytes))) return;
    out->bitmapRead = true;
    uint64_t entity = 0; int32_t selected = -1;
    for (int32_t i = 0; i < count; i++) {
        if (!(flags[(uint32_t)i / 32] & (UINT32_C(1) << ((uint32_t)i % 32)))) continue;
        out->mapOccupied++;
        if (CCPU32(entries + (size_t)i * 24) == 0) {
            out->masterMatches++; entity = CCPU64(entries + (size_t)i * 24 + 8); selected = i;
        }
    }
    if (out->mapOccupied != (uint32_t)(count - freeCount)) { out->status = CCPEntityMapInvalid; return; }
    if (out->masterMatches > 1) { out->status = CCPEntityAmbiguous; return; }
    uint64_t hash = CCPU64(header + 0x40), bucket = 0; int32_t bucketValue = -1, bucketAfter = -1;
    if (out->mapOccupied) {
        if (!hashes || (hash && !CCPPointer(hash))) { out->status = CCPEntityMapInvalid; return; }
        bucket = hash ? hash : avatar + CCP_ENTITY_MAP_OFFSET + 0x38;
        if (!CCPReadBytes(reader, bucket, &bucketValue, 4)) return;
        int32_t index = bucketValue, nativeSelected = -1; unsigned steps = 0;
        while (index != -1) {
            if (index < 0 || index >= count || steps++ >= (unsigned)count ||
                !(flags[(uint32_t)index / 32] & (UINT32_C(1) << ((uint32_t)index % 32)))) {
                out->status = CCPEntityMapInvalid; return;
            }
            const uint8_t *entry = entries + (size_t)index * 24;
            if (CCPU32(entry) == 0) { nativeSelected = index; break; }
            index = (int32_t)CCPU32(entry + 0x10);
        }
        out->hashMatches = nativeSelected == selected;
        if (!out->hashMatches) { out->status = CCPEntityMapInvalid; return; }
    } else out->hashMatches = true;
    if ((count && (!CCPReadBytes(reader, bitmap, flagsAfter, flagBytes) || memcmp(flags, flagsAfter, flagBytes) ||
                   !CCPReadBytes(reader, data, repeated, bytes) || memcmp(entries, repeated, bytes))) ||
        (bucket && (!CCPReadBytes(reader, bucket, &bucketAfter, 4) || bucketAfter != bucketValue)) ||
        !CCPReadBytes(reader, avatar + CCP_ENTITY_MAP_OFFSET, after, sizeof(after)) || memcmp(header, after, sizeof(header))) {
        out->status = CCPEntityChanged; return;
    }
    if (!out->masterMatches || !entity) { out->stable = true; out->status = CCPEntityNotFound; return; }
    uint64_t baseClass = 0, vehicleClass = 0, masterClass = 0;
    out->baseClassAvailable = CCPEntityClass(reader, game, CCP_ENTITY_BASE_CLASS_RVA, &baseClass);
    out->vehicleClassAvailable = CCPEntityClass(reader, game, CCP_ENTITY_VEHICLE_CLASS_RVA, &vehicleClass);
    out->masterClassAvailable = CCPEntityClass(reader, game, CCP_ENTITY_MASTER_CLASS_RVA, &masterClass);
    if (!out->baseClassAvailable || !out->vehicleClassAvailable) { out->status = CCPEntityClassUnavailable; return; }
    out->baseClassMatches = CCPObjectIsA(reader, entity, baseClass) == CCPClassMatches;
    out->vehicleClassMatches = CCPObjectIsA(reader, entity, vehicleClass) == CCPClassMatches;
    out->masterClassMatches = out->masterClassAvailable && CCPObjectIsA(reader, entity, masterClass) == CCPClassMatches;
    if (!out->baseClassMatches || !out->vehicleClassMatches) { out->status = CCPEntityClassUnconfirmed; return; }
    uint8_t identity[0x1C], identityAfter[sizeof(identity)], core[0x30], coreAfter[sizeof(core)],
        paths[2][24], pathsAfter[2][24], dirty[3], dirtyAfter[3];
    uint64_t table = 0, methods[3] = {0};
    out->status = CCPEntityFieldsUnreadable;
    if (!CCPReadBytes(reader, entity + 0xC, identity, sizeof(identity)) ||
        !CCPReadBytes(reader, entity + 0x28, core, sizeof(core))) return;
    out->slot = CCPU32(core); out->subslot = CCPU32(core + 4);
    out->ownerRead = true; out->ownerMatches = CCPU64(core + 0x20) == avatar;
    if (!out->ownerMatches || out->slot != 0) { out->status = CCPEntityOwnerUnconfirmed; return; }
    if (!CCPReadBytes(reader, entity, &table, 8) || !CCPPointer(table) || table < game || table - game > 0x120E0000 - 0x4E8 ||
        !CCPReadBytes(reader, table + 0x3C0, &methods[0], 8) ||
        !CCPReadBytes(reader, table + 0x478, &methods[1], 8) ||
        !CCPReadBytes(reader, table + 0x4E0, &methods[2], 8) ||
        !CCPReadBytes(reader, entity + 0x258, paths[0], 24) ||
        !CCPReadBytes(reader, entity + 0x3B8, paths[1], 24) ||
        !CCPReadBytes(reader, entity + 0x1B8, dirty, sizeof(dirty))) return;
    out->tableRVA = table - game; out->installRVA = CCPEntityCodeRVA(game, methods[0]);
    out->materialRVA = CCPEntityCodeRVA(game, methods[1]); out->behaviorRVA = CCPEntityCodeRVA(game, methods[2]);
    out->delayedRenderDirty = dirty[0]; out->renderDirty = dirty[1]; out->appearanceDirty = dirty[2];
    out->renderFlagsRead = true;
    if (!CCPEntityPathPresent(paths[0], &out->meshPathPresent) ||
        !CCPEntityPathPresent(paths[1], &out->lastMeshPathPresent) || dirty[0] > 1 || dirty[1] > 1 || dirty[2] > 1) {
        out->status = CCPEntityFieldsInvalid; return;
    }
    uint64_t handle = CCPU64(core + 8), cached = CCPU64(core + 0x10), delayed = CCPU64(core + 0x18);
    if ((handle && !CCPPointer(handle)) || (cached && !CCPPointer(cached)) || (delayed && !CCPPointer(delayed)) ||
        (CCPU64(core + 0x28) && !CCPPointer(CCPU64(core + 0x28)))) { out->status = CCPEntityFieldsInvalid; return; }
    out->handleAvailable = CCPPointer(handle); out->cachedHandleAvailable = CCPPointer(cached);
    out->delayedHandleAvailable = CCPPointer(delayed); out->cachedMatchesLoaded = handle && handle == cached;
    out->meshComponentAvailable = CCPPointer(CCPU64(core + 0x28));
    uint8_t handleIdentity[0x1C] = {0}, handleKey[24] = {0}; uint64_t itemClass = 0;
    if (out->handleAvailable) {
        out->handleClassAvailable = itemClassRVA && CCPEntityClass(reader, game, itemClassRVA, &itemClass);
        out->handleClassMatches = out->handleClassAvailable && CCPObjectIsA(reader, handle, itemClass) == CCPClassMatches;
        if (out->handleClassMatches) {
            if (!CCPReadBytes(reader, handle + 0xC, handleIdentity, sizeof(handleIdentity)) ||
                !CCPReadBytes(reader, handle + 0xA8, handleKey, sizeof(handleKey))) return;
            out->loadedType = CCPU32(handleKey); out->loadedID = CCPU32(handleKey + 4); out->handleKeyRead = true;
        }
    }
    uint64_t tableAfter = 0;
    if (!CCPReadBytes(reader, entity, &tableAfter, 8) || table != tableAfter ||
        !CCPReadBytes(reader, entity + 0xC, identityAfter, sizeof(identityAfter)) || memcmp(identity, identityAfter, sizeof(identity)) ||
        !CCPReadBytes(reader, entity + 0x28, coreAfter, sizeof(coreAfter)) || memcmp(core, coreAfter, sizeof(core)) ||
        !CCPReadBytes(reader, entity + 0x258, pathsAfter[0], 24) || memcmp(paths[0], pathsAfter[0], 24) ||
        !CCPReadBytes(reader, entity + 0x3B8, pathsAfter[1], 24) || memcmp(paths[1], pathsAfter[1], 24) ||
        !CCPReadBytes(reader, entity + 0x1B8, dirtyAfter, sizeof(dirtyAfter)) || memcmp(dirty, dirtyAfter, sizeof(dirty)) ||
        (count && (!CCPReadBytes(reader, bitmap, flagsAfter, flagBytes) || memcmp(flags, flagsAfter, flagBytes) ||
                   !CCPReadBytes(reader, data, repeated, bytes) || memcmp(entries, repeated, bytes))) ||
        (bucket && (!CCPReadBytes(reader, bucket, &bucketAfter, 4) || bucketAfter != bucketValue)) ||
        !CCPReadBytes(reader, avatar + CCP_ENTITY_MAP_OFFSET, after, sizeof(after)) || memcmp(header, after, sizeof(header))) {
        out->status = CCPEntityChanged; return;
    }
    if (out->handleKeyRead) {
        uint8_t keyAfter[24], idAfter[sizeof(handleIdentity)];
        if (!CCPReadBytes(reader, handle + 0xC, idAfter, sizeof(idAfter)) || memcmp(handleIdentity, idAfter, sizeof(idAfter)) ||
            !CCPReadBytes(reader, handle + 0xA8, keyAfter, sizeof(keyAfter)) || memcmp(handleKey, keyAfter, sizeof(keyAfter))) {
            out->status = CCPEntityChanged; return;
        }
    }
    out->stable = true; out->status = CCPEntitySampled;
}
#endif
