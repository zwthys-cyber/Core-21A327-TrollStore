#import <Foundation/Foundation.h>
#ifndef CBC_HOST_TEST
#import <UIKit/UIKit.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
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
#import <dlfcn.h>
#import "CoreBootPolicy.h"
#import "CoreBootProbeClient.h"
#import "CorePreparedCache.h"
#ifndef CBC_HOST_TEST
extern void CoreRemoteTraceStartup(void);
#ifdef CORE_MODEL_CAPTURE
extern void CoreModelCaptureStartup(void);
#endif
#endif

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
    count = snprintf(line, sizeof(line), "patch=19 pid=%u startup: %s\n", getpid(), phase);
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

#ifndef CBC_HOST_TEST
// Read only this process's pinned Core image. Aggregate cache counts are sampled
// diagnostics, not frame counters or proof that any individual actor is drawn.
static uintptr_t MarkerImageBase(void) {
    const struct mach_header_64 *header = (const void *)_dyld_get_image_header(0);
    const unsigned char expected[16] = {0x4e,0xd1,0xfd,0x29,0xa4,0x6a,0x36,0xb3,
        0x8d,0xfc,0x36,0xf6,0x7e,0xa9,0x29,0xd1};
    if (!header || header->magic != MH_MAGIC_64 || header->ncmds > 512 ||
        header->sizeofcmds > 65536) return 0;
    const unsigned char *cursor = (const void *)(header + 1);
    size_t remaining = header->sizeofcmds;
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (remaining < sizeof(struct load_command)) return 0;
        const struct load_command *command = (const void *)cursor;
        if (command->cmdsize < sizeof(*command) || command->cmdsize > remaining) return 0;
        if (command->cmd == LC_UUID && command->cmdsize == sizeof(struct uuid_command)) {
            const struct uuid_command *uuid = (const void *)command;
            return memcmp(uuid->uuid, expected, sizeof(expected)) == 0 ? (uintptr_t)header : 0;
        }
        cursor += command->cmdsize;
        remaining -= command->cmdsize;
    }
    return 0;
}

static BOOL MarkerRead(uintptr_t address, void *buffer, size_t length) {
    vm_size_t copied = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)address, (vm_size_t)length,
        (vm_address_t)buffer, &copied) == KERN_SUCCESS && copied == length;
}

static void StartMarkerDiagnostics(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        uintptr_t base = MarkerImageBase();
        if (!base) { Log(@"patch=19 markers diagnostics skipped: Core UUID mismatch"); return; }
        static dispatch_source_t timer;
        if (timer) return;
        timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        if (!timer) return;
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC),
                                  5 * NSEC_PER_SEC, NSEC_PER_SEC);
        dispatch_source_set_event_handler(timer, ^{
            unsigned char config[0x1ea], valid[128], obstructed[128], armed, running;
            uintptr_t scenes[3], target;
            uint32_t mode;
            uint64_t sourceKey, sourceValue;
            if (!MarkerRead(base + 0xC54270, config, sizeof(config)) ||
                !MarkerRead(base + 0xC15EC8, scenes, sizeof(scenes)) ||
                !MarkerRead(base + 0xC4E28C, valid, sizeof(valid)) ||
                !MarkerRead(base + 0xC4E18C, obstructed, sizeof(obstructed)) ||
                !MarkerRead(base + 0xC4E400, &armed, sizeof(armed)) ||
                !MarkerRead(base + 0xC4E3C8, &running, sizeof(running)) ||
                !MarkerRead(base + 0xBD4860, &sourceKey, sizeof(sourceKey)) ||
                !MarkerRead(base + 0xBD4AB8, &sourceValue, sizeof(sourceValue))) return;
            memcpy(&target, config + 0x18, sizeof(target));
            memcpy(&mode, config + 0x130, sizeof(mode));
            unsigned validCount = 0, obstructedCount = 0;
            for (size_t i = 0; i < sizeof(valid); i++) {
                if (valid[i] & 1) {
                    validCount++;
                    if (obstructed[i]) obstructedCount++;
                }
            }
            NSString *state = [NSString stringWithFormat:
                @"patch=19 markers sampled enabled=%u visibility_request=%u target_bound=%d visibility_mode=%u visibility_armed=%u geometry_running=%u source_offset_available=%d geometry_ready=%u scenes=%d/%d/%d cached_valid_joints=%u cached_obstructed_joints=%u",
                config[0x23], config[0x2f], target != 0, mode, armed, running,
                sourceKey != sourceValue, config[0x1e9], scenes[0] != 0,
                scenes[1] != 0, scenes[2] != 0, validCount, obstructedCount];
            static NSString *previous;
            static unsigned ticks;
            if (![state isEqualToString:previous] || ++ticks % 12 == 0) Log(state);
            previous = state;
        });
        dispatch_resume(timer);
    });
}
#endif

