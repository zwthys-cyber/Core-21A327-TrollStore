#ifndef CORE_COSMETICS_WRITE_CHECK_H
#define CORE_COSMETICS_WRITE_CHECK_H
#include "CoreCosmeticsRead.h"

typedef struct { uint64_t state, detail; } CCPNativeWriteResult;
typedef CCPNativeWriteResult (*CCPCompareWrite)(void *, uint64_t, const void *, const void *, size_t);
typedef struct {
    bool attempted, nativeCommitted, readbackStable;
    uint32_t nativeStatus, writtenBytes;
} CCPWriteCheck;

// Check the original write backend against the exact initial-outfit slot that
// the user captured. Expected and replacement are IDENTICAL. This test cannot
// select another address, replace an appearance, allocate or call game code.
static inline CCPWriteCheck CCPCheckInitialOutfitWrite(CCPReader reader, CCPCompareWrite write,
                                                      void *context, uint64_t arrayAddress) {
    CCPWriteCheck result = {0};
    CCPArray before = {0}, after = {0};
    uint8_t item[CCP_ITEM_STRIDE], repeated[sizeof(item)];
    if (!write || !CCPReadBytes(reader, arrayAddress, &before, sizeof(before)) || before.count != 30 ||
        before.capacity < 30 || before.capacity > 512 || !CCPPointer(before.data)) return result;
    uint64_t itemAddress = before.data + 2 * CCP_ITEM_STRIDE;
    if (!CCPReadBytes(reader, itemAddress, item, sizeof(item)) ||
        !CCPReadBytes(reader, itemAddress, repeated, sizeof(repeated)) || memcmp(item, repeated, sizeof(item)) ||
        CCPU32(item) != 4 || CCPU32(item + 4) != 40601010 ||
        !CCPReadBytes(reader, arrayAddress, &after, sizeof(after)) || memcmp(&before, &after, sizeof(before))) return result;
    uint32_t original = CCPU32(item + 4);
    result.attempted = true;
    CCPNativeWriteResult native = write(context, itemAddress + 4, &original, &original, sizeof(original));
    result.nativeStatus = (uint32_t)native.state;
    result.writtenBytes = (uint32_t)((native.state >> 32) & 0xFF);
    // Status is an ENUM: 3 and 5 are failures even though their low bit is set.
    result.nativeCommitted = result.nativeStatus == 1 && result.writtenBytes == sizeof(original) &&
        ((native.state >> 40) & 0xFF) == 1;
    uint32_t readback = 0;
    result.readbackStable = CCPReadBytes(reader, itemAddress + 4, &readback, sizeof(readback)) && readback == original &&
        CCPReadBytes(reader, arrayAddress, &after, sizeof(after)) && !memcmp(&before, &after, sizeof(before));
    return result;
}
#endif
