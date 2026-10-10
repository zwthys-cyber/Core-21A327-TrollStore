#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <CommonCrypto/CommonDigest.h>
#import <mach-o/loader.h>
#import <sys/sysctl.h>
#import <sys/stat.h>
#import <unistd.h>
#import <dlfcn.h>
#import <spawn.h>
#import <sys/wait.h>
#import <fcntl.h>
#import <errno.h>
#import <signal.h>

static NSString *const CoreID = @"qingxiugai.qingxiugai.qinxiugai";

@interface NSObject (CoreDiagnosticPrivate)
+ (id)applicationProxyForIdentifier:(NSString *)identifier;
+ (id)defaultWorkspace;
+ (id)sharedService;
- (NSURL *)bundleURL;
- (NSURL *)dataContainerURL;
- (NSString *)applicationType;
- (BOOL)openApplicationWithBundleID:(NSString *)identifier;
- (void)openApplication:(NSString *)identifier options:(NSDictionary *)options
            withResult:(void (^)(NSError *error))result;
@end

static NSURL *Documents(void) {
    return [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
}

static NSString *SysValue(const char *name) {
    size_t count = 0;
    if (sysctlbyname(name, NULL, &count, NULL, 0) || !count || count > 8192) return @"unavailable";
    NSMutableData *data = [NSMutableData dataWithLength:count];
    if (sysctlbyname(name, data.mutableBytes, &count, NULL, 0)) return @"unavailable";
    return [[NSString alloc] initWithBytes:data.bytes length:strnlen(data.bytes, count)
                                encoding:NSUTF8StringEncoding] ?: @"unavailable";
}

static NSDictionary *ErrorDetails(NSError *error, NSUInteger depth) {
    if (!error) return @{};
    NSMutableDictionary *result = [@{@"domain":error.domain, @"code":@(error.code),
                                     @"description":error.localizedDescription} mutableCopy];
    NSMutableDictionary *extra = [NSMutableDictionary new];
    for (NSString *key in error.userInfo) {
        id value = error.userInfo[key];
        if ([value isKindOfClass:NSString.class] || [value isKindOfClass:NSNumber.class]) extra[key] = value;
    }
    if (extra.count) result[@"details"] = extra;
    NSError *underlying = error.userInfo[NSUnderlyingErrorKey];
    if (depth < 4 && [underlying isKindOfClass:NSError.class] && underlying != error)
        result[@"underlying"] = ErrorDetails(underlying, depth + 1);
    return result;
}
static NSDictionary *ErrorInfo(NSError *error) { return ErrorDetails(error, 0); }

static uint32_t BE32(const uint8_t *bytes) {
    uint32_t value; memcpy(&value, bytes, sizeof(value)); return __builtin_bswap32(value);
}

// Read only the embedded metadata. This does not establish CoreTrust acceptance.
static NSDictionary *SigningInfo(NSData *data, uint32_t offset, uint32_t length) {
    if (offset > data.length || length > data.length - offset || length < 12) return @{@"error":@"invalid signature range"};
    const uint8_t *blob = (const uint8_t *)data.bytes + offset;
    uint32_t total = BE32(blob + 4), count = BE32(blob + 8);
    if (BE32(blob) != 0xfade0cc0 || total > length || total < 12 || count > (total - 12) / 8)
        return @{@"error":@"invalid signature superblob"};
    NSMutableDictionary *result = [NSMutableDictionary new];
    NSMutableArray *directories = [NSMutableArray new];
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t slot = BE32(blob + 12 + 8 * i), start = BE32(blob + 16 + 8 * i);
        if (start > total || total - start < 8) continue;
        uint32_t size = BE32(blob + start + 4);
        if (size < 8 || size > total - start) continue;
        const uint8_t *item = blob + start;
        if (BE32(item) == 0xfade0c02 && size >= 44) {
            uint32_t identifier = BE32(item + 20);
            NSMutableDictionary *directory = [@{@"slot":@(slot), @"version":@(BE32(item + 8)),
                @"flags":@(BE32(item + 12)), @"code_pages":@(BE32(item + 28)),
                @"code_limit":@(BE32(item + 32)), @"hash_type":@(item[37]), @"page_shift":@(item[39])} mutableCopy];
            if (identifier < size) directory[@"identifier"] = [[NSString alloc]
                initWithBytes:item + identifier length:strnlen((const char *)item + identifier, size - identifier)
                encoding:NSUTF8StringEncoding] ?: @"invalid";
            [directories addObject:directory];
        } else if (slot == 5 && BE32(item) == 0xfade7171) {
            NSData *xml = [NSData dataWithBytes:item + 8 length:size - 8];
            id plist = [NSPropertyListSerialization propertyListWithData:xml options:NSPropertyListImmutable format:NULL error:NULL];
            if ([plist isKindOfClass:NSDictionary.class]) result[@"entitlements"] = plist;
        } else if (slot == 7) result[@"DER_entitlements_present"] = @YES;
    }
    result[@"code_directories"] = directories;
    return result;
}

