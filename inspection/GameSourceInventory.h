// Bounded, read-only inspection of an installed application's files.
// Resource names are candidates, not proof of usable live mesh or mask data.
#import <Foundation/Foundation.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static NSDictionary *CSIError(NSString *message) {
    return @{@"status":@"unavailable", @"reason":message ?: @"unknown"};
}

static uint32_t CSI32(const uint8_t *p, BOOL big) {
    return big ? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]) :
        ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]);
}

static uint64_t CSI64(const uint8_t *p, BOOL big) {
    uint64_t a = CSI32(p, big), b = CSI32(p + 4, big);
    return big ? (a << 32 | b) : (b << 32 | a);
}

static BOOL CSIRead(int fd, uint64_t fileSize, uint64_t offset, void *buffer, size_t size) {
    if (offset > fileSize || size > fileSize - offset || offset > INT64_MAX) return NO;
    uint8_t *out = buffer;
    while (size) {
        ssize_t count = pread(fd, out, size, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return NO;
        out += count; offset += (uint64_t)count; size -= (size_t)count;
    }
    return YES;
}

static NSString *CSIVersion(uint32_t value) {
    return [NSString stringWithFormat:@"%u.%u.%u", value >> 16, (value >> 8) & 255, value & 255];
}

static NSDictionary *CSIMachSlice(int fd, uint64_t fileSize, uint64_t offset, uint64_t length) {
    uint8_t header[32];
    if (offset > fileSize || length > fileSize - offset || length < sizeof(header) ||
        !CSIRead(fd, fileSize, offset, header, sizeof(header))) return CSIError(@"truncated-slice");
    BOOL big = CSI32(header, YES) == 0xfeedfacf;
    if (!big && CSI32(header, NO) != 0xfeedfacf) return CSIError(@"unsupported-mach-header");
    uint32_t count = CSI32(header + 16, big), bytes = CSI32(header + 20, big);
    if (count > 8192 || bytes > 1024 * 1024 || bytes > length - sizeof(header) || count > bytes / 8)
        return CSIError(@"invalid-command-bounds");
    NSMutableData *commands = [NSMutableData dataWithLength:bytes];
    if (bytes && !CSIRead(fd, fileSize, offset + sizeof(header), commands.mutableBytes, bytes))
        return CSIError(@"commands-unreadable");
    const uint8_t *p = commands.bytes;
    NSMutableDictionary *result = [@{@"status":@"parsed", @"cpu_type":@(CSI32(header + 4, big)),
        @"cpu_subtype":@(CSI32(header + 8, big)), @"slice_bytes":@(length),
        @"slice_offset":@(offset), @"file_encryption":@"not-declared"} mutableCopy];
    uint32_t position = 0;
    BOOL sawUUID = NO, sawEncryption = NO;
    for (uint32_t i = 0; i < count; i++) {
        if (position > bytes || bytes - position < 8) return CSIError(@"truncated-command");
        uint32_t command = CSI32(p + position, big), size = CSI32(p + position + 4, big);
        if (size < 8 || size % 4 || size > bytes - position) return CSIError(@"invalid-command-size");
        if (command == 0x1b) {
            if (size != 24 || sawUUID) return CSIError(@"invalid-uuid-command");
            sawUUID = YES;
            NSUUID *uuid = [[NSUUID alloc] initWithUUIDBytes:p + position + 8];
            result[@"uuid"] = uuid.UUIDString;
        } else if (command == 0x2c || command == 0x21) {
            if (size < (command == 0x2c ? 24U : 20U) || sawEncryption)
                return CSIError(@"invalid-encryption-command");
            sawEncryption = YES;
            uint32_t cryptID = CSI32(p + position + 16, big);
            result[@"cryptid"] = @(cryptID);
            result[@"file_encryption"] = cryptID ? @"encrypted" : @"declared-unencrypted";
        } else if (command == 0x32) {
            if (size < 24) return CSIError(@"invalid-build-version-command");
            uint32_t tools = CSI32(p + position + 20, big);
            if (tools > (size - 24) / 8) return CSIError(@"invalid-build-tool-count");
            result[@"platform"] = @(CSI32(p + position + 8, big));
            result[@"minimum_os"] = CSIVersion(CSI32(p + position + 12, big));
            result[@"sdk"] = CSIVersion(CSI32(p + position + 16, big));
        } else if (command == 0x25) {
            if (size < 16) return CSIError(@"invalid-minimum-os-command");
            result[@"minimum_os"] = CSIVersion(CSI32(p + position + 8, big));
        }
        position += size;
    }
    if (position != bytes) return CSIError(@"command-table-length-mismatch");
    return result;
}

static NSDictionary *CSIInspectExecutable(NSURL *url) {
    int fd = open(url.fileSystemRepresentation, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return CSIError(@"file-unreadable");
    struct stat status;
    NSDictionary *result;
    if (fstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_size < 8) {
        result = CSIError(@"not-a-readable-regular-executable");
    } else {
        uint64_t total = (uint64_t)status.st_size;
        uint8_t prefix[8];
        if (!CSIRead(fd, total, 0, prefix, sizeof(prefix))) {
            result = CSIError(@"header-unreadable");
        } else if (CSI32(prefix, YES) == 0xfeedfacf || CSI32(prefix, NO) == 0xfeedfacf) {
            result = @{@"status":@"inspected", @"file_bytes":@(total),
                @"slices":@[CSIMachSlice(fd, total, 0, total)]};
        } else {
            uint32_t magicBig = CSI32(prefix, YES), magicLittle = CSI32(prefix, NO);
            BOOL big = magicBig == 0xcafebabe || magicBig == 0xcafebabf;
            uint32_t magic = big ? magicBig : magicLittle;
            uint32_t count = CSI32(prefix + 4, big), stride = magic == 0xcafebabf ? 32 : 20;
            if ((magic != 0xcafebabe && magic != 0xcafebabf) || !count || count > 16) {
                result = CSIError(@"unsupported-executable-header");
            } else {
                uint8_t table[16 * 32];
                if (!CSIRead(fd, total, 8, table, count * stride)) {
                    result = CSIError(@"truncated-fat-table");
                } else {
                    NSMutableArray *slices = [NSMutableArray new];
                    BOOL invalid = NO;
                    uint64_t tableEnd = 8 + (uint64_t)count * stride;
                    for (uint32_t i = 0; i < count; i++) {
                        const uint8_t *entry = table + i * stride;
                        uint64_t offset = stride == 32 ? CSI64(entry + 8, big) : CSI32(entry + 8, big);
                        uint64_t size = stride == 32 ? CSI64(entry + 16, big) : CSI32(entry + 12, big);
                        if (offset < tableEnd || offset > total || size > total - offset || !size) {
                            invalid = YES; break;
                        }
                        if (CSI32(entry, big) == 0x0100000c) {
                            NSDictionary *slice = CSIMachSlice(fd, total, offset, size);
                            if (![slice[@"status"] isEqual:@"parsed"] ||
                                ![slice[@"cpu_type"] isEqual:@(CSI32(entry, big))] ||
                                ![slice[@"cpu_subtype"] isEqual:@(CSI32(entry + 4, big))]) {
                                invalid = YES; break;
                            }
                            [slices addObject:slice];
                        }
                    }
                    result = invalid ? CSIError(@"invalid-fat-slice") :
                        @{@"status":@"inspected", @"file_bytes":@(total), @"slices":slices};
                }
            }
        }
    }
    close(fd);
    return result;
}

static NSDictionary *CSIInventory(NSURL *root, NSUInteger entryLimit, NSUInteger resourceLimit, NSTimeInterval seconds) {
    char canonical[PATH_MAX];
    if (!root || !realpath(root.fileSystemRepresentation, canonical)) return CSIError(@"directory-unavailable");
    NSURL *directory = [NSURL fileURLWithPath:@(canonical) isDirectory:YES];
    NSNumber *isDirectory;
    if (![directory getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:NULL] || !isDirectory.boolValue)
        return CSIError(@"not-a-directory");
    NSString *prefix = [directory.path stringByAppendingString:@"/"];
    NSMutableArray *resources = [NSMutableArray new], *errors = [NSMutableArray new];
    NSArray *keys = @[NSURLIsDirectoryKey, NSURLIsSymbolicLinkKey, NSURLIsRegularFileKey, NSURLFileSizeKey];
    NSDirectoryEnumerator *enumerator = [NSFileManager.defaultManager enumeratorAtURL:directory
        includingPropertiesForKeys:keys options:0 errorHandler:^BOOL(NSURL *url, NSError *error) {
            if (errors.count < 8) [errors addObject:@{@"entry":url.lastPathComponent ?: @"",
                @"domain":error.domain, @"code":@(error.code)}];
            return YES;
        }];
    if (!enumerator) return CSIError(@"enumeration-unavailable");
    NSSet *containers = [NSSet setWithArray:@[@"pak", @"utoc", @"ucas"]];
    NSSet *assets = [NSSet setWithArray:@[@"uasset", @"uexp", @"ubulk"]];
    NSSet *models = [NSSet setWithArray:@[@"gltf", @"glb", @"fbx", @"obj"]];
    NSTimeInterval deadline = NSProcessInfo.processInfo.systemUptime + MAX(0, seconds);
    NSUInteger visited = 0, links = 0;
    NSString *limit = nil;
    for (NSURL *child in enumerator) {
        @autoreleasepool {
            if (visited >= entryLimit) { limit = @"entry-limit"; break; }
            if (resources.count >= resourceLimit) { limit = @"resource-limit"; break; }
            if (NSProcessInfo.processInfo.systemUptime >= deadline) { limit = @"time-limit"; break; }
            visited++;
            NSDictionary *values = [child resourceValuesForKeys:keys error:NULL];
            if (!values) continue;
            if ([values[NSURLIsSymbolicLinkKey] boolValue]) { links++; [enumerator skipDescendants]; continue; }
            if (![values[NSURLIsRegularFileKey] boolValue]) continue;
            NSString *extension = child.pathExtension.lowercaseString, *kind = nil;
            if ([containers containsObject:extension]) kind = @"asset-container";
            else if ([assets containsObject:extension]) kind = @"unreal-asset";
            else if ([models containsObject:extension]) kind = @"model-file";
            else if ([extension isEqual:@"metallib"]) kind = @"shader-library";
            if (!kind || ![child.path hasPrefix:prefix]) continue;
            char resolved[PATH_MAX];
            if (!realpath(child.fileSystemRepresentation, resolved) || ![@(resolved) hasPrefix:prefix]) continue;
            NSString *relative = [child.path substringFromIndex:prefix.length];
            NSString *lower = relative.lowercaseString;
            BOOL hint = NO;
            for (NSString *term in @[@"skeletal", @"character", @"avatar", @"body", @"hair", @"cloth", @"mesh"])
                if ([lower containsString:term]) { hint = YES; break; }
            [resources addObject:@{@"relative_path":relative, @"bytes":values[NSURLFileSizeKey] ?: @0,
                @"kind":kind, @"name_hint_only":@(hint)}];
        }
    }
    [resources sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
        return [a[@"relative_path"] compare:b[@"relative_path"] options:NSLiteralSearch];
    }];
    return @{@"status":@"inventoried", @"entries_visited":@(visited), @"symlinks_skipped":@(links),
        @"truncated":@(limit != nil), @"stop_reason":limit ?: @"complete", @"errors":errors,
        @"resources":resources, @"resource_contents_read":@NO, @"live_mesh_verified":@NO};
}
