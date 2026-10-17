#ifndef CORE_COSMETICS_READ_H
#define CORE_COSMETICS_READ_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Exact 1.38.12/15915 input. These are static reflection candidates; this
// adapter only samples them. Nothing in this interface can write memory.
#define CCP_MAX_ITEMS 128
#define CCP_ITEM_STRIDE 0x38
#define CCP_AVATAR_OFFSET 0x3F78
#define CCP_SYNC_OFFSET 0x1180
#define CCP_CLIENT_OFFSET 0x1190
static const uint8_t CCPGameUUID[16] = {
    0x34,0xb7,0x85,0xb2,0x0d,0xab,0x39,0x92,
    0x98,0x5d,0x35,0x9e,0x6b,0xf4,0x55,0x85
};
typedef bool (*CCPRead)(void *context, uint64_t address, void *output, size_t length);
typedef struct { CCPRead read; void *context; } CCPReader;
typedef struct { uint64_t data; int32_t count, capacity; } CCPArray;
typedef struct { int32_t type, item, gender, operation, version; } CCPItem;
typedef struct { uint32_t count; CCPItem items[CCP_MAX_ITEMS]; } CCPItems;

static inline uint32_t CCPU32(const void *p) {
    uint32_t value; memcpy(&value, p, sizeof(value)); return value;
}
static inline uint64_t CCPU64(const void *p) {
    uint64_t value; memcpy(&value, p, sizeof(value)); return value;
}
static inline bool CCPPointer(uint64_t p) {
    return p >= UINT64_C(0x100000000) && p < UINT64_C(0x800000000000) && !(p & 7);
}
static inline bool CCPReadBytes(CCPReader reader, uint64_t address, void *out, size_t length) {
    if (!out || !length || length > 16384 || address < UINT64_C(0x100000000) ||
        address >= UINT64_C(0x800000000000) || length > UINT64_C(0x800000000000) - address) return false;
    memset(out, 0, length);
    if (!reader.read || !reader.read(reader.context, address, out, length)) {
        memset(out, 0, length); return false;
    }
    return true;
}
static inline const char *CCPIdentityDetail(CCPReader reader, uint64_t base, const uint8_t uuid[16]) {
    uint8_t header[32], command[24];
    if (!CCPPointer(base)) return "image-pointer-unavailable";
    if (!CCPReadBytes(reader, base, header, sizeof(header))) return "image-header-unreadable";
    if (CCPU32(header) != 0xFEEDFACF || CCPU32(header + 4) != 0x100000C) return "image-header-not-macho";
    uint32_t count = CCPU32(header + 16), size = CCPU32(header + 20);
    if (!count || count > 512 || size < 8 || size > 65536) return "image-load-commands-unconfirmed";
    uint32_t offset = 0; unsigned matches = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (size - offset < 8 || !CCPReadBytes(reader, base + 32 + offset, command, 8))
            return "image-load-commands-unconfirmed";
        uint32_t kind = CCPU32(command), length = CCPU32(command + 4);
        if (length < 8 || (length & 7) || length > size - offset) return "image-load-commands-unconfirmed";
        if (kind == 0x1B) {
            if (length != 24 || ++matches > 1 ||
                !CCPReadBytes(reader, base + 32 + offset, command, sizeof(command)))
                return "image-uuid-unreadable";
            if (memcmp(command + 8, uuid, 16)) return "image-uuid-mismatch";
        }
        offset += length;
    }
    if (matches != 1 || offset != size) return "image-load-commands-unconfirmed";
    return NULL;
}
static inline bool CCPIdentity(CCPReader reader, uint64_t base, const uint8_t uuid[16]) {
    return CCPIdentityDetail(reader, base, uuid) == NULL;
}
static inline bool CCPReadItems(CCPReader reader, uint64_t address, CCPItems *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    CCPArray before = {0}, after = {0};
    if (!CCPReadBytes(reader, address, &before, sizeof(before)) || before.count < 0 ||
        before.count > CCP_MAX_ITEMS || before.capacity < before.count || before.capacity > 512 ||
        (before.count && !CCPPointer(before.data))) return false;
    uint8_t first[CCP_MAX_ITEMS * CCP_ITEM_STRIDE], second[sizeof(first)];
    size_t length = (size_t)before.count * CCP_ITEM_STRIDE;
    if (length && (!CCPReadBytes(reader, before.data, first, length) ||
                   !CCPReadBytes(reader, before.data, second, length) || memcmp(first, second, length))) return false;
    if (!CCPReadBytes(reader, address, &after, sizeof(after)) || memcmp(&before, &after, sizeof(before))) return false;
    for (int32_t i = 0; i < before.count; i++) {
        const uint8_t *item = first + (size_t)i * CCP_ITEM_STRIDE;
        // InstanceID and SynAdditionalData deliberately stay out of reports.
        out->items[i] = (CCPItem){(int32_t)CCPU32(item), (int32_t)CCPU32(item + 4),
            (int32_t)CCPU32(item + 0x18), (int32_t)CCPU32(item + 0x1C), (int32_t)CCPU32(item + 0x30)};
    }
    out->count = (uint32_t)before.count;
    return true;
}
#endif
