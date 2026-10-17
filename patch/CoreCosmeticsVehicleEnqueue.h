#ifndef CORE_COSMETICS_VEHICLE_ENQUEUE_H
#define CORE_COSMETICS_VEHICLE_ENQUEUE_H
#include "CoreCosmeticsVehicleRead.h"
#include "CoreCosmeticsWriteCheck.h"
#include <dlfcn.h>
#include <ptrauth.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

// Product GameThread PutOn path. Must keep RX code separate from RW CAD slots.
// r30 froze the game by mprotect(RX) on one buffer that also held FGraphEventRef.
#define CCP_ENQUEUE_CAD_RVA 0x3765444u
#define CCP_ENQUEUE_PUTON_RVA 0x47f2824u
#define CCP_ENQUEUE_TF_DESTROY_RVA 0x2704da4u
#define CCP_ENQUEUE_EVENT_RELEASE_RVA 0x35691fcu
#define CCP_ENQUEUE_PAGE 0x1000u

typedef uint64_t (*CCPEnqueueRemoteCall)(uint64_t timeout, uint64_t function, const char *label,
                                         uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                                         uint64_t a5, uint64_t a6, uint64_t a7,
                                         uint64_t *out0, uint64_t *out1);
typedef uint64_t (*CCPEnqueueSymbolCall)(const char *, uint64_t, uint64_t, uint64_t *);
typedef CCPNativeWriteResult (*CCPEnqueueWriteFn)(uint32_t, uint64_t, const void *, const void *, size_t);
typedef bool (*CCPEnqueueReadFn)(uint32_t, uint64_t, size_t, void *);

typedef struct {
    const char *status;
    uint32_t remoteStatus;
    uint64_t codePage, dataPage;
    uint32_t skinBefore, skinAfter, targetItem, targetResource;
    bool skinChanged, putOnAttempted;
} CCPVehicleEnqueueResult;

static inline bool CCPVehicleTargetObserved(const CCPVehicleEnqueueResult *result) {
    return result && result->putOnAttempted && result->status &&
        strcmp(result->status, "target-observed") == 0 &&
        result->skinAfter == result->targetItem;
}

static inline bool CCPVehiclePutOnDispatched(const CCPVehicleEnqueueResult *result) {
    return CCPVehicleTargetObserved(result) ||
        (result && result->putOnAttempted && result->status &&
         strcmp(result->status, "target-not-observed") == 0);
}

static inline bool CCPVehicleRestoreObserved(const CCPVehicleEnqueueResult *result) {
    return result && result->status &&
        (CCPVehicleTargetObserved(result) ||
         strcmp(result->status, "skipped-already-target") == 0);
}

static inline bool CCPEnqueueResourceIDForItem(uint32_t itemID, uint32_t *resourceID) {
    if (!resourceID || itemID == 0 || itemID > UINT32_MAX / 100u) return false;
    *resourceID = itemID * 100u;
    return true;
}

static inline bool CCPEnqueueWrite(CCPEnqueueReadFn read, CCPEnqueueWriteFn write, uint32_t pid,
                                   uint64_t address, const void *data, size_t length) {
    const uint8_t *bytes = data;
    for (size_t offset = 0; offset < length; ) {
        size_t chunk = length - offset;
        if (chunk > 8) chunk = 8;
        uint8_t expected[8] = {0}, replacement[8] = {0};
        if (!read(pid, address + offset, chunk, expected)) return false;
        memcpy(replacement, bytes + offset, chunk);
        if (!memcmp(expected, replacement, chunk)) { offset += chunk; continue; }
        CCPNativeWriteResult native = write(pid, address + offset, expected, replacement, chunk);
        if ((uint32_t)native.state != 1) return false;
        uint8_t readback[8] = {0};
        if (!read(pid, address + offset, chunk, readback) || memcmp(readback, replacement, chunk))
            return false;
        offset += chunk;
    }
    return true;
}

