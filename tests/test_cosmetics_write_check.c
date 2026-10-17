#include <assert.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsWriteCheck.h"
static const uint64_t Base = UINT64_C(0x100000000);
typedef struct {
    uint8_t bytes[8192];
    unsigned writes, arrayReads, itemReads;
    bool moveArray, mutateItem;
    uint64_t nativeState;
} Fixture;
static bool Read(void *context, uint64_t address, void *out, size_t length) {
    Fixture *f = context;
    if (address < Base || address - Base > sizeof(f->bytes) || length > sizeof(f->bytes) - (address - Base)) return false;
    memcpy(out, f->bytes + address - Base, length);
    if (address == Base + 512 && ++f->arrayReads == 2 && f->moveArray) ((uint8_t *)out)[0] ^= 8;
    if (address == Base + 4096 + 2 * CCP_ITEM_STRIDE && ++f->itemReads == 2 && f->mutateItem) ((uint8_t *)out)[4] ^= 1;
    return true;
}
static CCPNativeWriteResult Write(void *context, uint64_t address, const void *expected, const void *replacement, size_t length) {
    Fixture *f = context; f->writes++;
    assert(address == Base + 4096 + 2 * CCP_ITEM_STRIDE + 4 && length == 4);
    assert(CCPU32(expected) == 40601010 && !memcmp(expected, replacement, length));
    return (CCPNativeWriteResult){f->nativeState, 0};
}
static void Reset(Fixture *f) {
    memset(f, 0, sizeof(*f));
    CCPArray array = {Base + 4096, 30, 32}; memcpy(f->bytes + 512, &array, sizeof(array));
    uint32_t type = 4, item = 40601010;
    memcpy(f->bytes + 4096 + 2 * CCP_ITEM_STRIDE, &type, 4);
    memcpy(f->bytes + 4096 + 2 * CCP_ITEM_STRIDE + 4, &item, 4);
    f->nativeState = UINT64_C(1) | (UINT64_C(4) << 32) | (UINT64_C(1) << 40);
}
int main(void) {
    Fixture f; Reset(&f); CCPReader reader = {Read, &f};
    CCPWriteCheck result = CCPCheckInitialOutfitWrite(reader, Write, &f, Base + 512);
    assert(f.writes == 1 && result.attempted && result.nativeCommitted && result.readbackStable);
    for (uint32_t failure = 0; failure <= 5; failure++) {
        if (failure == 1) continue;
        Reset(&f); f.nativeState = failure | (UINT64_C(4) << 32) | (UINT64_C(1) << 40);
        result = CCPCheckInitialOutfitWrite(reader, Write, &f, Base + 512);
        assert(result.attempted && !result.nativeCommitted);
    }
    Reset(&f); f.nativeState = 1 | (UINT64_C(2) << 32) | (UINT64_C(1) << 40);
    assert(!CCPCheckInitialOutfitWrite(reader, Write, &f, Base + 512).nativeCommitted);
    Reset(&f); f.nativeState = 1 | (UINT64_C(4) << 32);
    assert(!CCPCheckInitialOutfitWrite(reader, Write, &f, Base + 512).nativeCommitted);
    Reset(&f); f.moveArray = true;
    assert(!CCPCheckInitialOutfitWrite(reader, Write, &f, Base + 512).attempted && !f.writes);
    Reset(&f); f.mutateItem = true;
    assert(!CCPCheckInitialOutfitWrite(reader, Write, &f, Base + 512).attempted && !f.writes);
    Reset(&f); f.bytes[4096 + 2 * CCP_ITEM_STRIDE + 4] ^= 1;
    assert(!CCPCheckInitialOutfitWrite(reader, Write, &f, Base + 512).attempted && !f.writes);
    puts("PASS: exact initial slot, identical values, stable arrays, committed enum/byte count and readback required");
    return 0;
}
