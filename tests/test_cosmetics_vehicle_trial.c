#include <assert.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsVehicleTrial.h"

static const uint64_t Base = UINT64_C(0x100000000);
enum { Pawn = 0x10000, Controller = 0x20000, User = 0x30000, Vehicle = 0x40000,
    Avatar = 0x50000, Entries = 0x60000, Cached = 0x61000, Components = 0x62000,
    ControllerClass = 0x70000, ControllerAncestors = 0x71000, UserClass = 0x72000,
    UserAncestors = 0x73000, PawnTable = 0x74000 };
typedef struct {
    uint8_t bytes[0x80000];
    bool bound, consume, failAfter, changeMasterAfterWrite;
    unsigned writes, failWrite, unbindWrite;
    uint64_t onrep;
} Fixture;
static Fixture f;
static void Put32(size_t offset, uint32_t value) { memcpy(f.bytes + offset, &value, 4); }
static void Put64(size_t offset, uint64_t value) { memcpy(f.bytes + offset, &value, 8); }
static uint32_t Get32(size_t offset) { return CCPU32(f.bytes + offset); }
static bool Read(void *context, uint64_t address, void *out, size_t length) {
    (void)context;
    uint64_t special = 0;
    if (address == Base + CCP_PLAYER_CONTROLLER_CLASS_RVA) special = Base + ControllerClass;
    else if (address == Base + CCP_VEHICLE_USER_CLASS_RVA) special = Base + UserClass;
    else if (address == Base + 0xE6D6A38 + 0xD18) special = f.onrep;
    else if (address == Base + 0xE6D6A38 + 0x4C0) special = Base + 0x42E4F28;
    if (special) { assert(length == 8); memcpy(out, &special, 8); return true; }
    if (address < Base || address - Base > sizeof(f.bytes) || length > sizeof(f.bytes) - (address - Base)) return false;
    memcpy(out, f.bytes + address - Base, length); return true;
}
static bool Bound(void *context) { (void)context; return f.bound; }
static CCPNativeWriteResult Write(void *context, uint64_t address, const void *expected, const void *replacement, size_t length) {
    (void)context;
    assert(length == 4 && (address == Base + Entries + 4 || address == Base + Entries + 0x1C || address == Base + Vehicle + 0x20AC));
    f.writes++;
    uint8_t *destination = f.bytes + address - Base;
    if (memcmp(destination, expected, 4) || (f.failWrite == f.writes && !f.failAfter))
        return (CCPNativeWriteResult){.state = 5};
    memcpy(destination, replacement, 4);
    if (f.consume && address == Base + Vehicle + 0x20AC && CCPU32(replacement)) {
        uint32_t id = CCPU32(replacement);
        Put32(Vehicle + 0x20B4, Get32(Vehicle + 0x20B0));
        Put32(Vehicle + 0x20B0, id); Put32(Vehicle + 0x20B8, id); Put32(Vehicle + 0x20AC, 0);
        Put32(Cached, Get32(Entries + 0x1C));
    }
    if (f.changeMasterAfterWrite && f.writes == 1) Put32(Entries + 4, 9994999);
    if (f.unbindWrite == f.writes) f.bound = false;
    return (CCPNativeWriteResult){.state = (f.failWrite == f.writes ? 5 : 1) | (UINT64_C(4) << 32) | (UINT64_C(1) << 40)};
}
static CCPVehicleTrial Setup(void) {
    memset(&f, 0, sizeof(f)); f.bound = true; f.onrep = Base + 0x47EE7C8;
    Put64(Pawn, Base + PawnTable); Put64(PawnTable + 0x1528, Base + 0x4C1158C);
    Put64(Pawn + 0x60C8, Base + Controller); // Actual r8 device: +0x608 is zero.
    Put64(Controller, Base + PawnTable + 0x2000); Put64(Controller + 0x3540, Base + Pawn);
    Put64(Controller + 0x10, Base + ControllerClass);
    Put64(ControllerClass + 0x88, Base + ControllerAncestors); Put32(ControllerClass + 0x90, 2);
    Put64(ControllerAncestors + 16, Base + ControllerClass + 0x88);
    Put64(User + 0x10, Base + UserClass); Put64(UserClass + 0x88, Base + UserAncestors); Put32(UserClass + 0x90, 2);
    Put64(UserAncestors + 16, Base + UserClass + 0x88);
    Put64(User + 0x20, Base + Controller); Put64(User + 0x168, Base + Pawn); Put64(User + 0x158, Base + Vehicle);
    Put64(Controller + 0x440, Base + Components); Put32(Controller + 0x448, 39); Put32(Controller + 0x44C, 47);
    Put32(Controller + 0x450, UINT32_MAX); Put32(Controller + 0x454, 127);
    Put32(Controller + 0x468, 39); Put32(Controller + 0x46C, 128); Put64(Components + 18 * 16, Base + User);
    Put64(Vehicle, Base + 0xEBC1680); Put64(Vehicle + 0x1ED8, Base + Avatar);
    Put64(Avatar, Base + 0xE6D6A38); Put64(Avatar + 0x20, Base + Vehicle); Put64(Avatar + 0x10B8, Base + Vehicle);
    CCPArray sync = {Base + Entries, 11, 11}, cached = {Base + Cached, 11, 11};
    memcpy(f.bytes + Avatar + 0x11E8, &sync, sizeof(sync)); memcpy(f.bytes + Avatar + 0x1208, &cached, sizeof(cached));
    Put32(Entries, 33); Put32(Entries + 4, CCP_VEHICLE_ORIGINAL); Put32(Entries + 0x1C, 1); Put32(Cached, 1);
    memset(f.bytes + Entries + 8, 0xAB, 16);
    Put32(Vehicle + 0x20B0, CCP_VEHICLE_ORIGINAL); Put32(Vehicle + 0x20B4, CCP_VEHICLE_ORIGINAL);
    Put32(Vehicle + 0x20B8, CCP_VEHICLE_ORIGINAL); Put32(Avatar + 0x10A4, CCP_VEHICLE_ORIGINAL);
    return (CCPVehicleTrial){.game = Base, .pawn = Base + Pawn, .controller = Base + Controller};
}
static CCPVehicleTrialResult Apply(CCPVehicleTrial *t) {
    return CCPApplyVehicleTrial((CCPReader){Read, &f}, Write, &f, Bound, t);
}
static CCPVehicleTrialResult Restore(CCPVehicleTrial *t) {
    return CCPRestoreVehicleTrial((CCPReader){Read, &f}, Write, &f, Bound, t);
}
static CCPVehicleTrialResult Finish(CCPVehicleTrial *t, bool cancel) {
    return CCPFinishVehicleRestore((CCPReader){Read, &f}, Write, &f, Bound, t, cancel);
}
int main(void) {
    CCPVehicleTrial t = Setup(); CCPVehicleTrialResult r = Apply(&t);
    assert(r.status == CCPVehicleTrialApplied && r.idConfirmed && r.requestWritten && r.totalWrittenBytes == 12 && t.active);
    assert(Get32(Entries + 4) == CCP_VEHICLE_TARGET && Get32(Entries + 0x1C) == 2 && Get32(Vehicle + 0x20AC) == CCP_VEHICLE_TARGET);
    assert(Get32(Vehicle + 0x20B0) == CCP_VEHICLE_ORIGINAL); // Test does not pretend a field write implies native refresh.
    r = Restore(&t); assert(r.status == CCPVehicleTrialRestoring && r.idConfirmed && r.requestWritten);
    assert(Get32(Entries + 4) == CCP_VEHICLE_ORIGINAL && Get32(Entries + 0x1C) == 3);
    r = Finish(&t, false); assert(t.active && !r.dataRestored);
    r = Finish(&t, true); assert(!t.active && r.dataRestored && !r.nativeRestoreObserved && r.status == CCPVehicleTrialDataRestored);
    assert(Get32(Vehicle + 0x20AC) == 0 && Get32(Entries + 0x1C) == 3);
    t = Setup(); f.consume = true; r = Apply(&t); assert(r.requestWritten && t.active && Get32(Vehicle + 0x20AC) == 0);
    CCPVehicleView view; assert(CCPVehicleTrialLayout((CCPReader){Read, &f}, &t, &view));
    assert(view.skin == CCP_VEHICLE_TARGET && view.cachedCounter == 2); t.engineApplyObserved = true;
    r = Restore(&t); assert(r.requestWritten); r = Finish(&t, false);
    assert(r.dataRestored && r.nativeRestoreObserved && !t.active && Get32(Vehicle + 0x20B0) == CCP_VEHICLE_ORIGINAL);
    t = Setup(); Put32(Entries + 4, 3306003); r = Apply(&t); assert(r.status == CCPVehicleTrialWrongVehicle && !f.writes);
    t = Setup(); Put32(Entries, 16); r = Apply(&t); assert(r.status == CCPVehicleTrialLayoutChanged && !f.writes);
    t = Setup(); f.onrep = Base + 0x43D2EB0; r = Apply(&t); assert(r.status == CCPVehicleTrialLayoutChanged && !f.writes);
    t = Setup(); Put32(Vehicle + 0x20AC, 123); r = Apply(&t); assert(r.status == CCPVehicleTrialWrongVehicle && !f.writes);
    t = Setup(); Put32(Avatar + 0x1210, 0); r = Apply(&t); assert(r.status == CCPVehicleTrialLayoutChanged && !f.writes);
    t = Setup(); Put32(Cached, 2); r = Apply(&t); assert(r.requestWritten && f.writes == 3);
    t = Setup(); Put32(Entries + 0x1C, UINT32_MAX); r = Apply(&t); assert(r.status == CCPVehicleTrialLayoutChanged && !f.writes);
    t = Setup(); f.bound = false; r = Apply(&t); assert(r.status == CCPVehicleTrialBindingChanged && !f.writes);
    for (unsigned failure = 1; failure <= 3; failure++) {
        t = Setup(); f.failWrite = failure; r = Apply(&t); assert(!r.requestWritten && r.status == CCPVehicleTrialWriteFailed);
        f.failWrite = 0; r = Restore(&t); if (t.active) r = Finish(&t, true);
        assert(!t.active && Get32(Entries + 4) == CCP_VEHICLE_ORIGINAL && Get32(Vehicle + 0x20AC) == 0);
    }
    t = Setup(); f.failWrite = 3; f.failAfter = true; r = Apply(&t);
    assert(r.status == CCPVehicleTrialWriteFailed && t.active); f.failWrite = 0;
    r = Restore(&t); r = Finish(&t, true); assert(r.dataRestored && !t.active);
    t = Setup(); r = Apply(&t); Put32(Entries + 4, 9994999); unsigned before = f.writes;
    r = Restore(&t); assert(r.status == CCPVehicleTrialGameChanged && !t.active && f.writes == before && Get32(Entries + 4) == 9994999);
    t = Setup(); r = Apply(&t); Put32(Entries + 0x1C, 3); before = f.writes;
    r = Restore(&t); assert(r.status == CCPVehicleTrialGameChanged && !t.active && f.writes == before);
    t = Setup(); f.changeMasterAfterWrite = true; r = Apply(&t); before = f.writes;
    r = Restore(&t); assert(r.status == CCPVehicleTrialGameChanged && f.writes == before);
    t = Setup(); r = Apply(&t); f.bound = false; before = f.writes;
    r = Restore(&t); assert(r.status == CCPVehicleTrialBindingChanged && t.active && f.writes == before);
    t = Setup(); r = Apply(&t); Put64(Components + 18 * 16, Base + User + 0x1000); before = f.writes;
    r = Restore(&t); assert(r.status == CCPVehicleTrialLayoutChanged && t.active && f.writes == before);
    t = Setup(); r = Apply(&t); f.bytes[Entries + 8] ^= 1; before = f.writes;
    r = Restore(&t); assert(r.status == CCPVehicleTrialLayoutChanged && t.active && f.writes == before);
    t = Setup(); f.unbindWrite = 1; r = Apply(&t);
    assert(r.status == CCPVehicleTrialBindingChanged && f.writes == 1 && t.active);
    puts("PASS: exact r8 vehicle/type gate, three-field CAS journal, monotonic counter, native-consumed/unconsumed requests, scoped rollback and source/game takeover guards");
    return 0;
}