static NSDictionary *FileInfo(NSURL *url) {
    if (!url) return @{@"error":@"path unavailable"};
    NSError *error = nil;
    NSDictionary *attrs = [NSFileManager.defaultManager attributesOfItemAtPath:url.path error:&error];
    if (!attrs) return @{@"path":url.path, @"error":ErrorInfo(error)};
    return @{@"path":url.path, @"bytes":attrs[NSFileSize] ?: @0,
             @"modified":[attrs[NSFileModificationDate] description] ?: @"",
             @"readable":@([NSFileManager.defaultManager isReadableFileAtPath:url.path])};
}

static NSString *Digest(const void *bytes, size_t count) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes, (CC_LONG)count, digest);
    NSMutableString *hex = [NSMutableString new];
    for (NSUInteger i = 0; i < sizeof(digest); ++i) [hex appendFormat:@"%02x", digest[i]];
    return hex;
}

static NSDictionary *MachInfo(NSURL *url) {
    NSMutableDictionary *result = [FileInfo(url) mutableCopy];
    NSError *error = nil;
    NSData *data = [NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:&error];
    if (!data) { result[@"read_error"] = ErrorInfo(error); return result; }
    const uint8_t *bytes = data.bytes;
    if (data.length < sizeof(struct mach_header_64)) { result[@"parse_error"] = @"short header"; return result; }
    struct mach_header_64 header;
    memcpy(&header, bytes, sizeof(header));
    if (header.magic != MH_MAGIC_64 || header.ncmds > 1024 ||
        header.sizeofcmds > data.length - sizeof(header)) {
        result[@"parse_error"] = @"unsupported or invalid Mach-O"; return result;
    }
    result[@"cpu_type"] = @(header.cputype);
    result[@"cpu_subtype"] = @((uint32_t)header.cpusubtype);
    NSMutableArray *libraries = [NSMutableArray new];
    NSMutableArray *versions = [NSMutableArray new];
    size_t offset = sizeof(header), end = offset + header.sizeofcmds;
    for (uint32_t i = 0; i < header.ncmds; ++i) {
        if (offset > end || end - offset < sizeof(struct load_command)) break;
        struct load_command command;
        memcpy(&command, bytes + offset, sizeof(command));
        if (command.cmdsize < sizeof(command) || command.cmdsize > end - offset) break;
        const uint8_t *body = bytes + offset;
        if (command.cmd == LC_LOAD_DYLIB || command.cmd == LC_LOAD_WEAK_DYLIB || command.cmd == LC_ID_DYLIB) {
            struct dylib_command dylib;
            if (command.cmdsize >= sizeof(dylib)) {
                memcpy(&dylib, body, sizeof(dylib));
                if (dylib.dylib.name.offset < command.cmdsize) {
                    const char *name = (const char *)body + dylib.dylib.name.offset;
                    size_t count = strnlen(name, command.cmdsize - dylib.dylib.name.offset);
                    NSString *text = [[NSString alloc] initWithBytes:name length:count encoding:NSUTF8StringEncoding];
                    if (text) {
                        if (command.cmd != LC_ID_DYLIB) [libraries addObject:text];
                        [versions addObject:@{@"name":text, @"command":@(command.cmd),
                            @"current_version_raw":@(dylib.dylib.current_version),
                            @"compatibility_version_raw":@(dylib.dylib.compatibility_version)}];
                    }
                }
            }
        } else if (command.cmd == LC_CODE_SIGNATURE && command.cmdsize >= sizeof(struct linkedit_data_command)) {
            struct linkedit_data_command signing;
            memcpy(&signing, body, sizeof(signing));
            result[@"embedded_signing"] = SigningInfo(data, signing.dataoff, signing.datasize);
        } else if (command.cmd == LC_UUID && command.cmdsize >= sizeof(struct uuid_command)) {
            struct uuid_command uuid;
            memcpy(&uuid, body, sizeof(uuid));
            result[@"uuid"] = [[NSUUID alloc] initWithUUIDBytes:uuid.uuid].UUIDString;
        } else if (command.cmd == LC_BUILD_VERSION && command.cmdsize >= sizeof(struct build_version_command)) {
            struct build_version_command build;
            memcpy(&build, body, sizeof(build));
            result[@"min_os_raw"] = @(build.minos);
        } else if (command.cmd == LC_SEGMENT_64 && command.cmdsize >= sizeof(struct segment_command_64)) {
            struct segment_command_64 segment;
            memcpy(&segment, body, sizeof(segment));
            size_t room = command.cmdsize - sizeof(segment);
            if (segment.nsects <= room / sizeof(struct section_64)) {
                for (uint32_t j = 0; j < segment.nsects; ++j) {
                    struct section_64 section;
                    memcpy(&section, body + sizeof(segment) + j * sizeof(section), sizeof(section));
                    if (!strncmp(section.segname, "__TEXT", 16) && !strncmp(section.sectname, "__text", 16)
                        && section.offset <= data.length && section.size <= data.length - section.offset)
                        result[@"text_sha256"] = Digest(bytes + section.offset, (size_t)section.size);
                }
            }
        }
        offset += command.cmdsize;
    }
    result[@"loaded_libraries"] = libraries;
    result[@"dylib_versions"] = versions;
    result[@"patch_load_present"] = @([libraries containsObject:@"@executable_path/CoreBootCache.dylib"]);
    return result;
}