// Explicit-apply only. Never call from session-open probes.
// Vehicle state stores item IDs (for example 3349002), while PutOnEquipmentByResID
// takes the catalog resource ID (item ID * 100, for example 334900200).
static inline CCPVehicleEnqueueResult CCPEnqueuePutOnByResID(
    uint32_t pid, uint64_t game, uint64_t avatar, uint64_t vehicle, uint32_t skinBefore,
    uint32_t targetItem, uint32_t targetResource, CCPEnqueueReadFn read, CCPEnqueueWriteFn write,
    CCPEnqueueSymbolCall symbolCall, CCPEnqueueRemoteCall remoteCall) {
    CCPVehicleEnqueueResult out = {0};
    out.status = "not-started";
    out.skinBefore = skinBefore;
    out.targetItem = targetItem;
    out.targetResource = targetResource;
    if (!targetItem || !targetResource) {
        out.status = "bad-arguments";
        return out;
    }
    if (skinBefore == targetItem) { out.status = "skipped-already-target"; return out; }
    if (!CCPPointer(game) || !CCPPointer(avatar) || !CCPPointer(vehicle) || !read || !write ||
        !symbolCall || !remoteCall) {
        out.status = "bad-arguments";
        return out;
    }
    void *mprotectSym = dlsym(RTLD_DEFAULT, "mprotect");
    if (!mprotectSym) { out.status = "mprotect-symbol-missing"; return out; }
#if __has_feature(ptrauth_calls)
    uint64_t mprotectAddr = (uint64_t)ptrauth_strip(mprotectSym, ptrauth_key_function_pointer);
#else
    uint64_t mprotectAddr = (uint64_t)mprotectSym;
#endif
    uint64_t code = 0, data = 0, unused = 0;
    if ((uint32_t)symbolCall("calloc", 1, CCP_ENQUEUE_PAGE, &code) != 1 || !CCPPointer(code) ||
        (uint32_t)symbolCall("calloc", 1, CCP_ENQUEUE_PAGE, &data) != 1 || !CCPPointer(data)) {
        out.status = "calloc-failed";
        return out;
    }
    out.codePage = code;
    out.dataPage = data;
    // code+0x00: mov x8,x3; br x4
    // code+0x10: ldr x0,#16; ldr x1,#20; ldr x16,#24; br x16  pools at +0x20/+0x28/+0x30
    const uint32_t cadCode[2] = {0xAA0303E8u, 0xD61F0080u};
    const uint32_t putOnCode[4] = {0x58000080u, 0x580000A1u, 0x580000D0u, 0xD61F0200u};
    uint64_t avatarWord = avatar, resWord = out.targetResource;
    uint64_t putOnFn = game + CCP_ENQUEUE_PUTON_RVA;
    uint64_t invoker = code + 0x10, tfunction = data + 0x00, event = data + 0x08;
    if (!CCPEnqueueWrite(read, write, pid, code + 0x00, cadCode, sizeof(cadCode)) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x10, putOnCode, sizeof(putOnCode)) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x20, &avatarWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x28, &resWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x30, &putOnFn, 8) ||
        !CCPEnqueueWrite(read, write, pid, tfunction, &invoker, 8)) {
        out.status = "trampoline-write-failed";
        return out;
    }
    uint64_t mpRet = 0;
    uint64_t mpStatus = remoteCall(10000, mprotectAddr, "mprotect",
                                   code, CCP_ENQUEUE_PAGE, (uint64_t)(PROT_READ | PROT_EXEC),
                                   0, 0, 0, 0, 0, &mpRet, &unused);
    out.remoteStatus = (uint32_t)mpStatus;
    if ((uint32_t)mpStatus != 1 || mpRet != 0) {
        out.status = "mprotect-failed";
        return out;
    }
    out.putOnAttempted = true;
    uint64_t cad0 = 0, cad1 = 0;
    uint64_t cadStatus = remoteCall(15000, code + 0x00, "CAD-x8-tramp",
                                    tfunction, 0, 4, event, game + CCP_ENQUEUE_CAD_RVA,
                                    0, 0, 0, &cad0, &cad1);
    out.remoteStatus = (uint32_t)cadStatus;
    if ((uint32_t)cadStatus != 1) {
        out.status = "create-and-dispatch-call-failed";
        return out;
    }
    uint64_t rel = 0;
    remoteCall(10000, game + CCP_ENQUEUE_EVENT_RELEASE_RVA, "FGraphEventRef::Release",
               event, 0, 0, 0, 0, 0, 0, 0, &rel, &unused);
    remoteCall(10000, game + CCP_ENQUEUE_TF_DESTROY_RVA, "TFunction::Destroy",
               tfunction, 0, 0, 0, 0, 0, 0, 0, &rel, &unused);
    // GameThread PutOn is async. A successful CAD return proves only that the
    // task was submitted; product success requires observing the exact target.
    out.skinAfter = skinBefore;
    for (int settle = 0; settle < 100; settle++) {
        usleep(50000);
        uint32_t skinAfter[4] = {0};
        if (!read(pid, vehicle + 0x20AC, sizeof(skinAfter), skinAfter)) continue;
        out.skinAfter = skinAfter[1];
        if (skinAfter[1] == targetItem) break;
    }
    out.skinChanged = out.skinAfter != skinBefore;
    out.status = out.skinAfter == targetItem ? "target-observed" : "target-not-observed";
    return out;
}
#endif
