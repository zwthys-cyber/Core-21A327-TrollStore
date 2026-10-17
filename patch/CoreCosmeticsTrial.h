#ifndef CORE_COSMETICS_TRIAL_H
#define CORE_COSMETICS_TRIAL_H
#include "CoreCosmeticsWriteCheck.h"

#define CCP_COAT_SLOT 5
#define CCP_ORIGINAL_COAT 403251u
#define CCP_TRIAL_COAT 403188u
#define CCP_REFRESH_PENDING 0xF50
#define CCP_REFRESH_MODE_RVA 0x11069CD5
typedef bool (*CCPBindingValid)(void *context);
typedef struct {
    uint64_t game, pawn, avatar;
    uint32_t slot, target, original;
    CCPArray array;
    uint8_t instance[16];
    bool active;
} CCPCoatTrial;
typedef enum {
    CCPTrialApplied, CCPTrialRestored, CCPTrialAlreadyRestored,
    CCPTrialBindingChanged, CCPTrialLayoutChanged, CCPTrialWrongOutfit,
    CCPTrialUnsupportedRefresh, CCPTrialWriteFailed, CCPTrialReadbackFailed,
    CCPTrialSlotChanged, CCPTrialNoActiveChange
} CCPTrialStatus;
typedef struct {
    CCPTrialStatus status;
    uint32_t nativeStatus, writtenBytes;
    bool idConfirmed, refreshQueued;
} CCPTrialResult;

static inline bool CCPItemFitsSlot(uint32_t slot, uint32_t item) {
    switch (slot) {
        case 2: return item >= 40600000 && item < 40700000;
        case 5: return item >= 403000 && item < 404000;
        case 6: return item >= 404000 && item < 405000;
        case 7: return item >= 405000 && item < 406000;
        case 11: return item >= 703000 && item < 704000;
        default: return false;
    }
}

