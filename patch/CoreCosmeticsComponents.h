#ifndef CORE_COSMETICS_COMPONENTS_H
#define CORE_COSMETICS_COMPONENTS_H
#include "CoreCosmeticsRead.h"

// Actions review of native Actor::FindComponentByClass (0xA149148) and
// bit iterators 0x27026C8/0x2702724, pinned to game 1.38.12/15915.
// OwnedComponents is a sparse set: 16-byte entries and an allocation bitmap.
// Bitmap words live inline at +0x450 unless +0x460 supplies heap storage.
#define CCP_MAX_COMPONENT_SLOTS 512
typedef enum { CCPClassUnreadable, CCPClassDifferent, CCPClassMatches } CCPClassResult;
typedef enum {
    CCPComponentFound, CCPComponentNotFound, CCPComponentReadFailed,
    CCPComponentLayoutInvalid, CCPComponentChanged, CCPComponentAmbiguous
} CCPComponentStatus;
typedef struct {
    CCPComponentStatus status;
    uint32_t slots, occupied;
    bool headerRead, bitmapRead, stable, heapBitmap;
} CCPComponentDiagnostic;

static inline CCPClassResult CCPObjectIsA(CCPReader reader, uint64_t object, uint64_t targetClass) {
    uint64_t objectClass = 0, repeatedClass = 0, ancestor = 0;
    uint8_t targetInfo[16], objectInfo[16], repeatedInfo[16];
    if (!CCPPointer(object) || !CCPPointer(targetClass) ||
        !CCPReadBytes(reader, object + 0x10, &objectClass, 8) || !CCPPointer(objectClass) ||
        !CCPReadBytes(reader, targetClass + 0x88, targetInfo, sizeof(targetInfo)) ||
        !CCPReadBytes(reader, objectClass + 0x88, objectInfo, sizeof(objectInfo))) return CCPClassUnreadable;
    int32_t targetDepth = (int32_t)CCPU32(targetInfo + 8), depth = (int32_t)CCPU32(objectInfo + 8);
    if (targetDepth < 0 || targetDepth > 64 || depth < 0 || depth > 64) return CCPClassUnreadable;
    if (targetDepth > depth) return CCPClassDifferent;
    uint64_t ancestors = CCPU64(objectInfo);
    if (!CCPPointer(ancestors) || !CCPReadBytes(reader, ancestors + (uint64_t)targetDepth * 8, &ancestor, 8) ||
        !CCPReadBytes(reader, object + 0x10, &repeatedClass, 8) || repeatedClass != objectClass ||
        !CCPReadBytes(reader, objectClass + 0x88, repeatedInfo, sizeof(repeatedInfo)) ||
        memcmp(objectInfo, repeatedInfo, sizeof(objectInfo)) ||
        !CCPReadBytes(reader, targetClass + 0x88, repeatedInfo, sizeof(repeatedInfo)) ||
        memcmp(targetInfo, repeatedInfo, sizeof(targetInfo))) return CCPClassUnreadable;
    return ancestor == targetClass + 0x88 ? CCPClassMatches : CCPClassDifferent;
}

static inline bool CCPFindOwnedComponent(CCPReader reader, uint64_t actor, uint64_t componentClass,
                                        uint64_t *component, CCPComponentDiagnostic *out) {
    if (!component || !out) return false;
    *component = 0;
    memset(out, 0, sizeof(*out));
    out->status = CCPComponentReadFailed;
    uint8_t header[0x30], after[sizeof(header)];
    if (!CCPPointer(actor) || !CCPPointer(componentClass) ||
        !CCPReadBytes(reader, actor + 0x440, header, sizeof(header))) return false;
    out->headerRead = true;
    uint64_t data = CCPU64(header), heap = CCPU64(header + 0x20);
    int32_t count = (int32_t)CCPU32(header + 8), capacity = (int32_t)CCPU32(header + 12);
    int32_t bits = (int32_t)CCPU32(header + 0x28), bitCapacity = (int32_t)CCPU32(header + 0x2C);
    if (count < 0 || count > CCP_MAX_COMPONENT_SLOTS || capacity < count || capacity > 1024 ||
        bits != count || bitCapacity < bits || bitCapacity > 4096 ||
        (count && !CCPPointer(data)) || (heap && !CCPPointer(heap)) || (!heap && bits > 128)) {
        out->status = CCPComponentLayoutInvalid; return false;
    }
    out->slots = (uint32_t)count; out->heapBitmap = heap != 0;
    uint32_t flags[CCP_MAX_COMPONENT_SLOTS / 32] = {0}, flagsAfter[CCP_MAX_COMPONENT_SLOTS / 32] = {0};
    uint8_t entries[CCP_MAX_COMPONENT_SLOTS * 16], entriesAfter[sizeof(entries)];
    size_t flagBytes = ((size_t)count + 31) / 32 * 4, entryBytes = (size_t)count * 16;
    uint64_t bitmap = heap ? heap : actor + 0x450;
    if (count && (!CCPReadBytes(reader, bitmap, flags, flagBytes) ||
                  !CCPReadBytes(reader, data, entries, entryBytes))) return false;
    out->bitmapRead = true;
    uint64_t found = 0, foundIndex = 0;
    unsigned matches = 0;
    for (int32_t i = 0; i < count; i++) {
        if (!(flags[(uint32_t)i / 32] & (UINT32_C(1) << ((uint32_t)i % 32)))) continue;
        out->occupied++;
        uint64_t candidate = CCPU64(entries + (size_t)i * 16);
        if (!candidate) continue; // The native lookup also skips null entries.
        CCPClassResult result = CCPObjectIsA(reader, candidate, componentClass);
        if (result == CCPClassUnreadable) return false;
        if (result == CCPClassMatches) { found = candidate; foundIndex = (uint64_t)i; matches++; }
    }
    if ((count && (!CCPReadBytes(reader, bitmap, flagsAfter, flagBytes) || memcmp(flags, flagsAfter, flagBytes) ||
                   !CCPReadBytes(reader, data, entriesAfter, entryBytes) || memcmp(entries, entriesAfter, entryBytes))) ||
        !CCPReadBytes(reader, actor + 0x440, after, sizeof(after)) || memcmp(header, after, sizeof(header))) {
        out->status = CCPComponentChanged; return false;
    }
    out->stable = true;
    if (!matches) { out->status = CCPComponentNotFound; return false; }
    if (matches != 1) { out->status = CCPComponentAmbiguous; return false; }
    uint64_t selectedAfter = 0;
    if (!CCPReadBytes(reader, data + foundIndex * 16, &selectedAfter, 8) || selectedAfter != found ||
        CCPObjectIsA(reader, found, componentClass) != CCPClassMatches) {
        out->status = CCPComponentChanged; return false;
    }
    *component = found;
    out->status = CCPComponentFound;
    return true;
}
#endif
