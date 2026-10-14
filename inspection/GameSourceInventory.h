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
#include <CommonCrypto/CommonDigest.h>

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
    // Foundation may spell /var and /private/var differently. Compare and
    // report paths using realpath on both sides, without a doubled slash.
    NSString *prefix = [@(canonical) stringByAppendingString:@"/"];
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
            if ([values[NSURLIsSymbolicLinkKey] boolValue]) {
                links++;
                if ([values[NSURLIsDirectoryKey] boolValue]) [enumerator skipDescendants];
                continue;
            }
            if (![values[NSURLIsRegularFileKey] boolValue]) continue;
            NSString *extension = child.pathExtension.lowercaseString, *kind = nil;
            if ([containers containsObject:extension]) kind = @"asset-container";
            else if ([assets containsObject:extension]) kind = @"unreal-asset";
            else if ([models containsObject:extension]) kind = @"model-file";
            else if ([extension isEqual:@"metallib"]) kind = @"shader-library";
            if (!kind) continue;
            char resolved[PATH_MAX];
            if (!realpath(child.fileSystemRepresentation, resolved) || ![@(resolved) hasPrefix:prefix]) continue;
            NSString *relative = [@(resolved) substringFromIndex:prefix.length];
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

// Read only bounded file edges, not a full archive or its resource index.
// Keep raw samples so format analysis does not guess game-specific layouts
// or treat an executable's cryptid as asset encryption.
static NSDictionary *CSIPakSample(NSURL *root, NSString *relative) {
    if (!root || !relative.length || [relative isAbsolutePath] ||
        ![relative.pathExtension.lowercaseString isEqual:@"pak"]) return CSIError(@"invalid-relative-pak-path");
    for (NSString *component in [relative componentsSeparatedByString:@"/"])
        if (!component.length || [component isEqual:@"."] || [component isEqual:@".."]) return CSIError(@"invalid-relative-pak-path");
    char rootPath[PATH_MAX], sourcePath[PATH_MAX];
    NSURL *candidate = [root URLByAppendingPathComponent:relative];
    struct stat linkStatus;
    if (!realpath(root.fileSystemRepresentation, rootPath) ||
        lstat(candidate.fileSystemRepresentation, &linkStatus) || !S_ISREG(linkStatus.st_mode) ||
        !realpath(candidate.fileSystemRepresentation, sourcePath)) return CSIError(@"pak-unavailable-or-nonregular");
    NSString *prefix = [@(rootPath) stringByAppendingString:@"/"];
    if (![@(sourcePath) hasPrefix:prefix]) return CSIError(@"pak-path-outside-root");
    int fd = open(candidate.fileSystemRepresentation, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return CSIError(@"pak-unreadable");
    struct stat before, after;
    if (fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size <= 0 ||
        before.st_dev != linkStatus.st_dev || before.st_ino != linkStatus.st_ino) {
        close(fd); return CSIError(@"pak-identity-unavailable");
    }
    uint64_t length = (uint64_t)before.st_size;
    NSUInteger firstLength = (NSUInteger)MIN(length, 64), lastLength = (NSUInteger)MIN(length, 4096);
    NSMutableData *first = [NSMutableData dataWithLength:firstLength], *last = [NSMutableData dataWithLength:lastLength];
    BOOL read = CSIRead(fd, length, 0, first.mutableBytes, first.length) &&
        CSIRead(fd, length, length - lastLength, last.mutableBytes, last.length);
    BOOL stable = !fstat(fd, &after) && before.st_size == after.st_size &&
        before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec && before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
        before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec && before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec;
    close(fd);
    if (!read) return CSIError(@"pak-edge-read-failed");
    if (!stable) return CSIError(@"pak-changed-during-read");
    return @{@"status":@"sampled", @"relative_path":relative, @"file_bytes":@(length),
        @"prefix_bytes":@(first.length), @"prefix_base64":[first base64EncodedStringWithOptions:0],
        @"tail_offset":@(length - last.length), @"tail_bytes":@(last.length),
        @"tail_base64":[last base64EncodedStringWithOptions:0], @"resource_index_read":@NO,
        @"asset_encryption_verified":@NO, @"live_mesh_verified":@NO};
}

// A name in an archive edge remains a candidate, not a verified mesh class.
static BOOL CSIHasClothingCandidate(NSDictionary *sample) {
    if (![sample[@"status"] isEqual:@"sampled"] ||
        [sample[@"file_bytes"] unsignedLongLongValue] > 8 * 1024 * 1024) return NO;
    NSData *tail = [[NSData alloc] initWithBase64EncodedString:sample[@"tail_base64"] options:0];
    if (!tail || tail.length > 4096) return NO;
    for (NSString *name in @[@"Avatar_AnimBP_Jacket_", @"SK_Jacket_", @"SK_Hair_", @"SK_Body_",
        @"SK_Pants_", @"SK_Shoes_", @"SK_Hat_", @"SK_Pendant_"]) {
        NSData *needle = [name dataUsingEncoding:NSASCIIStringEncoding];
        if ([tail rangeOfData:needle options:0 range:NSMakeRange(0, tail.length)].location != NSNotFound) return YES;
    }
    return NO;
}

// Copy a small candidate archive into the inspector's own export directory.
// Verify its sampled edges and actual size, copy through file descriptors,
// and publish a digest only after the source remains unchanged throughout.
static NSDictionary *CSICopyPakSample(NSURL *root, NSDictionary *sample, NSURL *directory) {
    NSString *relative = sample[@"relative_path"];
    NSDictionary *current = CSIPakSample(root, relative);
    if (![current[@"status"] isEqual:@"sampled"]) return current;
    for (NSString *field in @[@"file_bytes", @"prefix_base64", @"tail_base64"])
        if (![current[field] isEqual:sample[field]]) return CSIError(@"source-sample-changed");
    uint64_t length = [current[@"file_bytes"] unsignedLongLongValue];
    if (length > 8 * 1024 * 1024) return CSIError(@"candidate-exceeds-8-mib-limit");
    struct stat directoryStatus;
    if (lstat(directory.fileSystemRepresentation, &directoryStatus) || !S_ISDIR(directoryStatus.st_mode))
        return CSIError(@"export-directory-unavailable");
    NSURL *source = [root URLByAppendingPathComponent:relative];
    int input = open(source.fileSystemRepresentation, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (input < 0) return CSIError(@"source-open-failed");
    char rootPath[PATH_MAX], openedPath[PATH_MAX], resolvedPath[PATH_MAX];
    struct stat before, after;
    BOOL contained = realpath(root.fileSystemRepresentation, rootPath) &&
        !fcntl(input, F_GETPATH, openedPath) && realpath(openedPath, resolvedPath) &&
        [@(resolvedPath) hasPrefix:[@(rootPath) stringByAppendingString:@"/"]];
    if (!contained || fstat(input, &before) || !S_ISREG(before.st_mode) || (uint64_t)before.st_size != length) {
        close(input); return CSIError(@"source-identity-changed");
    }
    uint8_t edge[4096];
    NSData *prefix = [[NSData alloc] initWithBase64EncodedString:current[@"prefix_base64"] options:0];
    NSData *tail = [[NSData alloc] initWithBase64EncodedString:current[@"tail_base64"] options:0];
    BOOL edgeMatch = prefix.length <= sizeof(edge) && tail.length <= sizeof(edge) &&
        CSIRead(input, length, 0, edge, prefix.length) && !memcmp(edge, prefix.bytes, prefix.length) &&
        CSIRead(input, length, length - tail.length, edge, tail.length) && !memcmp(edge, tail.bytes, tail.length);
    if (!edgeMatch) { close(input); return CSIError(@"source-edges-changed"); }
    NSURL *destination = [directory URLByAppendingPathComponent:relative.lastPathComponent];
    int output = open(destination.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (output < 0) { close(input); return CSIError(@"export-file-create-failed"); }
    uint8_t buffer[65536], digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_CTX hash; CC_SHA256_Init(&hash);
    uint64_t copied = 0; BOOL success = YES;
    while (copied < length) {
        size_t amount = (size_t)MIN(length - copied, sizeof(buffer));
        if (!CSIRead(input, length, copied, buffer, amount)) { success = NO; break; }
        size_t written = 0;
        while (written < amount) {
            ssize_t n = write(output, buffer + written, amount - written);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { success = NO; break; }
            written += (size_t)n;
        }
        if (!success) break;
        CC_SHA256_Update(&hash, buffer, (CC_LONG)amount); copied += amount;
    }
    success = success && !fstat(input, &after) && before.st_size == after.st_size &&
        before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec && before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
        before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec && before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec;
    int closeResult = close(output); close(input);
    if (!success || closeResult) { unlink(destination.fileSystemRepresentation); return CSIError(@"copy-failed-or-source-changed"); }
    CC_SHA256_Final(digest, &hash);
    NSMutableString *hex = [NSMutableString new];
    for (NSUInteger i = 0; i < sizeof(digest); i++) [hex appendFormat:@"%02x", digest[i]];
    return @{@"status":@"exported", @"relative_path":relative, @"file_name":destination.lastPathComponent,
        @"bytes":@(copied), @"sha256":hex, @"live_mesh_verified":@NO};
}