// Report only the permissions relevant to acquisition, never arbitrary entitlements.
static void LogAcquisitionPermissions(void) {
    typedef CFTypeRef (*CreateTask)(CFAllocatorRef);
    typedef CFTypeRef (*CopyEntitlement)(CFTypeRef, CFStringRef, CFErrorRef *);
    CreateTask create = (CreateTask)dlsym(RTLD_DEFAULT, "SecTaskCreateFromSelf");
    CopyEntitlement copy = (CopyEntitlement)dlsym(RTLD_DEFAULT, "SecTaskCopyValueForEntitlement");
    if (!create || !copy) { Log(@"runtime acquisition permissions: query API unavailable"); return; }
    CFTypeRef task = create(kCFAllocatorDefault);
    if (!task) { Log(@"runtime acquisition permissions: task unavailable"); return; }
    NSArray<NSString *> *keys = @[@"platform-application", @"com.apple.private.security.no-sandbox",
        @"com.apple.private.security.storage.AppDataContainers"];
    NSMutableArray<NSString *> *states = [NSMutableArray array];
    for (NSString *key in keys) {
        CFErrorRef error = NULL;
        CFTypeRef value = copy(task, (__bridge CFStringRef)key, &error);
        BOOL enabled = value && CFGetTypeID(value) == CFBooleanGetTypeID() &&
                       CFBooleanGetValue((CFBooleanRef)value);
        [states addObject:[NSString stringWithFormat:@"%@=%d error=%ld", key, enabled,
            error ? (long)CFErrorGetCode(error) : 0L]];
        if (value) CFRelease(value);
        if (error) CFRelease(error);
    }
    CFRelease(task);
    Log([@"runtime acquisition permissions: " stringByAppendingString:[states componentsJoinedByString:@" "]]);
}

static NSString *BootHash(void) {
    io_registry_entry_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
    if (!chosen) { Log(@"Direct IORegistry chosen entry unavailable"); return nil; }
    CFTypeRef property = IORegistryEntryCreateCFProperty(chosen, CFSTR("boot-manifest-hash"),
                                                        kCFAllocatorDefault, 0);
    IOObjectRelease(chosen);
    if (!property) { Log(@"Direct IORegistry boot-manifest-hash property unavailable"); return nil; }
    NSString *result = nil;
    if (CFGetTypeID(property) == CFDataGetTypeID()) {
        CFDataRef data = (CFDataRef)property;
        CFIndex count = CFDataGetLength(data);
        if (count == 48 || count == 20) {
            const UInt8 *bytes = CFDataGetBytePtr(data);
            NSMutableString *hex = [NSMutableString stringWithCapacity:(NSUInteger)count * 2];
            for (CFIndex i = 0; i < count; i++) [hex appendFormat:@"%02X", bytes[i]];
            result = hex;
            Log([NSString stringWithFormat:@"Direct current-boot hash available (%ld bytes)", (long)count]);
        } else Log([NSString stringWithFormat:@"Direct boot hash has unexpected length: %ld", (long)count]);
    } else Log(@"Direct boot hash has unexpected property type");
    CFRelease(property);
    return result;
}

