// Run only on GitHub Actions. Tests the exact file parser and resource scanner.
#import <Foundation/Foundation.h>
#include <assert.h>
#import "../inspection/GameSourceInventory.h"

static NSURL *FixtureRoot;
static NSURL *Executable;

static void Put32(NSMutableData *data, NSUInteger offset, uint32_t value, BOOL big) {
    uint8_t *p = (uint8_t *)data.mutableBytes + offset;
    for (NSUInteger i = 0; i < 4; i++) p[i] = (uint8_t)(value >> ((big ? 3 - i : i) * 8));
}
static void Put64(NSMutableData *data, NSUInteger offset, uint64_t value, BOOL big) {
    uint8_t *p = (uint8_t *)data.mutableBytes + offset;
    for (NSUInteger i = 0; i < 8; i++) p[i] = (uint8_t)(value >> ((big ? 7 - i : i) * 8));
}
static NSMutableData *Thin(BOOL big, uint32_t cryptID) {
    NSMutableData *data = [NSMutableData dataWithLength:32 + 24 + 24 + 24];
    Put32(data, 0, 0xfeedfacf, big); Put32(data, 4, 0x0100000c, big);
    Put32(data, 8, 0x80000002, big); Put32(data, 12, 2, big);
    Put32(data, 16, 3, big); Put32(data, 20, 72, big);
    Put32(data, 32, 0x1b, big); Put32(data, 36, 24, big);
    for (NSUInteger i = 0; i < 16; i++) ((uint8_t *)data.mutableBytes)[40 + i] = (uint8_t)i;
    Put32(data, 56, 0x2c, big); Put32(data, 60, 24, big); Put32(data, 72, cryptID, big);
    Put32(data, 80, 0x32, big); Put32(data, 84, 24, big);
    Put32(data, 88, 2, big); Put32(data, 92, 0x110000, big); Put32(data, 96, 0x120100, big);
    return data;
}
static NSDictionary *Inspect(NSData *data) {
    assert([data writeToURL:Executable atomically:YES]);
    return CSIInspectExecutable(Executable);
}
static NSDictionary *FirstSlice(NSDictionary *report) {
    assert([report[@"status"] isEqual:@"inspected"]);
    NSArray *slices = report[@"slices"]; assert(slices.count == 1);
    return slices.firstObject;
}
static void RejectThin(NSMutableData *data) {
    assert([FirstSlice(Inspect(data))[@"status"] isEqual:@"unavailable"]);
}
static NSMutableData *Fat(BOOL big, BOOL wide) {
    NSMutableData *thin = Thin(NO, 1);
    NSMutableData *data = [NSMutableData dataWithLength:4096 + thin.length];
    Put32(data, 0, wide ? 0xcafebabf : 0xcafebabe, big); Put32(data, 4, 1, big);
    Put32(data, 8, 0x0100000c, big); Put32(data, 12, 0x80000002, big);
    if (wide) { Put64(data, 16, 4096, big); Put64(data, 24, thin.length, big); Put32(data, 32, 12, big); }
    else { Put32(data, 16, 4096, big); Put32(data, 20, (uint32_t)thin.length, big); Put32(data, 24, 12, big); }
    [data replaceBytesInRange:NSMakeRange(4096, thin.length) withBytes:thin.bytes];
    return data;
}
static void WriteAsset(NSString *name) {
    NSURL *file = [FixtureRoot URLByAppendingPathComponent:name];
    assert([NSFileManager.defaultManager createDirectoryAtURL:[file URLByDeletingLastPathComponent]
        withIntermediateDirectories:YES attributes:nil error:NULL]);
    assert([[NSData dataWithBytes:"fixture" length:7] writeToURL:file atomically:YES]);
}