static NSString *BootHash(void) {
    io_registry_entry_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
    if (!chosen) return nil;
    CFTypeRef property = IORegistryEntryCreateCFProperty(chosen, CFSTR("boot-manifest-hash"), kCFAllocatorDefault, 0);
    IOObjectRelease(chosen);
    NSString *result = nil;
    if (property && CFGetTypeID(property) == CFDataGetTypeID()) {
        CFDataRef data = (CFDataRef)property;
        if (CFDataGetLength(data) == 48 || CFDataGetLength(data) == 20) {
            NSMutableString *hash = [NSMutableString new];
            const UInt8 *bytes = CFDataGetBytePtr(data);
            for (CFIndex i = 0; i < CFDataGetLength(data); ++i) [hash appendFormat:@"%02X", bytes[i]];
            result = hash;
        }
    }
    if (property) CFRelease(property);
    return result;
}

static NSArray *CrashReports(void) {
    NSMutableArray *reports = [NSMutableArray new];
    NSArray *roots = @[@"/var/mobile/Library/Logs/CrashReporter", @"/Library/Logs/CrashReporter"];
    for (NSString *root in roots) {
        NSError *error = nil;
        NSArray *names = [NSFileManager.defaultManager contentsOfDirectoryAtPath:root error:&error];
        if (error) { [reports addObject:@{@"directory":root, @"error":ErrorInfo(error)}]; continue; }
        NSMutableArray<NSURL *> *candidates = [NSMutableArray new];
        for (NSString *name in names) {
            if ([name hasPrefix:@"Core-"] && [name containsString:@".ips"])
                [candidates addObject:[NSURL fileURLWithPath:[root stringByAppendingPathComponent:name]]];
        }
        [candidates sortUsingComparator:^NSComparisonResult(NSURL *a, NSURL *b) {
            return [b.lastPathComponent compare:a.lastPathComponent];
        }];
        for (NSURL *url in [candidates subarrayWithRange:NSMakeRange(0, MIN(candidates.count, 5))]) {
            NSMutableDictionary *report = [FileInfo(url) mutableCopy];
            if ([report[@"bytes"] unsignedLongLongValue] <= 512 * 1024) {
                NSString *text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:&error];
                if (text) report[@"content"] = text;
                else report[@"read_error"] = ErrorInfo(error);
            }
            [reports addObject:report];
        }
        if (candidates.count == 0) [reports addObject:@{@"directory":root, @"Core_reports_found":@0}];
    }
    return reports;
}

