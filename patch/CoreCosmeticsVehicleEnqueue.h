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
#define CCP_ENQUEUE_COMPLETION_GADGET_RVA 0x334ad4u
#define CCP_ENQUEUE_TFUNCTION_OFFSET 0x000u
#define CCP_ENQUEUE_EVENT_OFFSET 0x100u
#define CCP_ENQUEUE_COMPLETION_OFFSET 0x180u

typedef uint64_t (*CCPEnqueueRemoteCall)(uint64_t timeout, uint64_t function, const char *label,
                                         uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                                         uint64_t a5, uint64_t a6, uint64_t a7,
                                         uint64_t *out0, uint64_t *out1);
typedef CCPNativeWriteResult (*CCPEnqueueWriteFn)(uint32_t, uint64_t, const void *, const void *, size_t);
typedef bool (*CCPEnqueueReadFn)(uint32_t, uint64_t, size_t, void *);

typedef struct {
    const char *status;
    uint32_t remoteStatus;
    uint64_t codePage, dataPage, mappingLength;
    uint32_t skinBefore, skinAfter, targetItem, targetResource;
    bool skinChanged, putOnAttempted, taskCompleted, taskRejectedContext, mappingReleased;
} CCPVehicleEnqueueResult;
typedef struct {
    uint32_t completionValue;
    bool completionRead, mappingReleased;
} CCPVehicleMappingRelease;

static inline bool CCPVehicleTargetObserved(const CCPVehicleEnqueueResult *result) {
    return result && result->putOnAttempted && result->status &&
        strcmp(result->status, "target-observed") == 0 &&
        result->skinAfter == result->targetItem;
}

static inline bool CCPVehiclePutOnDispatched(const CCPVehicleEnqueueResult *result) {
    return CCPVehicleTargetObserved(result) ||
        (result && result->putOnAttempted && result->status &&
         (!strcmp(result->status, "target-not-observed") ||
          !strcmp(result->status, "task-not-completed") ||
          !strcmp(result->status, "create-and-dispatch-call-failed") ||
          (!strcmp(result->status, "task-context-changed") && !result->mappingReleased)));
}

static inline bool CCPVehicleRestoreObserved(const CCPVehicleEnqueueResult *result) {
    return result && result->status &&
        (CCPVehicleTargetObserved(result) ||
         (result->taskCompleted && strcmp(result->status, "skipped-already-target") == 0));
}

static inline bool CCPStandardVehicleResourceIDForItem(uint32_t itemID, uint32_t *resourceID) {
    if (!resourceID || itemID < 3300000u || itemID >= 3400000u) return false;
    *resourceID = itemID * 100u;
    return true;
}

