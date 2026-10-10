#import <UIKit/UIKit.h>
#import <IOKit/IOKitLib.h>
#import <dlfcn.h>
#import <limits.h>
#import "../patch/CorePreparedCache.h"

static NSString * const CoreID = @"qingxiugai.qingxiugai.qinxiugai";
@interface NSObject (CorePreparationProxy)
+ (id)applicationProxyForIdentifier:(NSString *)identifier;
- (NSURL *)dataContainerURL;
- (NSURL *)bundleURL;
@end

static NSString *SystemString(const char *key) {
    char data[512] = {0}; size_t length = sizeof(data);
    if (sysctlbyname(key, data, &length, NULL, 0) || !length || length > sizeof(data)) return @"";
    return [[NSString alloc] initWithBytes:data length:strnlen(data, length) encoding:NSUTF8StringEncoding] ?: @"";
}

static NSURL *Documents(void) {
    return [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
}

static void Record(NSString *message) {
    NSString *path = [[Documents() URLByAppendingPathComponent:@"Core-cache-preparation.log"] path];
    NSString *line = [NSString stringWithFormat:@"%@ %@\n", NSDate.date, message];
    NSFileHandle *file = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!file) { [line writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL]; return; }
    @try { [file seekToEndOfFile]; [file writeData:[line dataUsingEncoding:NSUTF8StringEncoding]]; [file closeFile]; }
    @catch (__unused NSException *exception) {}
}

static NSString *BootHash(void) {
    io_registry_entry_t chosen = IORegistryEntryFromPath(MACH_PORT_NULL, "IODeviceTree:/chosen");
    if (!chosen) return nil;
    CFTypeRef property = IORegistryEntryCreateCFProperty(chosen, CFSTR("boot-manifest-hash"), kCFAllocatorDefault, 0);
    IOObjectRelease(chosen);
    NSString *hash = nil;
    if (property && CFGetTypeID(property) == CFDataGetTypeID()) {
        CFDataRef data = (CFDataRef)property; CFIndex length = CFDataGetLength(data);
        if (length == 20 || length == 48) {
            NSMutableString *value = [NSMutableString new]; const UInt8 *bytes = CFDataGetBytePtr(data);
            for (CFIndex i = 0; i < length; i++) [value appendFormat:@"%02X", bytes[i]];
            hash = value;
        }
    }
    if (property) CFRelease(property);
    return hash;
}