static NSDictionary *Collect(void) {
    NSMutableDictionary *report = [@{@"diagnostic_version":@2, @"date":[NSDate date].description,
        @"device":SysValue("hw.machine"), @"build":SysValue("kern.osversion"),
        @"board":SysValue("hw.target"), @"uid":@(getuid()), @"Core_bundle_id":CoreID} mutableCopy];
    report[@"helper_paths"] = @{@"HOME":@(getenv("HOME") ?: "unset"),
        @"CFFIXED_USER_HOME":@(getenv("CFFIXED_USER_HOME") ?: "unset"), @"Foundation_home":NSHomeDirectory()};
    dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_LAZY);
    dlopen("/System/Library/Frameworks/MobileCoreServices.framework/MobileCoreServices", RTLD_LAZY);
    NSURL *bundle = nil, *container = nil;
    @try {
        Class cls = NSClassFromString(@"LSApplicationProxy");
        id proxy = [cls respondsToSelector:@selector(applicationProxyForIdentifier:)] ?
            [cls applicationProxyForIdentifier:CoreID] : nil;
        report[@"Core_proxy_found"] = @(proxy != nil);
        if ([proxy respondsToSelector:@selector(bundleURL)]) bundle = [proxy bundleURL];
        if ([proxy respondsToSelector:@selector(dataContainerURL)]) container = [proxy dataContainerURL];
        if ([proxy respondsToSelector:@selector(applicationType)])
            report[@"Core_registration_type"] = [proxy applicationType] ?: @"unavailable";
    } @catch (NSException *exception) { report[@"proxy_exception"] = exception.description; }
    if (bundle) {
        report[@"Core_bundle_path"] = bundle.path;
        NSDictionary *info = [NSDictionary dictionaryWithContentsOfURL:[bundle URLByAppendingPathComponent:@"Info.plist"]];
        report[@"Core_installed_version"] = @{ @"version":info[@"CFBundleShortVersionString"] ?: @"missing",
            @"build":info[@"CFBundleVersion"] ?: @"missing", @"patch":info[@"CoreBootCachePatchVersion"] ?: @"missing" };
        NSMutableDictionary *binaries = [NSMutableDictionary new];
        for (NSString *name in @[@"Core", @"CoreBootCache.dylib", @"CoreOffline.work.dylib", @"CCCoreLicense.dylib"])
            binaries[name] = MachInfo([bundle URLByAppendingPathComponent:name]);
        report[@"Core_binaries"] = binaries;
    }
    if (container) {
        report[@"Core_container_path"] = container.path;
        NSMutableArray *logs = [NSMutableArray new];
        for (NSString *name in @[@"Core-21A327-adaptation.log", @"core-offline-original-id.log"]) {
            NSURL *url = [[container URLByAppendingPathComponent:@"Documents"] URLByAppendingPathComponent:name];
            NSMutableDictionary *item = [FileInfo(url) mutableCopy];
            NSError *error = nil;
            if ([item[@"bytes"] unsignedLongLongValue] <= 128 * 1024) {
                NSString *text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:&error];
                if (text) item[@"content"] = text;
                else item[@"read_error"] = ErrorInfo(error);
            }
            [logs addObject:item];
        }
        report[@"Core_logs"] = logs;
    }
    NSString *hash = BootHash();
    report[@"current_boot_hash_available"] = @(hash != nil);
    if (hash) {
        NSURL *kernel = [NSURL fileURLWithPath:[NSString stringWithFormat:
            @"/private/preboot/%@/System/Library/Caches/com.apple.kernelcaches/kernelcache", hash]];
        report[@"current_boot_kernelcache"] = FileInfo(kernel);
    }
    report[@"Core_crash_reports"] = CrashReports();
    NSURL *events = [Documents() URLByAppendingPathComponent:@"Core-launch-events.json"];
    NSData *data = [NSData dataWithContentsOfURL:events];
    report[@"launch_events"] = data ? ([NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] ?: @[]) : @[];
    return report;
}

