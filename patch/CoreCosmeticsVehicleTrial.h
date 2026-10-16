#ifndef CORE_COSMETICS_VEHICLE_TRIAL_H
#define CORE_COSMETICS_VEHICLE_TRIAL_H
#include "CoreCosmeticsVehicleRead.h"
#include "CoreCosmeticsTrial.h"

// First device-scoped experiment, not a general vehicle skin selector.
// 3349001/type 33/11 slots was sampled eight times on the user's r8 device.
// Native OnRep 0x47EE9A8..0x47EE9B0 copies the master ID to Vehicle+0x20AC.
// Whether this data request alone is consumed is NOT yet device verified.
// Never call remote game code, patch a vtable or reuse character F50 here.
#define CCP_VEHICLE_ORIGINAL 3349001u
#define CCP_VEHICLE_TARGET 3349002u
typedef enum {
    CCPVehicleTrialApplied, CCPVehicleTrialRestoring, CCPVehicleTrialRestored,
    CCPVehicleTrialDataRestored, CCPVehicleTrialBindingChanged, CCPVehicleTrialLayoutChanged,
    CCPVehicleTrialWrongVehicle, CCPVehicleTrialBusy, CCPVehicleTrialWriteFailed,
    CCPVehicleTrialGameChanged, CCPVehicleTrialInactive
} CCPVehicleTrialStatus;
typedef struct {
    uint32_t master, counter, pending, skin, previous, real, defaultSkin, cachedCounter;
} CCPVehicleView;
typedef struct {
    uint64_t game, pawn, controller, user, vehicle, avatar;
    CCPArray sync, cached;
    uint32_t componentIndex, originalCounter, appliedCounter, restoreCounter;
    uint8_t membership[0x30], objectIdentity[3][0x1C], entryIdentity[CCP_VEHICLE_STRIDE];
    unsigned owned;
    bool active, prepared, requestSent, restoring, counterRestored, restoreRequested, engineApplyObserved;
} CCPVehicleTrial;
typedef struct {
    CCPVehicleTrialStatus status;
    uint32_t nativeStatus, writtenBytes, totalWrittenBytes;
    bool idConfirmed, requestWritten, dataRestored, nativeRestoreObserved;
} CCPVehicleTrialResult;
enum { CCPVehicleOwnID = 1, CCPVehicleOwnCounter = 2, CCPVehicleOwnRequest = 4 };