static BOOL Prepare(NSError **error) {
    NSString *product = SystemString("hw.product"), *build = SystemString("kern.osversion");
    NSString *board = SystemString("hw.target");
    Record([NSString stringWithFormat:@"prepare product=%@ build=%@ board=%@ uid=%u", product, build, board, getuid()]);
    if (!CBCIdentityMatches(product.UTF8String, build.UTF8String, board.UTF8String,
        product.UTF8String, build.UTF8String, board.UTF8String)) {
        *error = CBCPreparedError(@"此准备工具只支持当前 21A327 系统。"); return NO;
    }
    NSDictionary *stamp = CBCBootStamp(); NSString *hash = BootHash();
    if (!stamp || !hash) { *error = CBCPreparedError(@"无法读取当前启动身份，尚未写入缓存。"); return NO; }
    dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_LAZY);
    dlopen("/System/Library/Frameworks/MobileCoreServices.framework/MobileCoreServices", RTLD_LAZY);
    Class cls = NSClassFromString(@"LSApplicationProxy");
    id proxy = [cls respondsToSelector:@selector(applicationProxyForIdentifier:)] ? [cls applicationProxyForIdentifier:CoreID] : nil;
    NSURL *container = [proxy respondsToSelector:@selector(dataContainerURL)] ? [proxy dataContainerURL] : nil;
    NSURL *bundle = [proxy respondsToSelector:@selector(bundleURL)] ? [proxy bundleURL] : nil;
    NSDictionary *info = bundle ? [NSDictionary dictionaryWithContentsOfURL:[bundle URLByAppendingPathComponent:@"Info.plist"]] : nil;
    if (!container || ![info[@"CFBundleIdentifier"] isEqual:CoreID] ||
        [info[@"CoreBootCachePatchVersion"] integerValue] < 7) {
        *error = CBCPreparedError(@"请先用巨魔安装 Core v7，再关闭 Core 后运行准备。"); return NO;
    }
    char containerPath[PATH_MAX];
    if (!realpath(container.fileSystemRepresentation, containerPath) ||
        !strstr(containerPath, "/Containers/Data/Application/")) {
        *error = CBCPreparedError(@"Core 数据容器路径不可用。"); return NO;
    }
    NSURL *destination = [[NSURL fileURLWithPath:@(containerPath) isDirectory:YES]
        URLByAppendingPathComponent:@"Documents/CoreBootCacheInput" isDirectory:YES];
    if (![NSFileManager.defaultManager createDirectoryAtURL:destination withIntermediateDirectories:YES
        attributes:@{NSFilePosixPermissions:@0700} error:error]) return NO;
    NSString *root = [@"/private/preboot" stringByAppendingPathComponent:hash];
    for (NSString *directory in @[@"System/Library/Caches/com.apple.kernelcaches",
        @"cryptex1/current/System/Library/Caches/com.apple.kernelcaches",
        @"Cryptexes/OS/System/Library/Caches/com.apple.kernelcaches"]) {
        for (NSString *name in @[@"kernelcache", @"kernelcache.release",
            [@"kernelcache.release." stringByAppendingString:board]]) {
            char sourcePath[PATH_MAX];
            NSString *candidate = [[root stringByAppendingPathComponent:directory] stringByAppendingPathComponent:name];
            if (!realpath(candidate.fileSystemRepresentation, sourcePath) ||
                !CBCPathMatchesBoot(sourcePath, hash.UTF8String)) continue;
            NSData *data = CBCReadRegular([NSURL fileURLWithPath:@(sourcePath)], 0x20000000, error);
            if (!data || data.length < 4) continue;
            if (![stamp isEqual:CBCBootStamp()] || ![hash isEqual:BootHash()]) {
                *error = CBCPreparedError(@"读取期间启动身份变化，请重新准备。"); return NO;
            }
            NSDictionary *manifest = @{@"protocol":@1, @"product":product, @"build":build, @"board":board,
                @"boot_time":stamp, @"boot_hash":hash, @"source_path":@(sourcePath),
                @"bytes":@(data.length), @"sha256":CBCDigest(data)};
            NSData *encoded = [NSJSONSerialization dataWithJSONObject:manifest options:NSJSONWritingPrettyPrinted error:error];
            if (!encoded || ![data writeToURL:[destination URLByAppendingPathComponent:@"kernelcache.bin"]
                options:NSDataWritingAtomic error:error] || ![encoded writeToURL:[destination URLByAppendingPathComponent:@"manifest.json"]
                options:NSDataWritingAtomic error:error]) return NO;
            // Read back the transfer with exactly the same checks used by Core.
            if (!CBCReadPrepared(destination, product, build, board, stamp, error)) return NO;
            Record([NSString stringWithFormat:@"SUCCESS prepared current-boot cache bytes=%lu", (unsigned long)data.length]);
            return YES;
        }
    }
    if (!*error) *error = CBCPreparedError(@"无法读取当前启动对应的内核文件。");
    return NO;
}

@interface PreparationController : UIViewController
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UIButton *prepareButton;
@end
@implementation PreparationController
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor = UIColor.systemBackgroundColor;
    UILabel *title = [UILabel new]; title.text = @"Core 环境准备"; title.font = [UIFont boldSystemFontOfSize:26];
    self.status = [UILabel new]; self.status.numberOfLines = 0;
    self.status.text = @"请先安装 Core v7，并彻底关闭 Core。\n\n点击准备后，工具会把本机当前系统的内核文件交给 Core。准备成功后再打开 Core。";
    self.prepareButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.prepareButton setTitle:@"准备 Core 环境" forState:UIControlStateNormal];
    [self.prepareButton addTarget:self action:@selector(prepareCache) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[title, self.status, self.prepareButton]];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 26; stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-24],
        [stack.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:48]]];
}
- (void)prepareCache {
    self.prepareButton.enabled = NO; self.status.text = @"正在读取并准备本机当前系统文件…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSError *error = nil; BOOL success = NO;
            @try { success = Prepare(&error); }
            @catch (NSException *exception) { error = CBCPreparedError([NSString stringWithFormat:@"%@: %@", exception.name, exception.reason]); }
            if (!success) Record([NSString stringWithFormat:@"FAILED %@", error]);
            dispatch_async(dispatch_get_main_queue(), ^{
                self.prepareButton.enabled = YES;
                self.status.text = success ? @"准备成功。\n\n现在请打开 Core，等待它完成环境校验。若重启手机后需要重新准备，再运行本工具。" :
                    [NSString stringWithFormat:@"准备失败：%@\n\n日志：文件 → 我的 iPhone → Core 环境准备 → Core-cache-preparation.log", error.localizedDescription ?: @"未知错误"];
            });
        }
    });
}
@end

@interface PreparationDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation PreparationDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [PreparationController new]; [self.window makeKeyAndVisible]; return YES;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(PreparationDelegate.class)); }
}
