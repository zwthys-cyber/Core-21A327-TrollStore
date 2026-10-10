#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <sys/sysctl.h>
#import <sys/stat.h>
#import <fcntl.h>
#import <unistd.h>
#import <errno.h>
#import <limits.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import "CoreBootPolicy.h"

// Separate executable: platform permission is limited to current-boot acquisition.
// The parent opens a private temporary cache file and passes its descriptor as 3.
static NSString *SystemString(const char *name) {
    char value[512] = {0}; size_t size = sizeof(value);
    if (sysctlbyname(name, value, &size, NULL, 0) || !size || size > sizeof(value)) return @"";
    return [[NSString alloc] initWithBytes:value length:strnlen(value, size)
                                 encoding:NSUTF8StringEncoding] ?: @"";
}

static BOOL WriteAll(int descriptor, const void *bytes, size_t count) {
    const char *position = bytes;
    while (count) {
        ssize_t written = write(descriptor, position, count);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return NO;
        position += written; count -= (size_t)written;
    }
    return YES;
}

static int Report(NSDictionary *result, int code) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:result options:0 error:NULL];
    if (!data || !WriteAll(STDOUT_FILENO, data.bytes, data.length)) return 20;
    return code;
}

static int Failure(NSString *stage, int error) {
    return Report(@{@"protocol":@1, @"success":@NO, @"stage":stage, @"errno":@(error)}, 1);
}

static NSString *CurrentBootHash(void) {
    io_registry_entry_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
    if (!chosen) return nil;
    CFTypeRef property = IORegistryEntryCreateCFProperty(chosen, CFSTR("boot-manifest-hash"), kCFAllocatorDefault, 0);
    IOObjectRelease(chosen);
    NSString *result = nil;
    if (property && CFGetTypeID(property) == CFDataGetTypeID()) {
        CFDataRef data = (CFDataRef)property;
        CFIndex length = CFDataGetLength(data);
        if (length == 20 || length == 48) {
            NSMutableString *hash = [NSMutableString new];
            const UInt8 *bytes = CFDataGetBytePtr(data);
            for (CFIndex i = 0; i < length; ++i) [hash appendFormat:@"%02X", bytes[i]];
            result = hash;
        }
    }
    if (property) CFRelease(property);
    return result;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 4) return Failure(@"arguments", EINVAL);
        NSString *product = SystemString("hw.product"), *build = SystemString("kern.osversion");
        NSString *board = SystemString("hw.target");
        if (!CBCIdentityMatches(argv[1], argv[2], argv[3], product.UTF8String, build.UTF8String, board.UTF8String))
            return Failure(@"identity", EINVAL);
        struct stat destination;
        if (fstat(3, &destination) || !S_ISREG(destination.st_mode) || destination.st_size != 0)
            return Failure(@"destination descriptor", EINVAL);
        NSString *hash = CurrentBootHash();
        if (!hash) return Failure(@"boot-manifest-hash", ENOENT);
        NSString *boot = [@"/private/preboot" stringByAppendingPathComponent:hash];
        NSArray *directories = @[@"System/Library/Caches/com.apple.kernelcaches",
            @"cryptex1/current/System/Library/Caches/com.apple.kernelcaches",
            @"Cryptexes/OS/System/Library/Caches/com.apple.kernelcaches"];
        int lastError = ENOENT;
        for (NSString *directory in directories) {
            for (NSString *name in @[@"kernelcache", @"kernelcache.release",
                                    [@"kernelcache.release." stringByAppendingString:board]]) {
                NSString *candidate = [[boot stringByAppendingPathComponent:directory] stringByAppendingPathComponent:name];
                char resolved[PATH_MAX];
                if (!realpath(candidate.fileSystemRepresentation, resolved)) { lastError = errno; continue; }
                if (!CBCPathMatchesBoot(resolved, hash.UTF8String)) { lastError = EPERM; continue; }
                int source = open(resolved, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
                if (source < 0) { lastError = errno; continue; }
                struct stat before, after;
                if (fstat(source, &before) || !S_ISREG(before.st_mode) || before.st_size < 4 || before.st_size > 0x20000000) {
                    close(source); lastError = EINVAL; continue;
                }
                char bytes[64 * 1024]; off_t total = 0; BOOL copied = YES;
                for (;;) {
                    ssize_t count = read(source, bytes, sizeof(bytes));
                    if (count < 0 && errno == EINTR) continue;
                    if (count < 0) { lastError = errno; copied = NO; break; }
                    if (!count) break;
                    if (count > before.st_size - total) { lastError = EFBIG; copied = NO; break; }
                    if (!WriteAll(3, bytes, (size_t)count)) { lastError = errno; copied = NO; break; }
                    total += count;
                }
                if (fstat(source, &after) || after.st_size != before.st_size || total != before.st_size) {
                    lastError = EIO; copied = NO;
                }
                close(source);
                if (!copied) return Failure(@"kernelcache copy", lastError);
                if (fsync(3)) return Failure(@"kernelcache flush", errno);
                return Report(@{@"protocol":@1, @"success":@YES, @"boot_hash":hash,
                    @"source_path":@(resolved), @"bytes":@(total), @"product":product,
                    @"build":build, @"board":board}, 0);
            }
        }
        return Failure(@"current-boot kernelcache", lastError);
    }
}