static inline bool CCPNativeCommitted(CCPNativeWriteResult result, size_t length) {
    return (uint32_t)result.state == 1 && ((result.state >> 32) & 255) == length &&
        ((result.state >> 40) & 255) == 1;
}
static inline bool CCPTrialLayout(CCPReader reader, CCPCoatTrial *trial, uint32_t *item) {
    uint64_t owner = 0, table = 0, onrep = 0, update = 0, tick = 0;
    CCPArray array = {0}; uint8_t entry[CCP_ITEM_STRIDE];
    if (!CCPItemFitsSlot(trial->slot, trial->target) ||
        !CCPPointer(trial->pawn) || !CCPPointer(trial->avatar) ||
        !CCPReadBytes(reader, trial->avatar + 0x20, &owner, sizeof(owner)) || owner != trial->pawn ||
        !CCPReadBytes(reader, trial->pawn + CCP_AVATAR_OFFSET, &owner, sizeof(owner)) || owner != trial->avatar ||
        !CCPReadBytes(reader, trial->avatar, &table, sizeof(table)) || !CCPPointer(table) ||
        !CCPReadBytes(reader, table + 0x1320, &onrep, sizeof(onrep)) || onrep != trial->game + 0x43D2EB0 ||
        !CCPReadBytes(reader, table + 0xC68, &update, sizeof(update)) || update != trial->game + 0x43D25A8 ||
        !CCPReadBytes(reader, table + 0x4C0, &tick, sizeof(tick)) || tick != trial->game + 0x43D6D10 ||
        !CCPReadBytes(reader, trial->avatar + CCP_SYNC_OFFSET, &array, sizeof(array)) ||
        array.count != 30 || array.capacity < 30 || array.capacity > 512 || !CCPPointer(array.data)) return false;
    if (trial->array.data && memcmp(&trial->array, &array, sizeof(array))) return false;
    if (!CCPReadBytes(reader, array.data + trial->slot * CCP_ITEM_STRIDE, entry, sizeof(entry)) ||
        CCPU32(entry) != 4) return false;
    if (trial->array.data && memcmp(trial->instance, entry + 8, sizeof(trial->instance))) return false;
    if (!trial->array.data) {
        trial->array = array;
        memcpy(trial->instance, entry + 8, sizeof(trial->instance));
    }
    *item = CCPU32(entry + 4);
    return true;
}
static inline bool CCPTrialQueueRefresh(CCPReader reader, CCPCompareWrite write, void *context,
                                       CCPBindingValid valid, const CCPCoatTrial *trial) {
    uint8_t mode = 0, pending = 0, requested = 1;
    if (!valid(context) || !CCPReadBytes(reader, trial->game + CCP_REFRESH_MODE_RVA, &mode, 1) || mode != 1 ||
        !CCPReadBytes(reader, trial->avatar + CCP_REFRESH_PENDING, &pending, 1) || pending > 1) return false;
    if (pending == 1) return true;
    CCPNativeWriteResult result = write(context, trial->avatar + CCP_REFRESH_PENDING, &pending, &requested, 1);
    // A game tick can consume the request before a later external read. The
    // native backend must confirm its own write/readback before we report it.
    return CCPNativeCommitted(result, 1) && valid(context);
}
static inline CCPTrialResult CCPTrialSetID(CCPReader reader, CCPCompareWrite write, void *context,
                                         CCPBindingValid valid, CCPCoatTrial *trial, bool restore) {
    CCPTrialResult result = {.status = CCPTrialBindingChanged};
    if (!write || !valid || !valid(context)) return result;
    uint32_t current = 0;
    if (!CCPTrialLayout(reader, trial, &current)) { result.status = CCPTrialLayoutChanged; return result; }
    if (!restore && !trial->original) {
        if (!CCPItemFitsSlot(trial->slot, current)) { result.status = CCPTrialWrongOutfit; return result; }
        if (current == trial->target) { result.status = CCPTrialNoActiveChange; return result; }
        trial->original = current;
    }
    if (!CCPItemFitsSlot(trial->slot, trial->original)) { result.status = CCPTrialWrongOutfit; return result; }
    uint32_t expected = restore ? trial->target : trial->original;
    uint32_t replacement = restore ? trial->original : trial->target;
    if (restore && current == trial->original) {
        trial->active = false; result.idConfirmed = true; result.status = CCPTrialAlreadyRestored; return result;
    }
    if (current != expected) {
        if (restore) trial->active = false; // The game/user owns this new value.
        result.status = restore ? CCPTrialSlotChanged : CCPTrialWrongOutfit; return result;
    }
    uint8_t mode = 0, pending = 0;
    if (!restore && (!CCPReadBytes(reader, trial->game + CCP_REFRESH_MODE_RVA, &mode, 1) || mode != 1 ||
        !CCPReadBytes(reader, trial->avatar + CCP_REFRESH_PENDING, &pending, 1) || pending > 1)) {
        result.status = CCPTrialUnsupportedRefresh; return result;
    }
    if (!valid(context)) return result;
    uint64_t address = trial->array.data + trial->slot * CCP_ITEM_STRIDE + 4;
    if (!restore) trial->active = true; // Preserve a possible change across failed verification.
    CCPNativeWriteResult native = write(context, address, &expected, &replacement, 4);
    result.nativeStatus = (uint32_t)native.state;
    result.writtenBytes = (uint32_t)((native.state >> 32) & 255);
    if (!valid(context)) return result;
    if (!CCPTrialLayout(reader, trial, &current)) { result.status = CCPTrialLayoutChanged; return result; }
    // Track an owned change even if a backend verification failed after writing.
    // The caller can then attempt a scoped rollback; it must not retry blindly.
    if (current == replacement) trial->active = !restore;
    else if (!restore) trial->active = false;
    if (!CCPNativeCommitted(native, 4)) { result.status = CCPTrialWriteFailed; return result; }
    if (current != replacement) { result.status = CCPTrialReadbackFailed; return result; }
    result.idConfirmed = true;
    result.refreshQueued = CCPTrialQueueRefresh(reader, write, context, valid, trial);
    result.status = restore ? CCPTrialRestored : CCPTrialApplied;
    return result;
}
static inline CCPTrialResult CCPApplyCoat(CCPReader reader, CCPCompareWrite write, void *context,
                                        CCPBindingValid valid, CCPCoatTrial *trial) {
    if (trial->active) return (CCPTrialResult){.status = CCPTrialNoActiveChange};
    return CCPTrialSetID(reader, write, context, valid, trial, false);
}
static inline CCPTrialResult CCPRestoreCoat(CCPReader reader, CCPCompareWrite write, void *context,
                                          CCPBindingValid valid, CCPCoatTrial *trial) {
    if (!trial->active) return (CCPTrialResult){.status = CCPTrialNoActiveChange};
    return CCPTrialSetID(reader, write, context, valid, trial, true);
}
#endif
