// Exercises the same filesystem intake used by Core, without executing its kernel backend.
#import <Foundation/Foundation.h>
#import <assert.h>
#import "../patch/CorePreparedCache.h"

static NSURL *Directory;
static NSDictionary *Stamp;
static NSDictionary *ValidManifest;
static NSData *Fixture;

static void WriteManifest(NSDictionary *manifest) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:manifest options:0 error:NULL];
    assert(data && [data writeToURL:[Directory URLByAppendingPathComponent:@"manifest.json"] atomically:YES]);
}

static NSDictionary *Read(NSError **error) {
    return CBCReadPrepared(Directory, @"iPhone16,2", @"21A327", @"D84AP", Stamp, error);
}

static void Reject(NSDictionary *manifest) {
    WriteManifest(manifest);
    NSError *error = nil;
    assert(!Read(&error) && error);
}

int main(void) {
    @autoreleasepool {
        assert(CBCBootStamp() && "The production current-boot stamp must be readable");
        Directory = [[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
            URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES];
        assert([NSFileManager.defaultManager createDirectoryAtURL:Directory withIntermediateDirectories:YES attributes:nil error:NULL]);
        Stamp = @{@"seconds":@10000, @"microseconds":@123};
        Fixture = [@"fixture current kernelcache" dataUsingEncoding:NSUTF8StringEncoding];
        NSString *digest = @"e3b1f1fcf01dafe19853b981037cd3ce0999174db759d514b4aa535a1e854539";
        assert([CBCDigest(Fixture) isEqual:digest]);
        NSString *hash = [@"" stringByPaddingToLength:96 withString:@"A" startingAtIndex:0];
        NSString *source = [NSString stringWithFormat:@"/private/preboot/%@/System/Library/Caches/com.apple.kernelcaches/kernelcache", hash];
        ValidManifest = @{@"protocol":@1, @"product":@"iPhone16,2", @"build":@"21A327", @"board":@"d84ap",
            @"boot_time":Stamp, @"boot_hash":hash, @"source_path":source, @"bytes":@(Fixture.length), @"sha256":digest};
        NSURL *cache = [Directory URLByAppendingPathComponent:@"kernelcache.bin"];
        assert([Fixture writeToURL:cache atomically:YES]);
        WriteManifest(ValidManifest);
        NSError *error = nil;
        NSDictionary *result = Read(&error);
        assert(result && !error && [result[@"data"] isEqual:Fixture] && [result[@"source_path"] isEqual:source]);
        NSArray *mutations = @[@{@"product":@"iPhone16,1"}, @{@"build":@"21A329"}, @{@"board":@"D83AP"},
            @{@"boot_time":@{@"seconds":@10001, @"microseconds":@123}},
            @{@"boot_time":@{@"seconds":@10000, @"microseconds":@124}},
            @{@"source_path":@"/private/preboot/OTHER/kernelcache"},
            @{@"source_path":[source stringByReplacingOccurrencesOfString:@"/System/" withString:@"/../System/"]},
            @{@"sha256":@"bad"}, @{@"bytes":@(Fixture.length + 1)}, @{@"bytes":@(-1)},
            @{@"bytes":@0x20000001}, @{@"bytes":@YES}, @{@"bytes":@26.5},
            @{@"protocol":@YES}, @{@"protocol":@2}, @{@"board":@42}, @{@"boot_time":@[]},
            @{@"boot_time":@{@"seconds":@10000, @"microseconds":@YES}}];
        for (NSDictionary *mutation in mutations) {
            NSMutableDictionary *manifest = [ValidManifest mutableCopy]; [manifest addEntriesFromDictionary:mutation]; Reject(manifest);
        }
        WriteManifest(ValidManifest);
        assert(!CBCReadPrepared(Directory, @"iPhone16,2", @"21A327", @"D84AP", nil, &error));
        NSMutableData *damaged = [Fixture mutableCopy]; ((unsigned char *)damaged.mutableBytes)[0] ^= 1;
        assert([damaged writeToURL:cache atomically:YES]); assert(!Read(&error));
        assert([NSFileManager.defaultManager removeItemAtURL:cache error:NULL]);
        NSURL *outside = [Directory URLByAppendingPathComponent:@"outside.bin"];
        assert([Fixture writeToURL:outside atomically:YES]);
        assert(!symlink(outside.fileSystemRepresentation, cache.fileSystemRepresentation)); assert(!Read(&error));
        assert([NSFileManager.defaultManager removeItemAtURL:cache error:NULL]);
        assert([NSFileManager.defaultManager createDirectoryAtURL:cache withIntermediateDirectories:NO attributes:nil error:NULL]);
        assert(!Read(&error));
        NSMutableData *oversized = [NSMutableData dataWithLength:16385];
        assert([oversized writeToURL:[Directory URLByAppendingPathComponent:@"manifest.json"] atomically:YES]); assert(!Read(&error));
        assert([NSFileManager.defaultManager removeItemAtURL:Directory error:NULL]);
        puts("PASS: intact transfer; wrong device/build/board, stale boot, path escape, digest/size/types, missing boot stamp, damaged copy, symlink, directory and oversized manifest rejected");
    }
    return 0;
}
