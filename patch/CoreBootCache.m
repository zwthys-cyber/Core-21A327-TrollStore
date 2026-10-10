#import <Foundation/Foundation.h>
#ifndef CBC_HOST_TEST
#import <UIKit/UIKit.h>
#endif
#import <IOKit/IOKitLib.h>
#import <objc/runtime.h>
#import <sys/sysctl.h>
#import <sys/stat.h>
#import <limits.h>
#import <unistd.h>
#import <errno.h>
#import <string.h>
#import <stdlib.h>
#import <fcntl.h>
#import <stdio.h>
#import <stdatomic.h>
#import "CoreBootPolicy.h"

// Core 1.6, UUID 4ED1FD29-A46A-36B3-8DFC-36F67EA929D1.
// This declaration describes the original, unmodified cache publisher.
@interface NSObject (CoreCachePublisher)
- (BOOL)qm539:(NSData *)data source:(NSURL *)source
  productType:(NSString *)product buildVersion:(NSString *)build
  boardConfig:(NSString *)board error:(NSError **)error;
@end

typedef BOOL (*CacheValidator)(id, SEL, NSURL *, NSURL *, NSString *, NSString *, NSString *);
static _Atomic(CacheValidator) OriginalValidator;
static _Thread_local BOOL Publishing;
static BOOL Attempted;

// Avoid Foundation, ObjC allocation and thread-local initialization at the first checkpoint.
static void StartupRecord(const char *phase) {
    const char *container = getenv("CFFIXED_USER_HOME");
    if (!container || (!strstr(container, "/Containers/Data/Application/") &&
                       !strstr(container, "/Containers/Data/PluginKitPlugin/"))) container = getenv("HOME");
    if (!container || (!strstr(container, "/Containers/Data/Application/") &&
                       !strstr(container, "/Containers/Data/PluginKitPlugin/"))) return;
    char directory[PATH_MAX], path[PATH_MAX], line[512];
    int count = snprintf(directory, sizeof(directory), "%s/Documents", container);
    if (count <= 0 || (size_t)count >= sizeof(directory)) return;
    mkdir(directory, 0700);
    count = snprintf(path, sizeof(path), "%s/Core-21A327-adaptation.log", directory);
    if (count <= 0 || (size_t)count >= sizeof(path)) return;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return;
    count = snprintf(line, sizeof(line), "patch=4 pid=%u startup: %s\n", getpid(), phase);
    if (count > 0 && (size_t)count < sizeof(line)) {
        ssize_t result;
        do { result = write(fd, line, (size_t)count); } while (result < 0 && errno == EINTR);
    }
    close(fd);
}

static BOOL ValidateOriginal(id self, SEL selector, NSURL *cache, NSURL *metadata,
                             NSString *product, NSString *build, NSString *board) {
    CacheValidator original = atomic_load_explicit(&OriginalValidator, memory_order_acquire);
    if (!original) {
        StartupRecord("ERROR original validator unavailable");
        return NO;
    }
    return original(self, selector, cache, metadata, product, build, board);
}

static NSString *SysString(const char *key) {
    size_t size = 0;
    if (sysctlbyname(key, NULL, &size, NULL, 0) || !size || size > 4096) return @"";
    NSMutableData *data = [NSMutableData dataWithLength:size];
    if (sysctlbyname(key, data.mutableBytes, &size, NULL, 0)) return @"";
    const char *bytes = data.bytes;
    return [[NSString alloc] initWithBytes:bytes length:strnlen(bytes, size)
                                encoding:NSUTF8StringEncoding] ?: @"";
}

static void Log(NSString *message) {
    NSString *line = [NSString stringWithFormat:@"%@ %@\n", [NSDate date], message];
    NSLog(@"[CoreBootCache] %@", message);
    @synchronized ([NSFileManager defaultManager]) {
        NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                                 inDomains:NSUserDomainMask].firstObject;
        NSURL *url = [documents URLByAppendingPathComponent:@"Core-21A327-adaptation.log"];
        if (!url) return;
        NSFileHandle *handle = [NSFileHandle fileHandleForWritingAtPath:url.path];
        if (!handle) {
            [line writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:NULL];
            return;
        }
        @try {
            [handle seekToEndOfFile];
            [handle writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
            [handle closeFile];
        } @catch (__unused NSException *exception) {}
    }
}