static inline CCPVehicleMappingRelease CCPReleaseCompletedVehicleMapping(
    uint32_t pid, uint64_t mapping, uint64_t dataPage, uint64_t mappingLength,
    CCPEnqueueReadFn read, CCPEnqueueRemoteCall remoteCall) {
    CCPVehicleMappingRelease out = {0};
    if (!mapping || !dataPage || !mappingLength || !read || !remoteCall) return out;
    out.completionRead = read(pid, dataPage + CCP_ENQUEUE_COMPLETION_OFFSET,
                              sizeof(out.completionValue), &out.completionValue);
    if (!out.completionRead || (out.completionValue != 1 && out.completionValue != 2)) return out;
    void *munmapSym = dlsym(RTLD_DEFAULT, "munmap");
    if (!munmapSym) return out;
#if __has_feature(ptrauth_calls)
    uint64_t munmapAddr = (uint64_t)ptrauth_strip(munmapSym, ptrauth_key_function_pointer);
#else
    uint64_t munmapAddr = (uint64_t)munmapSym;
#endif
    uint64_t unmapped = UINT64_MAX, unused = 0;
    uint64_t status = remoteCall(10000, munmapAddr, "munmap-completed-vehicle-task",
        mapping, mappingLength, 0, 0, 0, 0, 0, 0, &unmapped, &unused);
    out.mappingReleased = (uint32_t)status == 1 && unmapped == 0;
    return out;
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
    uint32_t pid, uint64_t game, uint64_t avatar, uint64_t controller,
    uint64_t vehicleComponentOffset, uint64_t vehicleComponentHeap,
    uint64_t vehicleComponentBitmapWord, uint32_t vehicleComponentBit,
    uint64_t vehicleUser, uint64_t vehicle, uint32_t skinBefore, uint32_t alternateSkinBefore,
    uint32_t targetItem, uint32_t targetResource, bool forceDispatch,
    CCPEnqueueReadFn read, CCPEnqueueWriteFn write, CCPEnqueueRemoteCall remoteCall) {
    CCPVehicleEnqueueResult out = {0};
    out.status = "not-started";
    out.skinBefore = skinBefore;
    out.targetItem = targetItem;
    out.targetResource = targetResource;
    if (!targetItem || !targetResource) {
        out.status = "bad-arguments";
        return out;
    }
    if (!forceDispatch && skinBefore == targetItem) {
        out.status = "skipped-already-target";
        out.skinAfter = skinBefore;
        out.taskCompleted = true;
        return out;
    }
    if (!CCPPointer(game) || !CCPPointer(avatar) || !CCPPointer(controller) ||
        vehicleComponentOffset >= CCP_MAX_COMPONENT_SLOTS * 16u ||
        (vehicleComponentOffset & 15u) || !CCPPointer(vehicleComponentBitmapWord) ||
        !vehicleComponentBit || (vehicleComponentBit & (vehicleComponentBit - 1)) ||
        (vehicleComponentHeap && !CCPPointer(vehicleComponentHeap)) || !CCPPointer(vehicleUser) ||
        !CCPPointer(vehicle) || !read || !write ||
        !remoteCall) {
        out.status = "bad-arguments";
        return out;
    }
    void *mprotectSym = dlsym(RTLD_DEFAULT, "mprotect");
    void *mmapSym = dlsym(RTLD_DEFAULT, "mmap");
    void *munmapSym = dlsym(RTLD_DEFAULT, "munmap");
    void *icacheSym = dlsym(RTLD_DEFAULT, "sys_icache_invalidate");
    if (!mprotectSym || !mmapSym || !munmapSym || !icacheSym) {
        out.status = "vm-symbol-missing";
        return out;
    }
#if __has_feature(ptrauth_calls)
    uint64_t mprotectAddr = (uint64_t)ptrauth_strip(mprotectSym, ptrauth_key_function_pointer);
    uint64_t mmapAddr = (uint64_t)ptrauth_strip(mmapSym, ptrauth_key_function_pointer);
    uint64_t munmapAddr = (uint64_t)ptrauth_strip(munmapSym, ptrauth_key_function_pointer);
    uint64_t icacheAddr = (uint64_t)ptrauth_strip(icacheSym, ptrauth_key_function_pointer);
#else
    uint64_t mprotectAddr = (uint64_t)mprotectSym;
    uint64_t mmapAddr = (uint64_t)mmapSym;
    uint64_t munmapAddr = (uint64_t)munmapSym;
    uint64_t icacheAddr = (uint64_t)icacheSym;
#endif
    uint64_t page = (uint64_t)getpagesize();
    if (page < 0x1000 || page > 0x10000 || (page & (page - 1))) {
        out.status = "page-size-invalid";
        return out;
    }
    uint64_t mapping = 0, unused = 0;
    uint64_t mapStatus = remoteCall(10000, mmapAddr, "mmap-code-data", 0, page * 2,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, UINT64_MAX, 0, 0, 0, &mapping, &unused);
    if ((uint32_t)mapStatus != 1 || mapping == UINT64_MAX || !CCPPointer(mapping) || (mapping & (page - 1))) {
        out.status = "mmap-failed";
        return out;
    }
    uint64_t code = mapping, data = mapping + page;
    out.codePage = code;
    out.dataPage = data;
    out.mappingLength = page * 2;
    // code+0x00: x8-sret bridge for CreateAndDispatch.
    // code+0x40: framed callable; revalidate vehicle ownership on GameThread,
    // invoke PutOn, then tail-call a reviewed
    // game-image "str w1, [x0]; ret" gadget. Completion is published only
    // after execution has permanently left this disposable code page.
    const uint32_t cadCode[2] = {0xAA0303E8u, 0xD61F0080u};
    const uint32_t putOnCode[] = {
        0xA9BF7BFDu, 0x910003FDu, 0x580009C0u, 0x580009E2u, 0x58000A04u,
        0x58000A25u, 0x58000A46u, 0x58000A67u, 0xF94230A8u, 0xEB07011Fu,
        0x54000481u, 0x58000A27u, 0xB94000E8u, 0x58000A27u, 0x6A07011Fu,
        0x540003E0u, 0xF94220A5u, 0x8B0600A5u, 0xF94000A5u, 0xEB0400BFu,
        0x54000341u, 0xF940AC83u, 0xEB02007Fu, 0x540002E1u, 0xF9401003u,
        0xEB02007Fu, 0x54000281u, 0xF9485C03u, 0xEB02007Fu, 0x54000221u,
        0xF94F6C43u, 0xEB00007Fu, 0x540001C1u, 0xB960B043u, 0x580007C7u,
        0x6B07007Fu, 0x540000A0u, 0x580007A7u, 0x34000107u, 0x6B07007Fu,
        0x540000C1u, 0x58000761u, 0x58000790u, 0xD63F0200u, 0x52800021u,
        0x14000002u, 0x52800041u, 0x58000720u, 0x58000750u, 0xA8C17BFDu,
        0xD61F0200u
    };
    uint64_t avatarWord = avatar, vehicleWord = vehicle, userWord = vehicleUser;
    uint64_t controllerWord = controller, componentOffsetWord = vehicleComponentOffset;
    uint64_t componentHeapWord = vehicleComponentHeap;
    uint64_t componentBitmapWord = vehicleComponentBitmapWord;
    uint64_t componentBitWord = vehicleComponentBit;
    uint64_t expectedSkinWord = skinBefore, alternateSkinWord = alternateSkinBefore;
    uint64_t resWord = out.targetResource;
    uint64_t putOnFn = game + CCP_ENQUEUE_PUTON_RVA;
    uint64_t completion = data + CCP_ENQUEUE_COMPLETION_OFFSET;
    uint64_t completionGadget = game + CCP_ENQUEUE_COMPLETION_GADGET_RVA;
    uint64_t invoker = code + 0x40, tfunction = data + CCP_ENQUEUE_TFUNCTION_OFFSET;
    uint64_t event = data + CCP_ENQUEUE_EVENT_OFFSET;
    if (!CCPEnqueueWrite(read, write, pid, code + 0x00, cadCode, sizeof(cadCode)) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x40, putOnCode, sizeof(putOnCode)) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x180, &avatarWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x188, &vehicleWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x190, &userWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x198, &controllerWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1A0, &componentOffsetWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1A8, &componentHeapWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1B0, &componentBitmapWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1B8, &componentBitWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1C0, &expectedSkinWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1C8, &alternateSkinWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1D0, &resWord, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1D8, &putOnFn, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1E0, &completion, 8) ||
        !CCPEnqueueWrite(read, write, pid, code + 0x1E8, &completionGadget, 8) ||
        !CCPEnqueueWrite(read, write, pid, tfunction, &invoker, 8)) {
        out.status = "trampoline-write-failed";
        uint64_t unmapped = 0;
        uint64_t unmapStatus = remoteCall(10000, munmapAddr, "munmap-failed-setup",
            mapping, page * 2, 0, 0, 0, 0, 0, 0, &unmapped, &unused);
        out.mappingReleased = (uint32_t)unmapStatus == 1 && unmapped == 0;
        return out;
    }
    uint64_t cacheRet = 0;
    uint64_t cacheStatus = remoteCall(10000, icacheAddr, "sys_icache_invalidate",
        code, 0x1F0, 0, 0, 0, 0, 0, 0, &cacheRet, &unused);
    if ((uint32_t)cacheStatus != 1) {
        out.status = "icache-invalidate-failed";
        uint64_t unmapped = 0;
        uint64_t unmapStatus = remoteCall(10000, munmapAddr, "munmap-failed-icache",
            mapping, page * 2, 0, 0, 0, 0, 0, 0, &unmapped, &unused);
        out.mappingReleased = (uint32_t)unmapStatus == 1 && unmapped == 0;
        return out;
    }
    uint64_t mpRet = 0;
    uint64_t mpStatus = remoteCall(10000, mprotectAddr, "mprotect",
                                   code, page, (uint64_t)(PROT_READ | PROT_EXEC),
                                   0, 0, 0, 0, 0, &mpRet, &unused);
    out.remoteStatus = (uint32_t)mpStatus;
    if ((uint32_t)mpStatus != 1 || mpRet != 0) {
        out.status = "mprotect-failed";
        uint64_t unmapped = 0;
        uint64_t unmapStatus = remoteCall(10000, munmapAddr, "munmap-failed-mprotect",
            mapping, page * 2, 0, 0, 0, 0, 0, 0, &unmapped, &unused);
        out.mappingReleased = (uint32_t)unmapStatus == 1 && unmapped == 0;
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
    for (int settle = 0; settle < 300 && !out.taskCompleted && !out.taskRejectedContext; settle++) {
        uint32_t done = 0;
        if (read(pid, completion, sizeof(done), &done) && done == 1) out.taskCompleted = true;
        else if (done == 2) out.taskRejectedContext = true;
        if (!out.taskCompleted && !out.taskRejectedContext) usleep(50000);
    }
    if (!out.taskCompleted && !out.taskRejectedContext) {
        out.status = "task-not-completed";
        return out; // Mapping remains owned by the indeterminate task.
    }
    if (out.taskRejectedContext) out.status = "task-context-changed";
    // GameThread PutOn is async. A successful CAD return proves only that the
    // task was submitted; product success requires observing the exact target.
    out.skinAfter = skinBefore;
    if (!out.taskRejectedContext) {
        for (int settle = 0; settle < 100; settle++) {
            usleep(50000);
            uint32_t skinAfter[4] = {0};
            if (!read(pid, vehicle + 0x20AC, sizeof(skinAfter), skinAfter)) continue;
            out.skinAfter = skinAfter[1];
            if (skinAfter[1] == targetItem) break;
        }
        out.skinChanged = out.skinAfter != skinBefore;
        out.status = out.skinAfter == targetItem ? "target-observed" : "target-not-observed";
    }
    uint64_t unmapped = 0;
    uint64_t unmapStatus = remoteCall(10000, munmapAddr, "munmap-code-data",
        mapping, page * 2, 0, 0, 0, 0, 0, 0, &unmapped, &unused);
    out.mappingReleased = (uint32_t)unmapStatus == 1 && unmapped == 0;
    return out;
}
#endif