int main(void) {
    @autoreleasepool {
        FixtureRoot = [[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
            URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES];
        assert([NSFileManager.defaultManager createDirectoryAtURL:FixtureRoot
            withIntermediateDirectories:YES attributes:nil error:NULL]);
        Executable = [FixtureRoot URLByAppendingPathComponent:@"Main"];
        for (NSUInteger big = 0; big < 2; big++) {
            for (uint32_t cryptID = 0; cryptID < 2; cryptID++) {
                NSDictionary *slice = FirstSlice(Inspect(Thin((BOOL)big, cryptID)));
                assert([slice[@"status"] isEqual:@"parsed"]);
                assert([slice[@"uuid"] isEqual:@"00010203-0405-0607-0809-0A0B0C0D0E0F"]);
                assert([slice[@"cryptid"] unsignedIntValue] == cryptID);
                assert([slice[@"file_encryption"] isEqual:cryptID ? @"encrypted" : @"declared-unencrypted"]);
                assert([slice[@"minimum_os"] isEqual:@"17.0.0"] && [slice[@"sdk"] isEqual:@"18.1.0"]);
                assert([slice[@"cpu_subtype"] unsignedIntValue] == 0x80000002);
            }
            for (NSUInteger wide = 0; wide < 2; wide++) {
                NSDictionary *slice = FirstSlice(Inspect(Fat((BOOL)big, (BOOL)wide)));
                assert([slice[@"slice_offset"] unsignedLongLongValue] == 4096 && [slice[@"cryptid"] isEqual:@1]);
            }
        }
        NSMutableData *invalid = Thin(NO, 0); Put32(invalid, 36, 0, NO); RejectThin(invalid);
        invalid = Thin(NO, 0); Put32(invalid, 36, 25, NO); RejectThin(invalid);
        invalid = Thin(NO, 0); Put32(invalid, 20, 1024 * 1024 + 1, NO); RejectThin(invalid);
        invalid = Thin(NO, 0); Put32(invalid, 16, 8193, NO); RejectThin(invalid);
        invalid = Thin(NO, 0); Put32(invalid, 36, 80, NO); RejectThin(invalid);
        invalid = Thin(NO, 0); Put32(invalid, 100, 1, NO); RejectThin(invalid);
        invalid = Thin(NO, 0); Put32(invalid, 56, 0x1b, NO); RejectThin(invalid);
        invalid = Thin(NO, 0); [invalid setLength:32]; RejectThin(invalid);
        invalid = Thin(NO, 0); [invalid setLength:16]; RejectThin(invalid);
        invalid = Thin(NO, 0); Put32(invalid, 16, 0, NO); RejectThin(invalid);
        invalid = Fat(YES, YES); Put64(invalid, 16, UINT64_MAX, YES);
        assert([Inspect(invalid)[@"status"] isEqual:@"unavailable"]);
        invalid = Fat(YES, NO); Put32(invalid, 16, 8, YES);
        assert([Inspect(invalid)[@"status"] isEqual:@"unavailable"]);
        invalid = Fat(YES, NO); Put32(invalid, 20, UINT32_MAX, YES);
        assert([Inspect(invalid)[@"status"] isEqual:@"unavailable"]);
        invalid = Fat(YES, NO); Put32(invalid, 12, 0, YES);
        assert([Inspect(invalid)[@"status"] isEqual:@"unavailable"]);
        invalid = Fat(YES, NO); Put32(invalid, 4, 17, YES);
        assert([Inspect(invalid)[@"status"] isEqual:@"unavailable"]);
        invalid = Fat(YES, NO); [invalid setLength:10];
        assert([Inspect(invalid)[@"status"] isEqual:@"unavailable"]);
        assert([Inspect([NSData data])[@"status"] isEqual:@"unavailable"]);
        assert([Inspect([@"not a mach-o executable" dataUsingEncoding:NSUTF8StringEncoding])[@"status"] isEqual:@"unavailable"]);
        assert([CSIInspectExecutable(FixtureRoot)[@"status"] isEqual:@"unavailable"]);
        assert([Thin(NO, 1) writeToURL:Executable atomically:YES]);
        NSURL *link = [FixtureRoot URLByAppendingPathComponent:@"LinkedMain"];
        assert(symlink(Executable.fileSystemRepresentation, link.fileSystemRepresentation) == 0);
        assert([CSIInspectExecutable(link)[@"status"] isEqual:@"unavailable"]);
        NSURL *fifo = [FixtureRoot URLByAppendingPathComponent:@"Pipe"];
        assert(mkfifo(fifo.fileSystemRepresentation, 0600) == 0);
        assert([CSIInspectExecutable(fifo)[@"status"] isEqual:@"unavailable"]);
        assert([CSIInventory([FixtureRoot URLByAppendingPathComponent:@"missing"], 10, 10, 10)[@"status"] isEqual:@"unavailable"]);
        assert([CSIInventory(Executable, 10, 10, 10)[@"status"] isEqual:@"unavailable"]);

        WriteAsset(@"Assets/character.pak"); WriteAsset(@"Assets/character.utoc"); WriteAsset(@"Assets/character.ucas");
        WriteAsset(@"Assets/Hair.uasset"); WriteAsset(@"Assets/Hair.uexp"); WriteAsset(@"Assets/Hair.ubulk");
        WriteAsset(@"Assets/body.glb"); WriteAsset(@"Assets/cloth.obj"); WriteAsset(@"Assets/plain.gltf");
        WriteAsset(@"Assets/plain.fbx"); WriteAsset(@"Assets/default.metallib");
        WriteAsset(@"Preferences/account.json"); WriteAsset(@"Assets/mesh.ini");
        NSURL *outside = [[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
            URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES];
        assert([NSFileManager.defaultManager createDirectoryAtURL:outside withIntermediateDirectories:YES attributes:nil error:NULL]);
        NSURL *outsideAsset = [outside URLByAppendingPathComponent:@"outside.pak"];
        assert([[NSData dataWithBytes:"outside" length:7] writeToURL:outsideAsset atomically:YES]);
        NSURL *linkedAsset = [FixtureRoot URLByAppendingPathComponent:@"escape.pak"];
        NSURL *linkedDirectory = [FixtureRoot URLByAppendingPathComponent:@"escape-directory"];
        assert(!symlink(outsideAsset.fileSystemRepresentation, linkedAsset.fileSystemRepresentation));
        assert(!symlink(outside.fileSystemRepresentation, linkedDirectory.fileSystemRepresentation));
        NSDictionary *inventory = CSIInventory(FixtureRoot, 1000, 1000, 30);
        assert([inventory[@"status"] isEqual:@"inventoried"] && ![inventory[@"truncated"] boolValue]);
        assert(![inventory[@"live_mesh_verified"] boolValue] && ![inventory[@"resource_contents_read"] boolValue]);
        assert([inventory[@"symlinks_skipped"] unsignedIntegerValue] >= 3);
        NSArray *resources = inventory[@"resources"];
        if (resources.count != 11) {
            NSData *encoded = [NSJSONSerialization dataWithJSONObject:inventory options:NSJSONWritingPrettyPrinted error:NULL];
            fprintf(stderr, "Unexpected fixture inventory: %s\n", [[NSString alloc] initWithData:encoded encoding:NSUTF8StringEncoding].UTF8String);
        }
        assert(resources.count == 11);
        NSUInteger hints = 0;
        for (NSDictionary *resource in resources) {
            assert([resource[@"relative_path"] hasPrefix:@"Assets/"] && [resource[@"bytes"] isEqual:@7]);
            assert(![resource[@"relative_path"] containsString:@"outside"]);
            hints += [resource[@"name_hint_only"] boolValue];
        }
        assert(hints == 8);
        assert([CSIInventory(FixtureRoot, 0, 1000, 30)[@"stop_reason"] isEqual:@"entry-limit"]);
        assert([CSIInventory(FixtureRoot, 1000, 0, 30)[@"stop_reason"] isEqual:@"resource-limit"]);
        assert([CSIInventory(FixtureRoot, 1000, 1000, 0)[@"stop_reason"] isEqual:@"time-limit"]);
        NSDictionary *limited = CSIInventory(FixtureRoot, 1000, 2, 30);
        assert([limited[@"stop_reason"] isEqual:@"resource-limit"] && [limited[@"resources"] count] == 2);
        assert([NSJSONSerialization dataWithJSONObject:inventory options:0 error:NULL]);
        assert([NSFileManager.defaultManager removeItemAtURL:FixtureRoot error:NULL]);
        assert([NSFileManager.defaultManager removeItemAtURL:outside error:NULL]);
        puts("PASS: thin and fat Mach-O byte orders, UUID/version/encryption, malformed bounds and tables, nonregular/symlink inputs, resource types, no content reads, path escape and scan limits");
    }
    return 0;
}
