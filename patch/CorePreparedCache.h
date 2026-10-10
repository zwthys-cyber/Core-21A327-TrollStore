#ifndef CORE_PREPARED_CACHE_H
#define CORE_PREPARED_CACHE_H
#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#import <sys/sysctl.h>
#import <sys/stat.h>
#import <sys/time.h>
#import <fcntl.h>
#import <unistd.h>
#import <errno.h>
#import "CoreBootPolicy.h"

static inline NSError *CBCPreparedError(NSString *reason) {
    return [NSError errorWithDomain:@"CorePreparedCache" code:1
        userInfo:@{NSLocalizedDescriptionKey:reason}];
}

static inline NSDictionary *CBCBootStamp(void) {
    struct timeval value = {0}; size_t size = sizeof(value);
    if (sysctlbyname("kern.boottime", &value, &size, NULL, 0) || size != sizeof(value) ||
        value.tv_sec <= 0 || value.tv_usec < 0 || value.tv_usec >= 1000000) return nil;
    return @{@"seconds":@(value.tv_sec), @"microseconds":@(value.tv_usec)};
}

static inline NSString *CBCDigest(NSData *data) {
    if (data.length > 0x20000000) return nil;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString *result = [NSMutableString stringWithCapacity:64];
    for (unsigned int i = 0; i < sizeof(digest); i++) [result appendFormat:@"%02x", digest[i]];
    return result;
}

// Read through one descriptor, reject links and unstable files, and bound allocation.
static inline NSData *CBCReadRegular(NSURL *url, size_t limit, NSError **error) {
    int fd = open(url.fileSystemRepresentation, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { if (error) *error = CBCPreparedError(@(strerror(errno))); return nil; }
    struct stat before, after;
    NSData *result = nil;
    if (!fstat(fd, &before) && S_ISREG(before.st_mode) && before.st_size > 0 &&
        (uint64_t)before.st_size <= limit) {
        NSMutableData *data = [NSMutableData dataWithLength:(NSUInteger)before.st_size];
        size_t done = 0;
        while (done < data.length) {
            ssize_t count = read(fd, (char *)data.mutableBytes + done, data.length - done);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            done += (size_t)count;
        }
        if (done == data.length && !fstat(fd, &after) && before.st_size == after.st_size &&
            before.st_mtime == after.st_mtime && before.st_ctime == after.st_ctime) result = data;
    }
    close(fd);
    if (!result && error) *error = CBCPreparedError(@"Input is not a stable regular file of the expected size");
    return result;
}

static inline BOOL CBCInteger(id value) {
    return [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) != CFBooleanGetTypeID() &&
        !strchr("fd", [value objCType][0]);
}

// A prepared copy is accepted only for this device, OS build and current boot.
// The original Core publisher and validator still check its actual kernel format.
static inline NSDictionary *CBCReadPrepared(NSURL *directory, NSString *product, NSString *build,
                                           NSString *board, NSDictionary *stamp, NSError **error) {
    NSData *encoded = CBCReadRegular([directory URLByAppendingPathComponent:@"manifest.json"], 16384, error);
    if (!encoded) return nil;
    id object = [NSJSONSerialization JSONObjectWithData:encoded options:0 error:error];
    if (![object isKindOfClass:NSDictionary.class]) {
        if (error) *error = CBCPreparedError(@"Prepared manifest is not a dictionary"); return nil;
    }
    NSDictionary *manifest = object;
    for (NSString *key in @[@"product", @"build", @"board", @"boot_hash", @"source_path", @"sha256"]) {
        if (![manifest[key] isKindOfClass:NSString.class]) {
            if (error) *error = CBCPreparedError(@"Prepared manifest contains an invalid string field"); return nil;
        }
    }
    id boot = manifest[@"boot_time"];
    if (!CBCInteger(manifest[@"protocol"]) || [manifest[@"protocol"] longLongValue] != 1 ||
        !CBCInteger(manifest[@"bytes"]) || [manifest[@"bytes"] longLongValue] < 4 ||
        [manifest[@"bytes"] unsignedLongLongValue] > 0x20000000 ||
        ![boot isKindOfClass:NSDictionary.class] || !CBCInteger(boot[@"seconds"]) ||
        !CBCInteger(boot[@"microseconds"]) || !stamp ||
        ![boot[@"seconds"] isEqual:stamp[@"seconds"]] ||
        ![boot[@"microseconds"] isEqual:stamp[@"microseconds"]] ||
        !CBCIdentityMatches([manifest[@"product"] UTF8String], [manifest[@"build"] UTF8String],
            [manifest[@"board"] UTF8String], product.UTF8String, build.UTF8String, board.UTF8String) ||
        !CBCPathMatchesBoot([manifest[@"source_path"] UTF8String], [manifest[@"boot_hash"] UTF8String])) {
        if (error) *error = CBCPreparedError(@"Prepared cache identity, boot time, path or size mismatch"); return nil;
    }
    NSData *data = CBCReadRegular([directory URLByAppendingPathComponent:@"kernelcache.bin"], 0x20000000, error);
    if (!data) return nil;
    if (data.length != [manifest[@"bytes"] unsignedLongLongValue] ||
        ![CBCDigest(data) isEqualToString:manifest[@"sha256"]]) {
        if (error) *error = CBCPreparedError(@"Prepared cache size or SHA-256 mismatch"); return nil;
    }
    return @{@"data":data, @"source_path":manifest[@"source_path"]};
}
#endif
