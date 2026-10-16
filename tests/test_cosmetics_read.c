#include <assert.h>
#include <stdio.h>
#include "../patch/CoreCosmeticsRead.h"

static const uint64_t Base = UINT64_C(0x100000000);
typedef struct {
    uint8_t bytes[32768];
    unsigned calls, itemReads, headerReads;
    bool fail, mutateItems, mutateHeader;
} Fixture;
static bool Read(void *context, uint64_t address, void *out, size_t length) {
    Fixture *f = context;
    f->calls++;
    if (f->fail || address < Base || address - Base > sizeof(f->bytes) ||
        length > sizeof(f->bytes) - (address - Base)) { memset(out, 0xCC, length); return false; }
    memcpy(out, f->bytes + address - Base, length);
    if (address == Base + 4096 && ++f->itemReads == 2 && f->mutateItems) ((uint8_t *)out)[4] ^= 1;
    if (address == Base + 2048 && ++f->headerReads == 2 && f->mutateHeader) ((uint8_t *)out)[8] ^= 1;
    return true;
}
static void Put32(Fixture *f, size_t offset, uint32_t value) { memcpy(f->bytes + offset, &value, 4); }
static void Package(Fixture *f) {
    memset(f, 0, sizeof(*f));
    Put32(f, 0, 0xFEEDFACF); Put32(f, 4, 0x100000C);
    Put32(f, 16, 1); Put32(f, 20, 24); Put32(f, 32, 0x1B); Put32(f, 36, 24);
    memcpy(f->bytes + 40, CCPGameUUID, 16);
}
static void Array(Fixture *f, int32_t count, int32_t capacity) {
    CCPArray array = {Base + 4096, count, capacity};
    memcpy(f->bytes + 2048, &array, sizeof(array));
    f->itemReads = f->headerReads = 0;
    Put32(f, 4096, 1); Put32(f, 4100, 101004); Put32(f, 4096 + 0x18, 2);
    Put32(f, 4096 + 0x1C, 3); Put32(f, 4096 + 0x30, 7);
}
int main(void) {
    Fixture f; Package(&f);
    CCPReader reader = {Read, &f};
    assert(CCPIdentity(reader, Base, CCPGameUUID));
    f.bytes[40] ^= 1; assert(!CCPIdentity(reader, Base, CCPGameUUID)); f.bytes[40] ^= 1;
    Put32(&f, 4, 0x1000007); assert(!CCPIdentity(reader, Base, CCPGameUUID));
    Package(&f); Put32(&f, 36, 7); assert(!CCPIdentity(reader, Base, CCPGameUUID));
    Package(&f); Put32(&f, 20, 32); assert(!CCPIdentity(reader, Base, CCPGameUUID));
    Package(&f); Put32(&f, 16, 2); Put32(&f, 20, 48);
    memcpy(f.bytes + 56, f.bytes + 32, 24); assert(!CCPIdentity(reader, Base, CCPGameUUID));
    Package(&f); f.fail = true; assert(!CCPIdentity(reader, Base, CCPGameUUID));
    uint8_t buffer[16]; memset(buffer, 0xFF, sizeof(buffer));
    assert(!CCPReadBytes(reader, Base, buffer, sizeof(buffer)));
    for (size_t i = 0; i < sizeof(buffer); i++) assert(buffer[i] == 0);
    unsigned calls = f.calls;
    assert(!CCPReadBytes(reader, UINT64_MAX - 4, buffer, 8)); assert(f.calls == calls);
    Package(&f); Array(&f, 1, 4); CCPItems items;
    assert(CCPReadItems(reader, Base + 2048, &items));
    assert(items.count == 1 && items.items[0].type == 1 && items.items[0].item == 101004);
    assert(items.items[0].gender == 2 && items.items[0].operation == 3 && items.items[0].version == 7);
    Array(&f, 0, 0); assert(CCPReadItems(reader, Base + 2048, &items) && items.count == 0);
    const int32_t invalid[][2] = {{-1,0}, {129,129}, {1,0}, {0,513}};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        Array(&f, invalid[i][0], invalid[i][1]); memset(&items, 0xFF, sizeof(items));
        assert(!CCPReadItems(reader, Base + 2048, &items) && items.count == 0);
        assert(f.itemReads == 0);
    }
    Array(&f, 1, 1); f.mutateItems = true;
    assert(!CCPReadItems(reader, Base + 2048, &items) && items.count == 0);
    Array(&f, 1, 1); f.mutateItems = false; f.mutateHeader = true;
    assert(!CCPReadItems(reader, Base + 2048, &items) && items.count == 0);
    f.mutateHeader = false; Array(&f, 1, 1); uint64_t invalidPointer = UINT64_MAX;
    memcpy(f.bytes + 2048, &invalidPointer, 8);
    assert(!CCPReadItems(reader, Base + 2048, &items) && f.itemReads == 0);
    puts("PASS: exact UUID/architecture gate, malformed commands, overflow, failed reads, bounds and changing arrays");
    return 0;
}
