#include <assert.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsVehicleRead.h"

static const uint64_t Base = UINT64_C(0x100000000);
enum { Pawn = 0x10000, Vehicle = 0x20000, Avatar = 0x30000, Table = 0x40000, Entries = 0x50000 };
typedef struct {
    uint8_t bytes[0x60000];
    unsigned entryReads, headerReads, chainReads, skinReads;
    bool changeEntry, changeHeader, changeVehicle, changeSkin, fail;
} Fixture;
static Fixture f;
static bool Read(void *context, uint64_t address, void *out, size_t length) {
    Fixture *state = context;
    if (state->fail || address < Base || address - Base > sizeof(state->bytes) ||
        length > sizeof(state->bytes) - (address - Base)) return false;
    memcpy(out, state->bytes + address - Base, length);
    if (address == Base + Entries && ++state->entryReads == 2 && state->changeEntry) ((uint8_t *)out)[4] ^= 1;
    if (address == Base + Avatar + CCP_VEHICLE_SYNC_OFFSET && ++state->headerReads == 2 && state->changeHeader)
        ((uint8_t *)out)[8] ^= 1;
    if (address == Base + Pawn + CCP_VEHICLE_OFFSET && ++state->chainReads == 3 && state->changeVehicle)
        memset(out, 0, length);
    if (address == Base + Vehicle + 0x20AC && ++state->skinReads == 2 && state->changeSkin) ((uint8_t *)out)[4] ^= 1;
    return true;
}
static void Put32(size_t offset, uint32_t value) { memcpy(f.bytes + offset, &value, 4); }
static void Put64(size_t offset, uint64_t value) { memcpy(f.bytes + offset, &value, 8); }
static void Setup(void) {
    memset(&f, 0, sizeof(f));
    Put64(Pawn + CCP_VEHICLE_OFFSET, Base + Vehicle);
    Put64(Vehicle + CCP_VEHICLE_AVATAR_OFFSET, Base + Avatar);
    Put64(Avatar + 0x20, Base + Vehicle); Put64(Avatar + 0x10B8, Base + Vehicle);
    Put64(Avatar, Base + Table); Put64(Table + 0xD18, Base + 0x47EE7C8); Put64(Table + 0x4C0, Base + 0x42E4F28);
    CCPArray array = {Base + Entries, 2, 2}; memcpy(f.bytes + Avatar + CCP_VEHICLE_SYNC_OFFSET, &array, sizeof(array));
    Put32(Vehicle + 0x20AC, 3306003); Put32(Vehicle + 0x20B0, 3306003); Put32(Vehicle + 0x20B8, 3306003);
    Put32(Avatar + 0x10A4, 3306003);
    Put32(Entries, 16); Put32(Entries + 4, 3306003); Put32(Entries + 0x1C, 7);
    Put32(Entries + CCP_VEHICLE_STRIDE, 16); Put32(Entries + CCP_VEHICLE_STRIDE + 4, 0);
    Put32(Entries + CCP_VEHICLE_STRIDE + 0x1C, 8);
    memset(f.bytes + Entries + 8, 0xCD, 16);
}
static void Failed(CCPReader reader, CCPVehicleStatus expected) {
    CCPVehicleSample sample;
    assert(!CCPReadVehicle(reader, Base, Base + Pawn, &sample));
    assert(sample.status == expected && sample.count == 0 && sample.skin == 0);
}
int main(void) {
    CCPReader reader = {Read, &f}; CCPVehicleSample sample;
    Setup(); assert(CCPReadVehicle(reader, Base, Base + Pawn, &sample));
    assert(sample.status == CCPVehicleSampled && sample.count == 2 && sample.skin == 3306003);
    assert(sample.items[0].type == 16 && sample.items[0].item == 3306003 && sample.items[0].updateCounter == 7);
    assert(sample.items[1].item == 0 && sample.items[1].updateCounter == 8 && sample.defaultSkin == 3306003);
    assert(sample.onrepRVA == 0x47EE7C8 && sample.tickRVA == 0x42E4F28);
    Setup(); Put64(Table + 0xD18, Base + 0x881F248); assert(CCPReadVehicle(reader, Base, Base + Pawn, &sample));
    Setup(); Put64(Pawn + CCP_VEHICLE_OFFSET, 0); Failed(reader, CCPVehicleUnavailable);
    Setup(); Put64(Avatar + 0x20, Base + Pawn); Failed(reader, CCPVehicleOwnerUnconfirmed);
    Setup(); Put64(Avatar + 0x10B8, Base + Pawn); Failed(reader, CCPVehicleOwnerUnconfirmed);
    Setup(); Put64(Table + 0xD18, Base + 0x43D2EB0); Failed(reader, CCPVehicleMethodsUnconfirmed);
    Setup(); Put64(Table + 0x4C0, Base + 0x43D6D10); Failed(reader, CCPVehicleMethodsUnconfirmed);
    const int32_t invalid[][2] = {{-1,2}, {0,0}, {17,17}, {2,1}, {2,129}};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        Setup(); CCPArray array = {Base + Entries, invalid[i][0], invalid[i][1]};
        memcpy(f.bytes + Avatar + CCP_VEHICLE_SYNC_OFFSET, &array, sizeof(array));
        Failed(reader, CCPVehicleArrayUnconfirmed); assert(f.entryReads == 0);
    }
    Setup(); f.changeEntry = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeHeader = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeVehicle = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.changeSkin = true; Failed(reader, CCPVehicleChanged);
    Setup(); f.fail = true; Failed(reader, CCPVehicleUnavailable);
    puts("PASS: vehicle owner/method gates, independent stride, bounded arrays and changing vehicle/data rejection");
    return 0;
}