static NSString *BootHash(void) {
    io_registry_entry_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
    if (!chosen) return nil;
    CFTypeRef property = IORegistryEntryCreateCFProperty(chosen, CFSTR("boot-manifest-hash"),
                                                        kCFAllocatorDefault, 0);
    IOObjectRelease(chosen);
    if (!property) return nil;
    NSString *result = nil;
    if (CFGetTypeID(property) == CFDataGetTypeID()) {
        CFDataRef data = (CFDataRef)property;
        CFIndex count = CFDataGetLength(data);
        if (count == 48 || count == 20) {
            const UInt8 *bytes = CFDataGetBytePtr(data);
            NSMutableString *hex = [NSMutableString stringWithCapacity:(NSUInteger)count * 2];
            for (CFIndex i = 0; i < count; i++) [hex appendFormat:@"%02X", bytes[i]];
            result = hex;
        }
    }
    CFRelease(property);
    return result;
}

static NSArray<NSURL *> *Candidates(NSString *hash, NSString *board) {
    // Never search other preboot hashes: an older installed build is unsafe here.
    NSString *boot = [@"/private/preboot" stringByAppendingPathComponent:hash];
    NSArray *relative = @[@"System/Library/Caches/com.apple.kernelcaches",
                          @"cryptex1/current/System/Library/Caches/com.apple.kernelcaches",
                          @"Cryptexes/OS/System/Library/Caches/com.apple.kernelcaches"];
    NSMutableArray *result = [NSMutableArray array];
    for (NSString *directory in relative) {
        NSURL *url = [NSURL fileURLWithPath:[boot stringByAppendingPathComponent:directory]];
        // Try the normal name first, even if directory enumeration is denied.
        [result addObject:[url URLByAppendingPathComponent:@"kernelcache"]];
        NSError *error = nil;
        NSArray<NSURL *> *files = [[NSFileManager defaultManager] contentsOfDirectoryAtURL:url
            includingPropertiesForKeys:nil options:0 error:&error];
        if (error) Log([NSString stringWithFormat:@"enumerate %@: %@", url.path, error]);
        for (NSURL *file in [files sortedArrayUsingComparator:^NSComparisonResult(NSURL *a, NSURL *b) {
            return [a.lastPathComponent compare:b.lastPathComponent];
        }]) {
            NSString *name = file.lastPathComponent;
            if ([name isEqualToString:@"kernelcache.release"] ||
                [name caseInsensitiveCompare:[@"kernelcache.release." stringByAppendingString:board]]
                    == NSOrderedSame) [result addObject:file];
        }
    }
    return result;
}

static void ShowFailure(NSString *reason) {
#ifndef CBC_HOST_TEST
    dispatch_async(dispatch_get_main_queue(), ^{
        UIWindow *window = nil;
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if ([scene isKindOfClass:UIWindowScene.class]) {
                for (UIWindow *candidate in ((UIWindowScene *)scene).windows)
                    if (candidate.isKeyWindow) window = candidate;
            }
        }
        UIViewController *controller = window.rootViewController;
        while (controller.presentedViewController) controller = controller.presentedViewController;
        if (!controller || [controller isKindOfClass:UIAlertController.class]) return;
        NSString *message = [reason stringByAppendingString:
            @"\n\n诊断日志：文件 App → 我的 iPhone → Core → Core-21A327-adaptation.log"];
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"21A327 环境适配"
            message:message preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:nil]];
        [controller presentViewController:alert animated:YES completion:nil];
    });
#else
    Log(reason);
#endif
}