static void RecordLaunch(NSDictionary *event) {
    @synchronized (NSFileManager.defaultManager) {
        NSURL *url = [Documents() URLByAppendingPathComponent:@"Core-launch-events.json"];
        NSData *previous = [NSData dataWithContentsOfURL:url];
        id decoded = previous ? [NSJSONSerialization JSONObjectWithData:previous options:0 error:NULL] : nil;
        NSMutableArray *events = [decoded isKindOfClass:NSArray.class] ? [decoded mutableCopy] : [NSMutableArray new];
        NSMutableDictionary *item = [event mutableCopy];
        item[@"date"] = [NSDate date].description;
        [events addObject:item];
        while (events.count > 10) [events removeObjectAtIndex:0];
        [[NSJSONSerialization dataWithJSONObject:events options:NSJSONWritingPrettyPrinted error:NULL]
            writeToURL:url atomically:YES];
    }
}

// FrontBoard reports only the aggregate launch failure on this device. A separate,
// short-lived child can expose dyld's stderr before either Core constructor runs.
// Its result is labelled separately because this is not a SpringBoard app launch.
static NSDictionary *CaptureCoreLaunch(void) {
    NSMutableDictionary *result = [@{@"stage":@"standalone stderr capture",
        @"same_as_FrontBoard_launch":@NO} mutableCopy];
    dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_LAZY);
    Class cls = NSClassFromString(@"LSApplicationProxy");
    id proxy = [cls respondsToSelector:@selector(applicationProxyForIdentifier:)] ?
        [cls applicationProxyForIdentifier:CoreID] : nil;
    NSURL *bundle = [proxy respondsToSelector:@selector(bundleURL)] ? [proxy bundleURL] : nil;
    NSURL *container = [proxy respondsToSelector:@selector(dataContainerURL)] ? [proxy dataContainerURL] : nil;
    if (!bundle || !container) { result[@"error"] = @"Core bundle or data container unavailable"; return result; }
    NSURL *binary = [bundle URLByAppendingPathComponent:@"Core"];
    NSURL *output = [Documents() URLByAppendingPathComponent:@"Core-launch-stderr.txt"];
    int fd = open(output.fileSystemRepresentation, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) { result[@"output_open_errno"] = @(errno); return result; }
    if (fd <= STDERR_FILENO) {
        int duplicate = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        int savedError = errno;
        close(fd); fd = duplicate;
        if (fd < 0) { result[@"output_open_errno"] = @(savedError); return result; }
    }
    posix_spawn_file_actions_t actions;
    int actionError = posix_spawn_file_actions_init(&actions);
    if (actionError) { close(fd); result[@"file_actions_error"] = @(actionError); return result; }
    actionError = posix_spawn_file_actions_adddup2(&actions, fd, STDOUT_FILENO);
    if (!actionError) actionError = posix_spawn_file_actions_adddup2(&actions, fd, STDERR_FILENO);
    if (!actionError) actionError = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!actionError) actionError = posix_spawn_file_actions_addclose(&actions, fd);
    if (actionError) {
        posix_spawn_file_actions_destroy(&actions); close(fd);
        result[@"file_actions_error"] = @(actionError); return result;
    }
    NSMutableDictionary *environment = NSProcessInfo.processInfo.environment.mutableCopy;
    environment[@"HOME"] = container.path;
    environment[@"CFFIXED_USER_HOME"] = container.path;
    environment[@"TMPDIR"] = [[container URLByAppendingPathComponent:@"tmp"] path];
    NSArray *keys = environment.allKeys;
    char **env = calloc(keys.count + 1, sizeof(char *));
    if (!env) { posix_spawn_file_actions_destroy(&actions); close(fd); result[@"error"] = @"environment allocation failed"; return result; }
    BOOL allocationOK = YES;
    for (NSUInteger i = 0; i < keys.count; ++i) {
        env[i] = strdup([[NSString stringWithFormat:@"%@=%@", keys[i], environment[keys[i]]] UTF8String]);
        if (!env[i]) allocationOK = NO;
    }
    char *args[] = {(char *)binary.fileSystemRepresentation, NULL};
    pid_t child = -1;
    int spawnError = allocationOK ? posix_spawn(&child, binary.fileSystemRepresentation, &actions, NULL, args, env) : ENOMEM;
    for (NSUInteger i = 0; i < keys.count; ++i) free(env[i]);
    free(env);
    posix_spawn_file_actions_destroy(&actions);
    close(fd);
    result[@"posix_spawn_errno"] = @(spawnError);
    if (spawnError) result[@"posix_spawn_message"] = @(strerror(spawnError));
    else {
        result[@"pid"] = @(child);
        int status = 0;
        pid_t waited = 0;
        for (NSUInteger i = 0; i < 30; ++i) {
            waited = waitpid(child, &status, WNOHANG);
            if (waited < 0 && errno == EINTR) waited = 0;
            if (waited != 0) break;
            usleep(100000);
        }
        if (waited == 0) {
            // Stop only the exact child created above, never an existing Core process.
            result[@"stopped_by_diagnostic_after_seconds"] = @3;
            int killed = kill(child, SIGKILL);
            result[@"timeout_kill_errno"] = @(killed == 0 ? 0 : errno);
            for (NSUInteger i = 0; i < 10; ++i) {
                waited = waitpid(child, &status, WNOHANG);
                if (waited < 0 && errno == EINTR) waited = 0;
                if (waited != 0) break;
                usleep(100000);
            }
        }
        if (waited == child) {
            if (WIFEXITED(status)) result[@"exit_code"] = @(WEXITSTATUS(status));
            if (WIFSIGNALED(status)) result[@"exit_signal"] = @(WTERMSIG(status));
        } else if (waited < 0) result[@"wait_errno"] = @(errno);
        else result[@"child_not_reaped"] = @YES;
    }
    NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:output.path];
    if (handle) {
        unsigned long long size = [handle seekToEndOfFile];
        [handle seekToFileOffset:size > 65536 ? size - 65536 : 0];
        NSData *tail = [handle readDataToEndOfFile];
        [handle closeFile];
        result[@"stdout_stderr_tail"] = [[NSString alloc] initWithData:tail encoding:NSUTF8StringEncoding] ?: @"output is not UTF-8";
        result[@"stdout_stderr_bytes"] = @(size);
    }
    return result;
}

