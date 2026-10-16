#include <assert.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsTrial.h"
static const uint64_t Game = UINT64_C(0x100000000), Pawn = UINT64_C(0x200001000);
static const uint64_t Avatar = UINT64_C(0x200006000), Table = UINT64_C(0x10E6189A8), Data = UINT64_C(0x200010000);
typedef struct {
    uint8_t pawn[0x4000], avatar[0x2300], table[0x1400], data[30 * CCP_ITEM_STRIDE], mode;
    bool bound, failWrite, failAfterWriting;
    unsigned idWrites, flagWrites;
    uint32_t slot, original, target;
} Fixture;
static void *Region(Fixture *f, uint64_t address, size_t size) {
    if (address == Game + CCP_REFRESH_MODE_RVA && size == 1) return &f->mode;
    const uint64_t bases[] = {Pawn, Avatar, Table, Data};
    const size_t lengths[] = {sizeof(f->pawn), sizeof(f->avatar), sizeof(f->table), sizeof(f->data)};
    void *buffers[] = {f->pawn, f->avatar, f->table, f->data};
    for (size_t i = 0; i < 4; i++)
        if (address >= bases[i] && address - bases[i] <= lengths[i] && size <= lengths[i] - (address - bases[i]))
            return (uint8_t *)buffers[i] + address - bases[i];
    return NULL;
}
static bool Read(void *context, uint64_t address, void *out, size_t size) {
    void *p = Region(context, address, size); if (!p) return false; memcpy(out, p, size); return true;
}
static bool Valid(void *context) { return ((Fixture *)context)->bound; }
static CCPNativeWriteResult Write(void *context, uint64_t address, const void *expected, const void *value, size_t size) {
    Fixture *f = context;
    if (!f->bound) return (CCPNativeWriteResult){3,0};
    if (size == 4) {
        assert(address == Data + f->slot * CCP_ITEM_STRIDE + 4);
        uint32_t a = CCPU32(expected), b = CCPU32(value);
        assert((a == f->original && b == f->target) || (a == f->target && b == f->original));
        f->idWrites++;
    } else {
        assert(size == 1 && address == Avatar + CCP_REFRESH_PENDING && *(const uint8_t *)expected == 0 && *(const uint8_t *)value == 1);
        f->flagWrites++;
    }
    if (f->failWrite) return (CCPNativeWriteResult){3,0};
    void *p = Region(f, address, size); assert(p);
    if (memcmp(p, expected, size)) return (CCPNativeWriteResult){2,0};
    memcpy(p, value, size);
    return (CCPNativeWriteResult){(f->failAfterWriting ? 5 : 1) | ((uint64_t)size << 32) | (UINT64_C(1) << 40),0};
}
static void Put64(void *p, uint64_t value) { memcpy(p, &value, 8); }
static void Reset(Fixture *f, CCPCoatTrial *trial) {
    memset(f, 0, sizeof(*f)); *trial = (CCPCoatTrial){.game=Game,.pawn=Pawn,.avatar=Avatar,
        .slot=CCP_COAT_SLOT,.target=CCP_TRIAL_COAT};
    f->slot = CCP_COAT_SLOT; f->original = CCP_ORIGINAL_COAT; f->target = CCP_TRIAL_COAT;
    f->bound = true; f->mode = 1;
    Put64(f->pawn + CCP_AVATAR_OFFSET, Avatar); Put64(f->avatar + 0x20, Pawn); Put64(f->avatar, Table);
    Put64(f->table + 0x1320, Game + 0x43D2EB0); Put64(f->table + 0xC68, Game + 0x43D25A8);
    Put64(f->table + 0x4C0, Game + 0x43D6D10);
    CCPArray array = {Data,30,30}; memcpy(f->avatar + CCP_SYNC_OFFSET, &array, sizeof(array));
    uint32_t type = 4, item = CCP_ORIGINAL_COAT;
    memcpy(f->data + CCP_COAT_SLOT * CCP_ITEM_STRIDE, &type, 4);
    memcpy(f->data + CCP_COAT_SLOT * CCP_ITEM_STRIDE + 4, &item, 4);
}
static CCPTrialResult Apply(Fixture *f, CCPCoatTrial *t) { return CCPApplyCoat((CCPReader){Read,f}, Write, f, Valid, t); }
static CCPTrialResult Restore(Fixture *f, CCPCoatTrial *t) { return CCPRestoreCoat((CCPReader){Read,f}, Write, f, Valid, t); }
int main(void) {
    Fixture f, original; CCPCoatTrial trial; Reset(&f, &trial); original = f;
    CCPTrialResult result = Apply(&f, &trial);
    assert(result.status == CCPTrialApplied && result.idConfirmed && result.refreshQueued && trial.active);
    assert(f.idWrites == 1 && f.flagWrites == 1);
    assert(!memcmp(f.pawn, original.pawn, sizeof(f.pawn)) && !memcmp(f.table, original.table, sizeof(f.table)));
    for (size_t i = 0; i < sizeof(f.data); i++)
        if (i < CCP_COAT_SLOT * CCP_ITEM_STRIDE + 4 || i >= CCP_COAT_SLOT * CCP_ITEM_STRIDE + 8) assert(f.data[i] == original.data[i]);
    f.avatar[CCP_REFRESH_PENDING] = 0; // game tick consumed the first request
    result = Restore(&f, &trial);
    assert(result.status == CCPTrialRestored && result.idConfirmed && result.refreshQueued && !trial.active);
    assert(!memcmp(f.data, original.data, sizeof(f.data)));
    Reset(&f, &trial); f.mode = 0; assert(Apply(&f, &trial).status == CCPTrialUnsupportedRefresh && !f.idWrites);
    Reset(&f, &trial); f.bound = false; assert(Apply(&f, &trial).status == CCPTrialBindingChanged && !f.idWrites);
    Reset(&f, &trial); f.table[0x1320] ^= 4; assert(Apply(&f, &trial).status == CCPTrialLayoutChanged && !f.idWrites);
    Reset(&f, &trial); memset(f.data + CCP_COAT_SLOT * CCP_ITEM_STRIDE + 4, 0, 4);
    assert(Apply(&f, &trial).status == CCPTrialWrongOutfit && !f.idWrites);
    Reset(&f, &trial); f.failWrite = true;
    assert(Apply(&f, &trial).status == CCPTrialWriteFailed && !trial.active && !f.flagWrites);
    Reset(&f, &trial); f.failAfterWriting = true;
    assert(Apply(&f, &trial).status == CCPTrialWriteFailed && trial.active);
    f.failAfterWriting = false; assert(Restore(&f, &trial).status == CCPTrialRestored && !trial.active);
    Reset(&f, &trial); assert(Apply(&f, &trial).idConfirmed); f.bound = false;
    unsigned writes = f.idWrites; assert(Restore(&f, &trial).status == CCPTrialBindingChanged && f.idWrites == writes);
    Reset(&f, &trial); assert(Apply(&f, &trial).idConfirmed); f.data[CCP_COAT_SLOT * CCP_ITEM_STRIDE + 4] ^= 1;
    writes = f.idWrites; assert(Restore(&f, &trial).status == CCPTrialSlotChanged && !trial.active && f.idWrites == writes);
    Reset(&f, &trial); assert(Apply(&f, &trial).idConfirmed); f.avatar[CCP_SYNC_OFFSET] ^= 8;
    writes = f.idWrites; assert(Restore(&f, &trial).status == CCPTrialLayoutChanged && f.idWrites == writes);
    const uint32_t slots[] = {2,5,6,7,11};
    const uint32_t originals[] = {40601010,403251,404007,405011,703001};
    const uint32_t targets[] = {40609192,403971,404232,405292,703068};
    for (size_t i = 0; i < 5; i++) {
        Reset(&f, &trial);
        f.slot = trial.slot = slots[i]; f.original = originals[i]; f.target = trial.target = targets[i];
        uint32_t type = 4;
        memcpy(f.data + f.slot * CCP_ITEM_STRIDE, &type, 4);
        memcpy(f.data + f.slot * CCP_ITEM_STRIDE + 4, &f.original, 4);
        assert(Apply(&f, &trial).idConfirmed && trial.original == f.original);
        assert(Restore(&f, &trial).idConfirmed && !trial.active);
        assert(CCPU32(f.data + f.slot * CCP_ITEM_STRIDE + 4) == f.original);
    }
    Reset(&f, &trial); trial.target = 703068;
    assert(Apply(&f, &trial).status == CCPTrialLayoutChanged && !f.idWrites);
    Reset(&f, &trial); trial.slot = 30;
    assert(Apply(&f, &trial).status == CCPTrialLayoutChanged && !f.idWrites);
    Reset(&f, &trial); f.original = 403385;
    memcpy(f.data + CCP_COAT_SLOT * CCP_ITEM_STRIDE + 4, &f.original, 4);
    assert(Apply(&f, &trial).idConfirmed && trial.original == 403385);
    assert(Restore(&f, &trial).idConfirmed && CCPU32(f.data + CCP_COAT_SLOT * CCP_ITEM_STRIDE + 4) == 403385);
    puts("PASS: scoped coat apply/restore, exact write footprint, mode/binding/layout guards and partial-write rollback");
    return 0;
}