static void LogSandboxStatus(void) {
    typedef int (*SandboxCheck)(pid_t, const char *, int, ...);
    SandboxCheck check = (SandboxCheck)dlsym(RTLD_DEFAULT, "sandbox_check");
    if (!check) {
        void *library = dlopen("/usr/lib/system/libsystem_sandbox.dylib", RTLD_LAZY | RTLD_LOCAL);
        if (library) check = (SandboxCheck)dlsym(library, "sandbox_check");
    }
    if (!check) { Log(@"runtime sandbox query unavailable"); return; }
    errno = 0; int result = check(getpid(), NULL, 0); int saved = errno;
    Log([NSString stringWithFormat:@"runtime sandbox_check=%d errno=%d (0=unsandboxed, 1=sandboxed)", result, saved]);
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

static BOOL PublishBootData(id self, SEL selector, NSData *data, NSURL *source, NSURL *cache,
                           NSURL *metadata, NSString *product, NSString *build, NSString *board) {
    NSError *error = nil; BOOL published = NO;
    Publishing = YES;
    @try {
        published = [self qm539:data source:source productType:product buildVersion:build boardConfig:board error:&error];
    } @catch (NSException *exception) {
        error = [NSError errorWithDomain:@"CoreBootCache" code:2 userInfo:@{NSLocalizedDescriptionKey:
            [NSString stringWithFormat:@"%@: %@", exception.name, exception.reason]}];
    } @finally { Publishing = NO; }
    BOOL valid = published && ValidateOriginal(self, selector, cache, metadata, product, build, board);
    Log([NSString stringWithFormat:@"publish %@ bytes=%lu result=%d error=%@", source.path,
        (unsigned long)data.length, valid, error]);
    if (valid) Log(@"SUCCESS: original cache validator passed; continuing original preparation");
    return valid;
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
        Log([NSString stringWithFormat:@"patch=19 profile=enemy-markers-600m-identity-colors product=%@ build=%@ board=%@ uid=%u",
             product, build, board, getuid()]);
        LogAcquisitionPermissions();
        LogSandboxStatus();
        NSURL *preparedDirectory = [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
            inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"CoreBootCacheInput" isDirectory:YES];
        NSURL *manifest = [preparedDirectory URLByAppendingPathComponent:@"manifest.json"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:manifest.path]) {
            NSError *preparedError = nil;
            NSDictionary *prepared = CBCReadPrepared(preparedDirectory, product, build, board, CBCBootStamp(), &preparedError);
            if (prepared) {
                Log([NSString stringWithFormat:@"prepared current-boot acquisition passed: bytes=%lu",
                    (unsigned long)[prepared[@"data"] length]]);
                if (PublishBootData(self, selector, prepared[@"data"], [NSURL fileURLWithPath:prepared[@"source_path"]],
                    cache, metadata, product, build, board)) {
                    [[NSFileManager defaultManager] removeItemAtURL:manifest error:NULL];
                    [[NSFileManager defaultManager] removeItemAtURL:[preparedDirectory URLByAppendingPathComponent:@"kernelcache.bin"] error:NULL];
                    return YES;
                }
            } else Log([NSString stringWithFormat:@"prepared current-boot input rejected: %@", preparedError]);
        } else Log(@"No prepared input; use Core 环境准备 if direct acquisition fails");
        NSString *hash = BootHash();
        if (!hash) {
            Log(@"Direct boot hash unavailable; falling back to bundled platform probe");
        }
        for (NSURL *url in (hash ? Candidates(hash, board) : @[])) {
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
                if (PublishBootData(self, selector, data, url, cache, metadata, product, build, board)) return YES;
            }
        }
        NSError *error = nil;
        NSURL *directory = [[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory
            inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"CoreBootCache"];
        NSString *helper = [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"CoreBootProbe"];
        NSDictionary *probe = RunBootProbe(helper, product, build, board, directory, &error);
        if (probe) {
            Log([NSString stringWithFormat:@"probe current-boot acquisition passed: bytes=%@", probe[@"bytes"]]);
            NSString *temporary = probe[@"temporary_path"];
            NSData *data = [NSData dataWithContentsOfFile:temporary options:NSDataReadingMappedIfSafe error:&error];
            [[NSFileManager defaultManager] removeItemAtPath:temporary error:NULL];
            if (data && PublishBootData(self, selector, data, [NSURL fileURLWithPath:probe[@"source_path"]],
                cache, metadata, product, build, board)) return YES;
        }
        if (error) Log([NSString stringWithFormat:@"FAILED: bundled probe: %@", error]);
        Log(@"FAILED: no readable, valid current-boot kernelcache");
        ShowFailure(@"当前系统内核文件尚未准备好。请关闭 Core，打开“Core 环境准备”并点击准备；成功后重新打开 Core。");
        return NO;
    }
}

__attribute__((constructor)) static void InstallBootCachePatch(void) {
    StartupRecord("constructor entered");
#ifndef CBC_HOST_TEST
    CoreRemoteTraceStartup();
#ifdef CORE_MODEL_CAPTURE
    CoreModelCaptureStartup();
#endif
#endif
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
#ifndef CBC_HOST_TEST
    StartMarkerDiagnostics();
#endif
}
