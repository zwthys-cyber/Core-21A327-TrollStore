#include <assert.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsVehicleEntity.h"

#define GAME UINT64_C(0x100000000)
#define AVATAR UINT64_C(0x200000000)
#define ENTITY UINT64_C(0x200004000)
#define HANDLE UINT64_C(0x200008000)
#define BASE_CLASS UINT64_C(0x300000000)
#define VEHICLE_CLASS UINT64_C(0x300004000)
#define MASTER_CLASS UINT64_C(0x300008000)
#define ITEM_CLASS UINT64_C(0x30000C000)
#define ANCESTORS UINT64_C(0x300010000)
#define ITEM_ANCESTORS UINT64_C(0x300014000)
#define ENTRIES UINT64_C(0x400000000)
#define BITMAP UINT64_C(0x400004000)
#define HASH UINT64_C(0x400008000)
#define CACHE UINT64_C(0x40000C000)
#define TABLE (GAME + 0xE67A9B8)
#define MAP (AVATAR + CCP_ENTITY_MAP_OFFSET)
typedef struct { uint64_t address; size_t length; uint8_t bytes[0x2000]; } Region;
static Region regions[20];
static unsigned regionCount, reads, keyReads, mutationAt, mutationReads;
static uint64_t mutationAddress;
static size_t maximumRead;
static void Add(uint64_t address, size_t length) {
    assert(regionCount < sizeof(regions) / sizeof(regions[0]) && length <= sizeof(regions[0].bytes));
    regions[regionCount++] = (Region){.address = address, .length = length};
}
static uint8_t *At(uint64_t address, size_t length) {
    for (unsigned i = 0; i < regionCount; i++)
        if (address >= regions[i].address && address - regions[i].address <= regions[i].length &&
            length <= regions[i].length - (size_t)(address - regions[i].address))
            return regions[i].bytes + (size_t)(address - regions[i].address);
    return NULL;
}
static void U32(uint64_t address, uint32_t value) { uint8_t *p = At(address, 4); assert(p); memcpy(p, &value, 4); }
static void U64(uint64_t address, uint64_t value) { uint8_t *p = At(address, 8); assert(p); memcpy(p, &value, 8); }
static bool Read(void *context, uint64_t address, void *output, size_t length) {
    (void)context; reads++; assert(length <= 16384);
    if (length > maximumRead) maximumRead = length;
    if (address == HANDLE + 0xA8) keyReads++;
    uint8_t *p = At(address, length);
    if (!p) return false;
    if (address == mutationAddress && ++mutationReads == mutationAt) p[0] ^= 1;
    memcpy(output, p, length); return true;
}
static void Fixture(void) {
    memset(regions, 0, sizeof(regions)); regionCount = reads = keyReads = 0;
    mutationAddress = 0; mutationAt = mutationReads = 0; maximumRead = 0;
    Add(AVATAR, 0x2000); Add(ENTITY, 0x500); Add(HANDLE, 0x200);
    Add(BASE_CLASS, 0x100); Add(VEHICLE_CLASS, 0x100); Add(MASTER_CLASS, 0x100); Add(ITEM_CLASS, 0x100);
    Add(ANCESTORS, 24); Add(ITEM_ANCESTORS, 8); Add(ENTRIES, 4 * 24); Add(BITMAP, 16); Add(HASH, 16); Add(CACHE, 4);
    Add(GAME + CCP_ENTITY_BASE_CLASS_RVA, 8); Add(GAME + CCP_ENTITY_VEHICLE_CLASS_RVA, 8);
    Add(GAME + CCP_ENTITY_MASTER_CLASS_RVA, 8); Add(GAME + CCP_ENTITY_ITEM_CLASS_RVA, 8); Add(TABLE, 0x600);
    U64(GAME + CCP_ENTITY_BASE_CLASS_RVA, BASE_CLASS); U64(GAME + CCP_ENTITY_VEHICLE_CLASS_RVA, VEHICLE_CLASS);
    U64(GAME + CCP_ENTITY_MASTER_CLASS_RVA, MASTER_CLASS); U64(GAME + CCP_ENTITY_ITEM_CLASS_RVA, ITEM_CLASS);
    const uint64_t classes[] = {BASE_CLASS, VEHICLE_CLASS, MASTER_CLASS};
    for (unsigned i = 0; i < 3; i++) {
        U64(classes[i] + 0x88, ANCESTORS); U32(classes[i] + 0x90, i);
        U64(ANCESTORS + i * 8, classes[i] + 0x88);
    }
    U64(ITEM_CLASS + 0x88, ITEM_ANCESTORS); U64(ITEM_ANCESTORS, ITEM_CLASS + 0x88);
    U64(MAP, ENTRIES); U32(MAP + 8, 3); U32(MAP + 12, 4);
    U32(MAP + 0x10, 5); U32(MAP + 0x28, 3); U32(MAP + 0x2C, 128);
    U32(MAP + 0x30, 1); U32(MAP + 0x34, 1); U32(MAP + 0x38, 0); U32(MAP + 0x48, 4);
    U32(ENTRIES, 0); U64(ENTRIES + 8, ENTITY); U32(ENTRIES + 0x10, UINT32_MAX);
    // Deleted slot intentionally contains a second key 0 and an invalid value.
    // It must never be followed or counted as an ambiguous live entity.
    U32(ENTRIES + 24, 0); U64(ENTRIES + 24 + 8, 3);
    U32(ENTRIES + 48, 9); U32(ENTRIES + 48 + 0x10, UINT32_MAX);
    U64(ENTITY, TABLE); U64(ENTITY + 0x10, MASTER_CLASS); U64(ENTITY + 0x20, AVATAR);
    U64(ENTITY + 0x30, HANDLE); U64(ENTITY + 0x38, HANDLE); U64(ENTITY + 0x48, AVATAR);
    U64(ENTITY + 0x50, AVATAR + 0x4000); U64(ENTITY + 0x258, 123); U64(ENTITY + 0x3B8, 456);
    U64(HANDLE + 0x10, ITEM_CLASS); U32(HANDLE + 0xA8, 33); U32(HANDLE + 0xAC, 3349001);
    U64(TABLE + 0x3C0, GAME + 0x45DABBC); U64(TABLE + 0x478, GAME + 0x45E012C); U64(TABLE + 0x4E0, GAME + 0x42F1AF4);
    U64(AVATAR + 0x1208, CACHE); U32(AVATAR + 0x1210, 1); U32(AVATAR + 0x1214, 1); U32(CACHE, 1);
}
static CCPVehicleEntityDiagnostic Sample(void) {
    CCPVehicleEntityDiagnostic out;
    CCPReadVehicleEntity((CCPReader){Read, NULL}, GAME, AVATAR, CCP_ENTITY_ITEM_CLASS_RVA, &out);
    assert(maximumRead <= CCP_ENTITY_MAX_SLOTS * 24);
    return out;
}
int main(void) {
    Fixture(); CCPVehicleEntityDiagnostic out = Sample();
    assert(out.status == CCPEntitySampled && out.stable && out.mapSlots == 3 && out.mapOccupied == 2 && out.masterMatches == 1);
    assert(out.hashMatches && out.ownerMatches && out.baseClassMatches && out.vehicleClassMatches && out.masterClassMatches);
    assert(out.handleKeyRead && out.loadedType == 33 && out.loadedID == 3349001 && out.cachedMatchesLoaded);
    assert(out.meshComponentAvailable && out.meshPathPresent && out.lastMeshPathPresent && out.renderFlagsRead);
    assert(out.cachedCounterRead && out.cachedCounterCount == 1 && out.cachedCounter == 1);
    assert(out.tableRVA == 0xE67A9B8 && out.installRVA == 0x45DABBC && out.behaviorRVA == 0x42F1AF4);

    Fixture(); U64(MAP + 0x20, BITMAP); U32(BITMAP, 5); U64(MAP + 0x40, HASH); U32(HASH, 0);
    out = Sample(); assert(out.status == CCPEntitySampled && out.heapBitmap && out.hashMatches);

    Fixture(); U32(MAP + 0x10, 4); U32(MAP + 0x34, 2); U32(MAP + 0x38, UINT32_MAX);
    out = Sample(); assert(out.status == CCPEntityNotFound && out.stable && keyReads == 0);
    Fixture(); memset(At(MAP, 0x50), 0, 0x50);
    out = Sample(); assert(out.status == CCPEntityNotFound && out.stable);

    Fixture(); U32(MAP + 0x10, 7); U32(MAP + 0x34, 0);
    out = Sample(); assert(out.status == CCPEntityAmbiguous && keyReads == 0);
    Fixture(); U32(MAP + 0x38, 31);
    out = Sample(); assert(out.status == CCPEntityMapInvalid && keyReads == 0);
    Fixture(); U32(MAP + 0x38, 2); U32(ENTRIES + 48 + 0x10, 2);
    out = Sample(); assert(out.status == CCPEntityMapInvalid && reads < 30); // Bounded cyclic hash chain.
    Fixture(); U32(MAP + 0x34, 0);
    out = Sample(); assert(out.status == CCPEntityMapInvalid);
    Fixture(); U32(MAP + 8, CCP_ENTITY_MAX_SLOTS + 1);
    out = Sample(); assert(out.status == CCPEntityMapInvalid && keyReads == 0);
    Fixture(); U32(MAP + 0x48, 3);
    out = Sample(); assert(out.status == CCPEntityMapInvalid);

    Fixture(); mutationAddress = ENTRIES; mutationAt = 2;
    out = Sample(); assert(out.status == CCPEntityChanged && keyReads == 0);
    Fixture(); mutationAddress = MAP + 0x38; mutationAt = 2;
    out = Sample(); assert(out.status == CCPEntityChanged && keyReads == 0);
    Fixture(); U64(ENTITY + 0x10, ITEM_CLASS);
    out = Sample(); assert(out.status == CCPEntityClassUnconfirmed && keyReads == 0);
    Fixture(); U64(GAME + CCP_ENTITY_BASE_CLASS_RVA, 0);
    out = Sample(); assert(out.status == CCPEntityClassUnavailable);
    Fixture(); U64(ENTITY + 0x48, AVATAR + 8);
    out = Sample(); assert(out.status == CCPEntityOwnerUnconfirmed && keyReads == 0);
    Fixture(); mutationAddress = ENTITY + 0xC; mutationAt = 2;
    out = Sample(); assert(out.status == CCPEntityChanged);
    Fixture(); mutationAddress = HANDLE + 0xA8; mutationAt = 2;
    out = Sample(); assert(out.status == CCPEntityChanged);

    Fixture(); U64(GAME + CCP_ENTITY_ITEM_CLASS_RVA, BASE_CLASS);
    out = Sample(); assert(out.status == CCPEntitySampled && !out.handleClassMatches && !out.handleKeyRead && keyReads == 0);
    Fixture(); U64(ENTITY + 0x30, 0); U64(ENTITY + 0x38, 0); U64(ENTITY + 0x50, 0);
    out = Sample(); assert(out.status == CCPEntitySampled && !out.handleAvailable && !out.meshComponentAvailable && keyReads == 0);
    Fixture(); U64(ENTITY + 0x30, 3);
    out = Sample(); assert(out.status == CCPEntityFieldsInvalid && keyReads == 0);
    Fixture(); At(ENTITY + 0x1B9, 1)[0] = 2;
    out = Sample(); assert(out.status == CCPEntityFieldsInvalid);
    Fixture(); U32(ENTITY + 0x258 + 16, 513); U32(ENTITY + 0x258 + 20, 513);
    out = Sample(); assert(out.status == CCPEntityFieldsInvalid);
    Fixture(); U32(AVATAR + 0x1210, 0);
    out = Sample(); assert(out.status == CCPEntitySampled && !out.cachedCounterRead);
    puts("PASS: read-only native hash/bitmap entity lookup, class/owner gates, item keys, model-state bounds and changing identity");
    return 0;
}