@interface DiagnosticController : UIViewController
@property(nonatomic,strong) UITextView *text;
@property(nonatomic,strong) NSURL *reportURL;
@property(nonatomic,strong) UIButton *collectButton;
@property(nonatomic,strong) UIButton *launchButton;
@end

@implementation DiagnosticController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Core 启动诊断";
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    UILabel *help = [UILabel new];
    help.numberOfLines = 0;
    help.text = @"先点「记录启动」尝试打开 Core；闪退后返回此工具，点「收集诊断」，再分享报告。";
    help.font = [UIFont systemFontOfSize:16];
    UIButton *launch = [UIButton buttonWithType:UIButtonTypeSystem];
    self.launchButton = launch;
    [launch setTitle:@"记录启动" forState:UIControlStateNormal];
    [launch addTarget:self action:@selector(launchCore) forControlEvents:UIControlEventTouchUpInside];
    self.collectButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.collectButton setTitle:@"收集诊断" forState:UIControlStateNormal];
    [self.collectButton addTarget:self action:@selector(collectReport) forControlEvents:UIControlEventTouchUpInside];
    UIButton *share = [UIButton buttonWithType:UIButtonTypeSystem];
    [share setTitle:@"分享报告" forState:UIControlStateNormal];
    [share addTarget:self action:@selector(shareReport:) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *buttons = [[UIStackView alloc] initWithArrangedSubviews:@[launch, self.collectButton, share]];
    buttons.distribution = UIStackViewDistributionFillEqually;
    self.text = [UITextView new];
    self.text.editable = NO;
    self.text.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
    self.text.text = @"报告会保存在：文件 → 我的 iPhone → Core 诊断 → Core-diagnostic-report.json";
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[help, buttons, self.text]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 12;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[[stack.topAnchor constraintEqualToAnchor:safe.topAnchor constant:16],
        [stack.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16],
        [stack.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:16],
        [stack.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-16],
        [buttons.heightAnchor constraintEqualToConstant:44]]];
    self.reportURL = [Documents() URLByAppendingPathComponent:@"Core-diagnostic-report.json"];
}
- (void)launchCore {
    self.launchButton.enabled = NO;
    RecordLaunch(@{@"stage":@"requested"});
    @try {
        dlopen("/System/Library/PrivateFrameworks/FrontBoardServices.framework/FrontBoardServices", RTLD_LAZY);
        Class cls = NSClassFromString(@"FBSSystemService");
        id service = [cls respondsToSelector:@selector(sharedService)] ? [cls sharedService] : nil;
        if ([service respondsToSelector:@selector(openApplication:options:withResult:)]) {
            [service openApplication:CoreID options:@{} withResult:^(NSError *error) {
                RecordLaunch(@{@"stage":@"FrontBoard result", @"request_accepted":@(error == nil),
                               @"error":ErrorInfo(error)});
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (error) [self captureFailure];
                    else {
                        self.launchButton.enabled = YES;
                        self.text.text = @"系统已接受启动请求。这不代表 Core 启动成功。返回后点「收集诊断」。";
                    }
                });
            }];
            return;
        }
        Class workspaceClass = NSClassFromString(@"LSApplicationWorkspace");
        id workspace = [workspaceClass respondsToSelector:@selector(defaultWorkspace)] ? [workspaceClass defaultWorkspace] : nil;
        BOOL accepted = [workspace respondsToSelector:@selector(openApplicationWithBundleID:)] &&
                        [workspace openApplicationWithBundleID:CoreID];
        RecordLaunch(@{@"stage":@"LaunchServices fallback", @"request_accepted":@(accepted)});
        if (!accepted) [self captureFailure];
        else { self.launchButton.enabled = YES; self.text.text = @"启动请求已提交，返回后点「收集诊断」。"; }
    } @catch (NSException *exception) {
        RecordLaunch(@{@"stage":@"launch exception", @"exception":exception.description});
        [self captureFailure];
    }
}
- (void)captureFailure {
    self.collectButton.enabled = NO;
    self.text.text = @"系统启动失败，正在尝试捕获底层错误，请等待约 4 秒…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            @try { RecordLaunch(CaptureCoreLaunch()); }
            @catch (NSException *exception) { RecordLaunch(@{@"stage":@"capture exception", @"exception":exception.description}); }
            dispatch_async(dispatch_get_main_queue(), ^{
                self.launchButton.enabled = YES;
                self.collectButton.enabled = YES;
                self.text.text = @"启动错误捕获已完成。点「收集诊断」，再「分享报告」。";
            });
        }
    });
}
- (void)collectReport {
    self.collectButton.enabled = NO;
    self.text.text = @"正在读取 Core 的安装信息与日志…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSDictionary *report;
            @try { report = Collect(); }
            @catch (NSException *exception) { report = @{@"collector_exception":exception.description}; }
            NSError *error = nil;
            NSData *data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:&error];
            if (data) [data writeToURL:self.reportURL options:NSDataWritingAtomic error:&error];
            dispatch_async(dispatch_get_main_queue(), ^{
                self.collectButton.enabled = YES;
                self.text.text = error ? [NSString stringWithFormat:@"保存失败：%@", error] :
                    [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            });
        }
    });
}
- (void)shareReport:(UIButton *)sender {
    if (![NSFileManager.defaultManager fileExistsAtPath:self.reportURL.path]) {
        self.text.text = @"请先点「收集诊断」，再分享报告。";
        return;
    }
    UIActivityViewController *share = [[UIActivityViewController alloc] initWithActivityItems:@[self.reportURL] applicationActivities:nil];
    share.popoverPresentationController.sourceView = sender;
    share.popoverPresentationController.sourceRect = sender.bounds;
    [self presentViewController:share animated:YES completion:nil];
}
@end

@interface DiagnosticDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation DiagnosticDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:[DiagnosticController new]];
    [self.window makeKeyAndVisible];
    return YES;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(DiagnosticDelegate.class)); }
}