static inline bool CCPVehicleTrialLayout(CCPReader reader, const CCPVehicleTrial *t, CCPVehicleView *view) {
    memset(view, 0, sizeof(*view));
    uint64_t actor = 0, current = 0, owner = 0, avatar = 0, table = 0, method = 0, userClass = 0;
    CCPVehicleSample diagnostic = {0};
    if (!CCPVehicleResolveActor(reader, t->game, t->pawn, t->controller, &actor, &diagnostic) || actor != t->controller ||
        !CCPReadBytes(reader, t->game + CCP_VEHICLE_USER_CLASS_RVA, &userClass, 8) ||
        CCPObjectIsA(reader, t->user, userClass) != CCPClassMatches ||
        !CCPReadBytes(reader, t->user + 0x20, &owner, 8) || owner != t->controller ||
        !CCPReadBytes(reader, t->user + 0x168, &owner, 8) || owner != t->pawn ||
        !CCPReadBytes(reader, t->user + 0x158, &current, 8) || current != t->vehicle ||
        !CCPVehicleOwner(reader, t->vehicle, &avatar) || avatar != t->avatar) return false;
    uint8_t membership[0x30], identity[0x1C];
    if (!CCPReadBytes(reader, actor + 0x440, membership, sizeof(membership)) ||
        memcmp(t->membership, membership, sizeof(membership)) ||
        !CCPReadBytes(reader, CCPU64(membership) + (uint64_t)t->componentIndex * 16, &current, 8) || current != t->user) return false;
    uint64_t bitmap = CCPU64(membership + 0x20);
    if (!bitmap) bitmap = actor + 0x450;
    uint32_t bits = 0;
    if (!CCPReadBytes(reader, bitmap + t->componentIndex / 32 * 4, &bits, 4) ||
        !(bits & (UINT32_C(1) << (t->componentIndex % 32)))) return false;
    const uint64_t objects[] = {t->user, t->vehicle, t->avatar};
    for (unsigned i = 0; i < 3; i++)
        if (!CCPReadBytes(reader, objects[i] + 0xC, identity, sizeof(identity)) ||
            memcmp(identity, t->objectIdentity[i], sizeof(identity))) return false;
    if (!CCPReadBytes(reader, t->vehicle, &table, 8) || table != t->game + 0xEBC1680 ||
        !CCPReadBytes(reader, t->avatar, &table, 8) || table != t->game + 0xE6D6A38 ||
        !CCPReadBytes(reader, table + 0xD18, &method, 8) || method != t->game + 0x47EE7C8 ||
        !CCPReadBytes(reader, table + 0x4C0, &method, 8) || method != t->game + 0x42E4F28) return false;
    CCPArray sync = {0}, cached = {0}, repeated = {0};
    uint8_t entry[CCP_VEHICLE_STRIDE], again[sizeof(entry)]; uint32_t skin[4], skinAgain[4];
    if (!CCPReadBytes(reader, t->avatar + 0x11E8, &sync, sizeof(sync)) || memcmp(&sync, &t->sync, sizeof(sync)) ||
        !CCPReadBytes(reader, t->avatar + 0x1208, &cached, sizeof(cached)) || memcmp(&cached, &t->cached, sizeof(cached)) ||
        !CCPReadBytes(reader, sync.data, entry, sizeof(entry)) ||
        !CCPReadBytes(reader, sync.data, again, sizeof(again)) || memcmp(entry, again, sizeof(entry)) ||
        memcmp(entry, t->entryIdentity, 4) || memcmp(entry + 8, t->entryIdentity + 8, 0x14) ||
        memcmp(entry + 0x20, t->entryIdentity + 0x20, 0x10) ||
        !CCPReadBytes(reader, t->vehicle + 0x20AC, skin, sizeof(skin)) ||
        !CCPReadBytes(reader, t->vehicle + 0x20AC, skinAgain, sizeof(skinAgain)) || memcmp(skin, skinAgain, sizeof(skin)) ||
        !CCPReadBytes(reader, t->avatar + 0x10A4, &view->defaultSkin, 4) ||
        !CCPReadBytes(reader, cached.data, &view->cachedCounter, 4) ||
        !CCPReadBytes(reader, t->avatar + 0x11E8, &repeated, sizeof(repeated)) || memcmp(&sync, &repeated, sizeof(sync)) ||
        !CCPReadBytes(reader, t->avatar + 0x1208, &repeated, sizeof(repeated)) || memcmp(&cached, &repeated, sizeof(cached))) return false;
    view->master = CCPU32(entry + 4); view->counter = CCPU32(entry + 0x1C);
    view->pending = skin[0]; view->skin = skin[1]; view->previous = skin[2]; view->real = skin[3];
    return true;
}

static inline bool CCPVehicleTrialPrepare(CCPReader reader, CCPVehicleTrial *t) {
    uint64_t actor = 0; CCPVehicleSample source = {0};
    if (!CCPVehicleSource(reader, t->game, t->pawn, t->controller, &actor, &t->user, &t->vehicle, &source) ||
        !CCPVehicleOwner(reader, t->vehicle, &t->avatar)) return false;
    t->componentIndex = source.components.selectedIndex;
    if (!CCPReadBytes(reader, actor + 0x440, t->membership, sizeof(t->membership))) return false;
    const uint64_t objects[] = {t->user, t->vehicle, t->avatar};
    for (unsigned i = 0; i < 3; i++)
        if (!CCPReadBytes(reader, objects[i] + 0xC, t->objectIdentity[i], sizeof(t->objectIdentity[i]))) return false;
    if (!CCPReadBytes(reader, t->avatar + 0x11E8, &t->sync, sizeof(t->sync)) ||
        t->sync.count != 11 || t->sync.capacity < 11 || t->sync.capacity > 128 || !CCPPointer(t->sync.data) ||
        !CCPReadBytes(reader, t->avatar + 0x1208, &t->cached, sizeof(t->cached)) ||
        t->cached.count < t->sync.count || t->cached.count > 16 || t->cached.capacity < t->cached.count ||
        t->cached.capacity > 128 || !CCPPointer(t->cached.data) ||
        !CCPReadBytes(reader, t->sync.data, t->entryIdentity, sizeof(t->entryIdentity)) || CCPU32(t->entryIdentity) != 33) return false;
    CCPVehicleView view;
    if (!CCPVehicleTrialLayout(reader, t, &view)) return false;
    t->originalCounter = view.counter;
    if (t->originalCounter > UINT32_MAX - 2) return false;
    t->appliedCounter = t->originalCounter + 1; t->restoreCounter = t->originalCounter + 2;
    t->prepared = true;
    return true;
}