static BOOL ValidateWithBootCache(id self, SEL selector, NSURL *cache, NSURL *metadata,
                                 NSString *product, NSString *build, NSString *board) {
    StartupRecord("validator entered");
    if (ValidateOriginal(self, selector, cache, metadata, product, build, board)) return YES;
    if (Publishing || !CBCIdentityMatches(product.UTF8String, build.UTF8String, board.UTF8String,
        SysString("hw.product").UTF8String, SysString("kern.osversion").UTF8String,
        SysString("hw.target").UTF8String)) return NO;
    @synchronized (self) {
        if (ValidateOriginal(self, selector, cache, metadata, product, build, board)) return YES;
        if (Attempted) return NO;
        Attempted = YES;
        Log([NSString stringWithFormat:@"patch=4 product=%@ build=%@ board=%@ uid=%u",
             product, build, board, getuid()]);
        NSString *hash = BootHash();
        if (!hash) {
            Log(@"FAILED: current boot-manifest-hash unavailable");
            ShowFailure(@"无法识别当前启动系统的内核路径。请将诊断日志发回分析。");
            return NO;
        }
        for (NSURL *url in Candidates(hash, board)) {
            @autoreleasepool {
                char resolved[PATH_MAX];
                if (!realpath(url.fileSystemRepresentation, resolved)) {
                    Log([NSString stringWithFormat:@"resolve %@: %s", url.path, strerror(errno)]);
                    continue;
                }
                NSString *path = [NSString stringWithUTF8String:resolved];
                if (!CBCPathMatchesBoot(resolved, hash.UTF8String)) {
                    Log([@"reject path outside current boot: " stringByAppendingString:path]);
                    continue;
                }
                struct stat st;
                if (stat(resolved, &st) || !S_ISREG(st.st_mode) || st.st_size < 4 ||
                    st.st_size > 0x20000000) {
                    Log([@"reject file type/size: " stringByAppendingString:path]);
                    continue;
                }
                NSError *error = nil;
                NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:&error];
                if (!data) {
                    Log([NSString stringWithFormat:@"read %@: %@", path, error]);
                    continue;
                }
                BOOL published = NO;
                Publishing = YES;
                @try {
                    published = [self qm539:data source:url
                        productType:product buildVersion:build boardConfig:board error:&error];
                } @catch (NSException *exception) {
                    error = [NSError errorWithDomain:@"CoreBootCache" code:2
                        userInfo:@{NSLocalizedDescriptionKey:
                            [NSString stringWithFormat:@"%@: %@", exception.name, exception.reason]}];
                } @finally { Publishing = NO; }
                BOOL valid = published && ValidateOriginal(self, selector, cache, metadata, product, build, board);
                Log([NSString stringWithFormat:@"publish %@ bytes=%lu result=%d error=%@",
                     path, (unsigned long)data.length, valid, error]);
                if (valid) {
                    Log(@"SUCCESS: original cache validator passed; continuing original preparation");
                    return YES;
                }
            }
        }
        Log(@"FAILED: no readable, valid current-boot kernelcache");
        ShowFailure(@"当前系统内核文件读取或校验失败。请使用巨魔安装此 IPA，并将诊断日志发回分析。");
        return NO;
    }
}

__attribute__((constructor)) static void InstallBootCachePatch(void) {
    StartupRecord("constructor entered");
    Class cls = objc_getClass("QXA107");
    SEL selector = sel_registerName("qm509:metadataURL:matchesProduct:build:board:");
    Method method = class_getInstanceMethod(cls, selector);
    Method publisher = class_getInstanceMethod(cls,
        sel_registerName("qm539:source:productType:buildVersion:boardConfig:error:"));
    StartupRecord("methods resolved");
    if (!method || !publisher || method_getNumberOfArguments(method) != 7 ||
        method_getNumberOfArguments(publisher) != 8) {
        StartupRecord("unsupported Core ABI; hook skipped");
        return;
    }
    // The original CoreOffline constructor may already have launched the prefetch worker.
    // Publish its callable original BEFORE another thread can observe the replacement.
    CacheValidator original = (CacheValidator)method_getImplementation(method);
    if (!original) { StartupRecord("missing original; hook skipped"); return; }
    atomic_store_explicit(&OriginalValidator, original, memory_order_release);
    StartupRecord("original published");
    method_setImplementation(method, (IMP)ValidateWithBootCache);
    StartupRecord("hook installed");
}
