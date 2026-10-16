#include <assert.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsVehicleRead.h"

static const uint64_t Base = UINT64_C(0x100000000);
enum { Pawn = 0x10000, Vehicle = 0x20000, Avatar = 0x30000, Table = 0x40000,
    Entries = 0x50000, User = 0x58000, UserClass = 0x60000, UserAncestors = 0x61000,
    ControllerClass = 0x62000, ControllerAncestors = 0x63000, Controller = 0x68000,
    Components = 0x6E000, HeapFlags = 0x70000 };
typedef struct {
    uint8_t bytes[0x80000];
    unsigned entryReads, headerReads, chainReads, componentReads, skinReads, controllerReads, sourceReads, bitmapReads, legacyReads;
    uint64_t failAddress, userClass, controllerClass;
    bool changeEntry, changeHeader, changeVehicle, changeUser, changeSkin, changeController, changeSource, changeBitmap, fail;
} Fixture;
static Fixture f;
static bool Read(void *context, uint64_t address, void *out, size_t length) {
    Fixture *state = context;
    if (state->fail || address == state->failAddress) return false;
    if (address == Base + CCP_PLAYER_CONTROLLER_CLASS_RVA && length == 8) {
        memcpy(out, &state->controllerClass, 8); return true;
    }
    if (address == Base + CCP_VEHICLE_USER_CLASS_RVA && length == 8) {
        memcpy(out, &state->userClass, 8); return true;
    }
    if (address < Base || address - Base > sizeof(state->bytes) ||
        length > sizeof(state->bytes) - (address - Base)) return false;
    memcpy(out, state->bytes + address - Base, length);
    if (address == Base + Pawn + 0x47F8 || address == Base + Controller + 0x47F8) state->legacyReads++;
    if (address == Base + Entries && ++state->entryReads == 2 && state->changeEntry) ((uint8_t *)out)[4] ^= 1;
    if (address == Base + Avatar + CCP_VEHICLE_SYNC_OFFSET && ++state->headerReads == 2 && state->changeHeader)
        ((uint8_t *)out)[8] ^= 1;
    if (address == Base + User + CCP_VEHICLE_USER_CURRENT_OFFSET && ++state->chainReads == 2 && state->changeVehicle)
        memset(out, 0, length);
    if (address == Base + Components && ++state->componentReads == 3 && state->changeUser) memset(out, 0, length);
    if (address == Base + Controller + 0x3540 && ++state->controllerReads == 2 && state->changeController)
        memset(out, 0, length);
    if (address == Base + Pawn + 0x608 && ++state->sourceReads == 2 && state->changeSource) memset(out, 0, length);
    if (address == Base + Controller + 0x450 && ++state->bitmapReads == 2 && state->changeBitmap) ((uint8_t *)out)[0] ^= 8;
    if (address == Base + Vehicle + 0x20AC && ++state->skinReads == 2 && state->changeSkin) ((uint8_t *)out)[4] ^= 1;
    return true;
}
static void Put32(size_t offset, uint32_t value) { memcpy(f.bytes + offset, &value, 4); }
static void Put64(size_t offset, uint64_t value) { memcpy(f.bytes + offset, &value, 8); }
static void SetComponents(unsigned slots, unsigned index, bool heap) {
    memset(f.bytes + Components, 0, CCP_MAX_COMPONENT_SLOTS * 16);
    memset(f.bytes + HeapFlags, 0, CCP_MAX_COMPONENT_SLOTS / 8);
    memset(f.bytes + Controller + 0x450, 0, 0x30);
    Put64(Controller + 0x440, Base + Components);
    Put32(Controller + 0x448, slots); Put32(Controller + 0x44C, slots);
    Put64(Controller + 0x460, heap ? Base + HeapFlags : 0);
    Put32(Controller + 0x468, slots); Put32(Controller + 0x46C, heap ? 512 : 128);
    if (index < slots) {
        Put64(Components + index * 16, Base + User);
        Put32((heap ? HeapFlags : Controller + 0x450) + index / 32 * 4, UINT32_C(1) << (index % 32));
    }
}
static void Setup(void) {
    memset(&f, 0, sizeof(f));
    f.controllerClass = Base + ControllerClass; f.userClass = Base + UserClass;
    Put64(Pawn, Base + Table + 0x4000); Put64(Table + 0x4000 + 0x1528, Base + 0x4C1158C);
    Put64(Pawn + 0x608, Base + Controller); Put64(Controller + 0x3540, Base + Pawn);
    Put64(Controller, Base + Table + 0x6000); Put64(Controller + 0x10, Base + ControllerClass);
    Put64(ControllerClass + 0x88, Base + ControllerAncestors); Put32(ControllerClass + 0x90, 2);
    Put64(ControllerAncestors + 16, Base + ControllerClass + 0x88);
    Put64(User + 0x10, Base + UserClass); Put64(UserClass + 0x88, Base + UserAncestors);
    Put32(UserClass + 0x90, 2); Put64(UserAncestors + 16, Base + UserClass + 0x88);
    Put64(User + 0x20, Base + Controller); Put64(User + 0x168, Base + Pawn);
    Put64(User, Base + Table + 0x2800);
    Put64(User + CCP_VEHICLE_USER_CURRENT_OFFSET, Base + Vehicle);
    SetComponents(5, 3, false);
    Put64(Components, 123); // Unoccupied poisoned slot must not be dereferenced.
    Put64(Vehicle + CCP_VEHICLE_AVATAR_OFFSET, Base + Avatar);
    Put64(Vehicle, Base + Table + 0x2000);
    Put64(Avatar + 0x20, Base + Vehicle); Put64(Avatar + 0x10B8, Base + Vehicle);
    Put64(Avatar, Base + Table); Put64(Table + 0xD18, Base + 0x47EE7C8); Put64(Table + 0x4C0, Base + 0x42E4F28);
    CCPArray array = {Base + Entries, 2, 2}; memcpy(f.bytes + Avatar + CCP_VEHICLE_SYNC_OFFSET, &array, sizeof(array));
    Put32(Vehicle + 0x20AC, 3306003); Put32(Vehicle + 0x20B0, 3306003); Put32(Vehicle + 0x20B8, 3306003);
    Put32(Avatar + 0x10A4, 3306003);
    Put32(Entries, 16); Put32(Entries + 4, 3306003); Put32(Entries + 0x1C, 7);
    Put32(Entries + CCP_VEHICLE_STRIDE, 16); Put32(Entries + CCP_VEHICLE_STRIDE + 0x1C, 8);
    memset(f.bytes + Entries + 8, 0xCD, 16);
}
static bool Sample(CCPReader reader, CCPVehicleSample *sample) {
    return CCPReadVehicle(reader, Base, Base + Pawn, Base + Controller, sample);
}
static void Failed(CCPReader reader, CCPVehicleStatus expected) {
    CCPVehicleSample sample;
    assert(!Sample(reader, &sample));
    assert(sample.status == expected && sample.count == 0 && sample.skin == 0);
}
int main(void) {
    CCPReader reader = {Read, &f}; CCPVehicleSample sample;
    Setup(); assert(Sample(reader, &sample));
    assert(sample.status == CCPVehicleSampled && sample.count == 2 && sample.skin == 3306003);
    assert(sample.items[0].type == 16 && sample.items[0].item == 3306003 && sample.items[0].updateCounter == 7);
    assert(sample.items[1].item == 0 && sample.items[1].updateCounter == 8 && sample.defaultSkin == 3306003);
    assert(sample.onrepRVA == 0x47EE7C8 && sample.tickRVA == 0x42E4F28);
    assert(sample.vehicleTableRVA == Table + 0x2000 && sample.avatarTableRVA == Table);
    assert(sample.userTableRVA == Table + 0x2800 && sample.userAvailable && sample.userOwnerMatches);
    assert(sample.sourceMatchesController && sample.controllerPawnMatches && sample.primaryClassMatches && !sample.usedFallback);
    assert(sample.components.stable && sample.components.slots == 5 && sample.components.occupied == 1);
    assert(sample.vehicleFieldRead && sample.vehicleNonzero && sample.vehiclePointerValid && sample.userCharacterMatches);
    assert(f.legacyReads == 0);
    // A stock vehicle may have no appearance sync entries or paid skin ID.
    // Read-only capture must still preserve its scalar fields and ownership.
    Setup(); CCPArray empty = {0}; memcpy(f.bytes + Avatar + CCP_VEHICLE_SYNC_OFFSET, &empty, sizeof(empty));
    memset(f.bytes + Vehicle + 0x20AC, 0, 16); Put32(Avatar + 0x10A4, 0);
    assert(Sample(reader, &sample) && sample.count == 0 && sample.skin == 0 && sample.defaultSkin == 0);
    assert(f.entryReads == 0 && sample.userCharacterMatches);
    // The legacy caches stay empty. Misleading nonzero values must be ignored too.
    Setup(); Put64(Pawn + 0x47F8, Base + Entries); Put64(Controller + 0x47F8, Base + Entries);
    Put64(Pawn + 0x3710, Base + Entries); assert(Sample(reader, &sample)); assert(f.legacyReads == 0);
    Setup(); SetComponents(160, 159, true); assert(Sample(reader, &sample)); assert(sample.components.heapBitmap);
    Setup(); SetComponents(512, 511, true); assert(Sample(reader, &sample)); assert(sample.components.slots == 512);
    Setup(); Put64(User + 0x10, Base + 0x64000); Put64(0x64088, Base + 0x65000);
    Put32(0x64090, 3); Put64(0x65010, Base + UserClass + 0x88); assert(Sample(reader, &sample));
    Setup(); Put64(Pawn + 0x608, 0); Put64(Pawn + 0x60C8, Base + Controller);
    assert(Sample(reader, &sample) && sample.usedFallback);
    Setup(); Put64(Pawn + 0x608, Base + User); Put64(Pawn + 0x60C8, Base + Controller);
    assert(Sample(reader, &sample) && sample.usedFallback && !sample.primaryClassMatches);
    Setup(); Put64(Table + 0xD18, Base + 0x881F248); assert(Sample(reader, &sample));
    Setup(); Put64(Table + 0x4000 + 0x1528, Base + 0x43D2EB0); Failed(reader, CCPVehicleResolverUnconfirmed);
    Setup(); f.controllerClass = 0; Failed(reader, CCPVehicleControllerClassUnconfirmed);
    Setup(); Put64(Controller + 0x3540, Base + Vehicle); Failed(reader, CCPVehicleSourceActorUnconfirmed);
    Setup(); Put64(Pawn + 0x608, 0); Failed(reader, CCPVehicleSourceActorUnconfirmed);
    Setup(); Put64(Pawn + 0x608, 123); Failed(reader, CCPVehicleSourceReadFailed);
    Setup(); Put64(Pawn + 0x608, Base + User); Failed(reader, CCPVehicleSourceActorUnconfirmed);
    Setup(); Put64(Vehicle + 0x10, Base + ControllerClass); Put64(Pawn + 0x608, Base + Vehicle);
    Put64(Pawn + 0x60C8, Base + Controller); Failed(reader, CCPVehicleSourceActorUnconfirmed);
    Setup(); f.userClass = 0; Failed(reader, CCPVehicleUserClassUnconfirmed);
    Setup(); SetComponents(0, 0, false); Failed(reader, CCPVehicleSourceUnavailable);
    Setup(); Put32(Controller + 0x450, 0); Failed(reader, CCPVehicleSourceUnavailable);
    Setup(); Put64(UserAncestors + 16, Base + ControllerClass + 0x88); Failed(reader, CCPVehicleSourceUnavailable);
    Setup(); Put32(UserClass + 0x90, UINT32_MAX); Failed(reader, CCPVehicleComponentsReadFailed);
    Setup(); Put32(Controller + 0x468, 6); Failed(reader, CCPVehicleComponentsInvalid);
    Setup(); Put32(Controller + 0x448, 513); Failed(reader, CCPVehicleComponentsInvalid);
    Setup(); SetComponents(160, 159, false); Failed(reader, CCPVehicleComponentsInvalid);
    Setup(); Put32(Controller + 0x46C, 4); Failed(reader, CCPVehicleComponentsInvalid);
    Setup(); Put64(Controller + 0x460, 123); Failed(reader, CCPVehicleComponentsInvalid);
    Setup(); Put64(Controller + 0x440, 123); Failed(reader, CCPVehicleComponentsInvalid);
    Setup(); Put32(Controller + 0x44C, 4); Failed(reader, CCPVehicleComponentsInvalid);
    Setup(); f.failAddress = Base + Controller + 0x450; Failed(reader, CCPVehicleComponentsReadFailed);
    Setup(); f.changeBitmap = true; Failed(reader, CCPVehicleComponentsChanged);
    Setup(); Put64(Components + 16, Base + User); Put32(Controller + 0x450, 10); Failed(reader, CCPVehicleComponentsAmbiguous);
    Setup(); f.failAddress = Base + User + 0x10; Failed(reader, CCPVehicleComponentsReadFailed);
    Setup(); Put64(User + 0x20, Base + Pawn); Failed(reader, CCPVehicleSourceOwnerUnconfirmed);
    Setup(); Put64(User + 0x168, 0); Failed(reader, CCPVehicleCharacterUnconfirmed);
    Setup(); f.failAddress = Base + User + CCP_VEHICLE_USER_CURRENT_OFFSET; Failed(reader, CCPVehiclePointerReadFailed);
    Setup(); Put64(User + CCP_VEHICLE_USER_CURRENT_OFFSET, 0); Failed(reader, CCPVehicleUnavailable);
    Setup(); Put64(User + CCP_VEHICLE_USER_CURRENT_OFFSET, 123); Failed(reader, CCPVehiclePointerInvalid);
    Setup(); Put64(Avatar + 0x20, Base + Pawn); Failed(reader, CCPVehicleOwnerUnconfirmed);
    Setup(); Put64(Avatar + 0x10B8, Base + Pawn); Failed(reader, CCPVehicleOwnerUnconfirmed);
    Setup(); Put64(Table + 0xD18, Base + 0x43D2EB0); Failed(reader, CCPVehicleMethodsUnconfirmed);
    Setup(); Put64(Table + 0x4C0, Base + 0x43D6D10); Failed(reader, CCPVehicleMethodsUnconfirmed);
    const int32_t invalid[][2] = {{-1,2}, {0,-1}, {17,17}, {2,1}, {2,129}};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        Setup(); CCPArray array = {Base + Entries, invalid[i][0], invalid[i][1]};
        memcpy(f.bytes + Avatar + CCP_VEHICLE_SYNC_OFFSET, &array, sizeof(array));
        Failed(reader, CCPVehicleArrayUnconfirmed); assert(f.entryReads == 0);
    }
    Setup(); f.changeEntry = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeHeader = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeVehicle = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeUser = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeSkin = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeController = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeSource = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.fail = true; Failed(reader, CCPVehicleResolverUnconfirmed);
    puts("PASS: controller resolution, native class ancestry, sparse inline/heap components, ownership and concurrent source/array changes");
    return 0;
}