static inline bool CCPVehicleTrialValuesOwned(const CCPVehicleTrial *t, const CCPVehicleView *v) {
    return (v->master == CCP_VEHICLE_ORIGINAL || v->master == CCP_VEHICLE_TARGET) &&
        (v->counter == t->originalCounter || v->counter == t->appliedCounter ||
         (t->counterRestored && v->counter == t->restoreCounter)) &&
        (v->pending == 0 || v->pending == CCP_VEHICLE_ORIGINAL || v->pending == CCP_VEHICLE_TARGET) &&
        (v->skin == CCP_VEHICLE_ORIGINAL || v->skin == CCP_VEHICLE_TARGET) &&
        (v->real == CCP_VEHICLE_ORIGINAL || v->real == CCP_VEHICLE_TARGET) &&
        (v->defaultSkin == CCP_VEHICLE_ORIGINAL || v->defaultSkin == CCP_VEHICLE_TARGET);
}
static inline bool CCPVehicleTrialWrite(CCPReader reader, CCPCompareWrite write, void *context, CCPBindingValid valid,
    CCPVehicleTrial *t, uint64_t address, uint32_t expected, uint32_t replacement, unsigned bit,
    bool consumedRequestAllowed, CCPVehicleTrialResult *result) {
    CCPVehicleView view;
    if (!valid(context)) { result->status = CCPVehicleTrialBindingChanged; return false; }
    if (!CCPVehicleTrialLayout(reader, t, &view)) { result->status = CCPVehicleTrialLayoutChanged; return false; }
    if (!CCPVehicleTrialValuesOwned(t, &view) || (address != t->sync.data + 4 &&
        view.master != (t->restoring ? CCP_VEHICLE_ORIGINAL : CCP_VEHICLE_TARGET))) {
        result->status = CCPVehicleTrialGameChanged; return false;
    }
    t->owned |= bit; t->active = true; // Keep a journal even across failed backend verification.
    CCPNativeWriteResult native = write(context, address, &expected, &replacement, 4);
    result->nativeStatus = (uint32_t)native.state; result->writtenBytes = (uint32_t)((native.state >> 32) & 255);
    result->totalWrittenBytes += result->writtenBytes;
    uint32_t after = 0;
    if (!valid(context)) { result->status = CCPVehicleTrialBindingChanged; return false; }
    bool read = CCPReadBytes(reader, address, &after, 4);
    if (read && after == expected && !consumedRequestAllowed) t->owned &= ~bit;
    if (address == t->sync.data + 0x1C && replacement == t->restoreCounter && read && after == replacement)
        t->counterRestored = true;
    if (!CCPNativeCommitted(native, 4) || !read || (after != replacement && !(consumedRequestAllowed && after == 0))) {
        result->status = CCPVehicleTrialWriteFailed; return false;
    }
    return true;
}
static inline CCPVehicleTrialResult CCPApplyVehicleTrial(CCPReader reader, CCPCompareWrite write, void *context,
                                                        CCPBindingValid valid, CCPVehicleTrial *t) {
    CCPVehicleTrialResult result = {.status = CCPVehicleTrialBindingChanged};
    if (!write || !valid || !valid(context)) return result;
    if (t->active) { result.status = CCPVehicleTrialBusy; return result; }
    *t = (CCPVehicleTrial){.game = t->game, .pawn = t->pawn, .controller = t->controller};
    if (!CCPVehicleTrialPrepare(reader, t)) { result.status = CCPVehicleTrialLayoutChanged; return result; }
    CCPVehicleView view;
    if (!CCPVehicleTrialLayout(reader, t, &view)) { result.status = CCPVehicleTrialLayoutChanged; return result; }
    if (view.master != CCP_VEHICLE_ORIGINAL || view.skin != CCP_VEHICLE_ORIGINAL || view.real != CCP_VEHICLE_ORIGINAL ||
        view.defaultSkin != CCP_VEHICLE_ORIGINAL || view.pending != 0) {
        result.status = CCPVehicleTrialWrongVehicle; return result;
    }
    if (!CCPVehicleTrialWrite(reader, write, context, valid, t, t->sync.data + 4, CCP_VEHICLE_ORIGINAL,
        CCP_VEHICLE_TARGET, CCPVehicleOwnID, false, &result)) return result;
    if (!CCPVehicleTrialWrite(reader, write, context, valid, t, t->sync.data + 0x1C, t->originalCounter,
        t->appliedCounter, CCPVehicleOwnCounter, false, &result)) return result;
    result.idConfirmed = true;
    if (!CCPVehicleTrialWrite(reader, write, context, valid, t, t->vehicle + 0x20AC, 0,
        CCP_VEHICLE_TARGET, CCPVehicleOwnRequest, true, &result)) return result;
    t->requestSent = true; result.requestWritten = true; result.status = CCPVehicleTrialApplied;
    return result;
}
static inline CCPVehicleTrialResult CCPRestoreVehicleTrial(CCPReader reader, CCPCompareWrite write, void *context,
                                                          CCPBindingValid valid, CCPVehicleTrial *t) {
    CCPVehicleTrialResult result = {.status = CCPVehicleTrialBindingChanged};
    if (!t->active) { result.status = CCPVehicleTrialInactive; return result; }
    if (!write || !valid || !valid(context)) return result;
    CCPVehicleView view;
    if (!CCPVehicleTrialLayout(reader, t, &view)) { result.status = CCPVehicleTrialLayoutChanged; return result; }
    if (!CCPVehicleTrialValuesOwned(t, &view)) {
        t->active = false; t->owned = 0; result.status = CCPVehicleTrialGameChanged; return result;
    }
    t->restoring = true;
    if (view.master == CCP_VEHICLE_TARGET && (t->owned & CCPVehicleOwnID) &&
        !CCPVehicleTrialWrite(reader, write, context, valid, t, t->sync.data + 4, CCP_VEHICLE_TARGET,
            CCP_VEHICLE_ORIGINAL, CCPVehicleOwnID, false, &result)) return result;
    if (view.master == CCP_VEHICLE_ORIGINAL || (t->owned & CCPVehicleOwnID)) t->owned &= ~CCPVehicleOwnID;
    if (view.counter == t->appliedCounter && (t->owned & CCPVehicleOwnCounter) &&
        !CCPVehicleTrialWrite(reader, write, context, valid, t, t->sync.data + 0x1C, t->appliedCounter,
            t->restoreCounter, CCPVehicleOwnCounter, false, &result)) return result;
    t->owned &= ~CCPVehicleOwnCounter; // Restore advances the native counter; it must never rewind.
    if (!CCPVehicleTrialLayout(reader, t, &view)) { result.status = CCPVehicleTrialLayoutChanged; return result; }
    result.idConfirmed = view.master == CCP_VEHICLE_ORIGINAL;
    if (!result.idConfirmed) { result.status = CCPVehicleTrialGameChanged; return result; }
    if (!(t->owned & CCPVehicleOwnRequest) && !t->requestSent) {
        t->active = false; result.dataRestored = true; result.status = CCPVehicleTrialDataRestored; return result;
    }
    if (view.pending != CCP_VEHICLE_ORIGINAL &&
        !CCPVehicleTrialWrite(reader, write, context, valid, t, t->vehicle + 0x20AC, view.pending,
            CCP_VEHICLE_ORIGINAL, CCPVehicleOwnRequest, true, &result)) return result;
    t->restoreRequested = true; result.requestWritten = true; result.status = CCPVehicleTrialRestoring;
    return result;
}
static inline CCPVehicleTrialResult CCPFinishVehicleRestore(CCPReader reader, CCPCompareWrite write, void *context,
    CCPBindingValid valid, CCPVehicleTrial *t, bool allowCancelUnconsumed) {
    CCPVehicleTrialResult result = {.status = CCPVehicleTrialBindingChanged};
    if (!t->active) { result.status = CCPVehicleTrialInactive; return result; }
    if (!valid || !valid(context)) return result;
    CCPVehicleView view;
    if (!CCPVehicleTrialLayout(reader, t, &view)) { result.status = CCPVehicleTrialLayoutChanged; return result; }
    if (!CCPVehicleTrialValuesOwned(t, &view)) {
        t->active = false; t->owned = 0; result.status = CCPVehicleTrialGameChanged; return result;
    }
    result.idConfirmed = view.master == CCP_VEHICLE_ORIGINAL;
    bool originalFields = result.idConfirmed && view.skin == CCP_VEHICLE_ORIGINAL &&
        view.real == CCP_VEHICLE_ORIGINAL && view.defaultSkin == CCP_VEHICLE_ORIGINAL;
    if (originalFields && view.pending == 0) {
        t->active = false; t->owned = 0; result.dataRestored = true;
        result.nativeRestoreObserved = t->restoreRequested;
        result.status = CCPVehicleTrialRestored; return result;
    }
    if (originalFields && view.pending == CCP_VEHICLE_ORIGINAL && allowCancelUnconsumed && !t->engineApplyObserved && write) {
        if (!CCPVehicleTrialWrite(reader, write, context, valid, t, t->vehicle + 0x20AC, CCP_VEHICLE_ORIGINAL, 0,
            CCPVehicleOwnRequest, false, &result)) return result;
        t->active = false; t->owned = 0; result.dataRestored = true;
        result.status = CCPVehicleTrialDataRestored; return result;
    }
    result.status = CCPVehicleTrialRestoring;
    return result;
}
#endif
