#import <UIKit/UIKit.h>
#import <dlfcn.h>
#import <sys/sysctl.h>
#import "GameSourceInventory.h"

// Verified from Apple's public lookup for App Store ID 1321803705.
static NSString * const GameID = @"com.tencent.tmgp.pubgmhd";
static NSString * const ReportName = @"Core-model-source-report.json";

@interface NSObject (CoreModelSourceProxy)
+ (id)applicationProxyForIdentifier:(NSString *)identifier;
- (NSURL *)bundleURL;
- (NSURL *)dataContainerURL;
@end

static NSString *SystemString(const char *key) {
    char data[512] = {0}; size_t length = sizeof(data);
    if (sysctlbyname(key, data, &length, NULL, 0) || !length || length > sizeof(data)) return @"";
    return [[NSString alloc] initWithBytes:data length:strnlen(data, length) encoding:NSUTF8StringEncoding] ?: @"";
}

static NSURL *ReportURL(void) {
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    return [documents URLByAppendingPathComponent:ReportName];
}

static NSDictionary *InspectGame(void) {
    NSMutableDictionary *report = [@{@"diagnostic_version":@2, @"date":NSDate.date.description,
        @"device":SystemString("hw.machine"), @"system_build":SystemString("kern.osversion"),
        @"target_bundle_id":GameID, @"silhouette_implemented":@NO, @"live_mesh_verified":@NO,
        @"target_process_started":@NO, @"target_process_memory_read":@NO,
        @"target_files_modified":@NO, @"network_requests":@0} mutableCopy];
    dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_LAZY);
    dlopen("/System/Library/Frameworks/MobileCoreServices.framework/MobileCoreServices", RTLD_LAZY);
    Class cls = NSClassFromString(@"LSApplicationProxy");
    id proxy = [cls respondsToSelector:@selector(applicationProxyForIdentifier:)] ?
        [cls applicationProxyForIdentifier:GameID] : nil;
    NSURL *bundle = [proxy respondsToSelector:@selector(bundleURL)] ? [proxy bundleURL] : nil;
    if (!bundle) { report[@"status"] = @"game-not-found"; return report; }
    NSURL *infoURL = [bundle URLByAppendingPathComponent:@"Info.plist"];
    NSDictionary *attributes = [NSFileManager.defaultManager attributesOfItemAtPath:infoURL.path error:NULL];
    if ([attributes[NSFileSize] unsignedLongLongValue] > 256 * 1024 ||
        ![attributes[NSFileType] isEqual:NSFileTypeRegular]) {
        report[@"status"] = @"game-info-unavailable"; return report;
    }
    NSDictionary *info = [NSDictionary dictionaryWithContentsOfURL:infoURL];
    if (![info[@"CFBundleIdentifier"] isEqual:GameID]) {
        report[@"status"] = @"game-identity-mismatch"; return report;
    }
    NSString *executable = info[@"CFBundleExecutable"];
    if (![executable isKindOfClass:NSString.class] || !executable.length ||
        ![executable isEqual:executable.lastPathComponent] || [executable isEqual:@"."] || [executable isEqual:@".."]) {
        report[@"status"] = @"game-executable-name-invalid"; return report;
    }
    report[@"installed_game"] = @{@"bundle_id":GameID, @"executable":executable,
        @"version":info[@"CFBundleShortVersionString"] ?: @"", @"build":info[@"CFBundleVersion"] ?: @""};
    report[@"executable_header"] = CSIInspectExecutable([bundle URLByAppendingPathComponent:executable]);
    report[@"bundle_resources"] = CSIInventory(bundle, 15000, 2048, 8);
    NSURL *container = [proxy respondsToSelector:@selector(dataContainerURL)] ? [proxy dataContainerURL] : nil;
    report[@"downloaded_resources"] = container ? CSIInventory(container, 15000, 2048, 8) : CSIError(@"data-container-unavailable");
    NSMutableArray *samples = [NSMutableArray new];
    NSString *version = info[@"CFBundleShortVersionString"], *build = info[@"CFBundleVersion"];
    NSCharacterSet *digitsAndDots = [NSCharacterSet characterSetWithCharactersInString:@"0123456789."];
    if ([version isKindOfClass:NSString.class] && [build isKindOfClass:NSString.class] &&
        version.length && version.length <= 32 && build.length && build.length <= 32 &&
        [version rangeOfCharacterFromSet:digitsAndDots.invertedSet].location == NSNotFound &&
        [build rangeOfCharacterFromSet:digitsAndDots.invertedSet].location == NSNotFound) {
        for (NSString *name in @[@"map_avatar_base", @"map_avatar_filter"]) {
            NSString *path = [NSString stringWithFormat:@"cookeddata/shadowtrackerextra/content/paks/%@_%@.%@.pak", name, version, build];
            NSMutableDictionary *sample = [CSIPakSample(bundle, path) mutableCopy];
            sample[@"root"] = @"bundle"; sample[@"relative_path"] = path; [samples addObject:sample];
        }
        NSString *path = [NSString stringWithFormat:@"Documents/ShadowTrackerExtra/Saved/Paks/map_avatermesh_%@.%@.pak", version, build];
        NSMutableDictionary *sample = [CSIPakSample(container, path) mutableCopy];
        sample[@"root"] = @"downloaded"; sample[@"relative_path"] = path; [samples addObject:sample];
    }
    // Scan avatar files separately so activity packs do not consume their limit.
    NSString *avatarPath = @"Documents/ShadowTrackerExtra/Saved/Paks/avatarpaks";
    NSDictionary *avatars = container ? CSIInventory([container URLByAppendingPathComponent:avatarPath], 4096, 2048, 4) :
        CSIError(@"data-container-unavailable");
    report[@"avatar_resource_directory"] = avatars;
    NSUInteger avatarSamples = 0;
    for (NSDictionary *resource in avatars[@"resources"]) {
        NSString *name = resource[@"relative_path"];
        if (![name.pathExtension.lowercaseString isEqual:@"pak"]) continue;
        NSString *path = [avatarPath stringByAppendingPathComponent:name];
        NSMutableDictionary *sample = [CSIPakSample(container, path) mutableCopy];
        sample[@"root"] = @"downloaded"; sample[@"relative_path"] = path; [samples addObject:sample];
        if (++avatarSamples >= 8) break;
    }
    report[@"pak_edge_samples"] = samples;
    NSUInteger sampled = 0;
    for (NSDictionary *sample in samples) if ([sample[@"status"] isEqual:@"sampled"]) sampled++;
    report[@"successful_pak_edge_samples"] = @(sampled);
    report[@"bounded_resource_edges_read"] = @(sampled > 0);
    report[@"status"] = @"inspection-complete";
    report[@"next_requirement"] = @"Identify version-matched asset contents or an available per-frame character mask; this file inventory does not supply a live silhouette.";
    return report;
}

@interface InspectionController : UIViewController
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UIButton *inspectButton;
@property(nonatomic, strong) UIButton *shareButton;
@end

@implementation InspectionController
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor = UIColor.systemBackgroundColor;
    UILabel *title = [UILabel new]; title.text = @"Core 模型检查";
    title.font = [UIFont boldSystemFontOfSize:26];
    self.status = [UILabel new]; self.status.numberOfLines = 0;
    self.status.text = @"请先关闭和平精英。\n\n检查游戏版本、人物资源清单及少量资源包头尾数据，确认完整人物轮廓的数据来源。完成后，请共享检查报告。";
    self.inspectButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.inspectButton setTitle:@"检查游戏资源" forState:UIControlStateNormal];
    [self.inspectButton addTarget:self action:@selector(inspect) forControlEvents:UIControlEventTouchUpInside];
    self.shareButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.shareButton setTitle:@"共享检查报告" forState:UIControlStateNormal];
    [self.shareButton addTarget:self action:@selector(shareReport) forControlEvents:UIControlEventTouchUpInside];
    self.shareButton.enabled = [NSFileManager.defaultManager fileExistsAtPath:ReportURL().path];
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[title, self.status, self.inspectButton, self.shareButton]];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 24; stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-24],
        [stack.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:48]]];
}
- (void)inspect {
    self.inspectButton.enabled = NO; self.shareButton.enabled = NO;
    self.status.text = @"正在检查游戏版本和资源清单…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSDictionary *report;
            @try { report = InspectGame(); }
            @catch (NSException *exception) { report = @{@"diagnostic_version":@2,
                @"status":@"inspection-exception", @"exception":exception.name,
                @"reason":exception.reason ?: @"", @"live_mesh_verified":@NO}; }
            NSError *error = nil;
            NSData *data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
            BOOL saved = data && [data writeToURL:ReportURL() options:NSDataWritingAtomic error:&error];
            dispatch_async(dispatch_get_main_queue(), ^{
                self.inspectButton.enabled = YES; self.shareButton.enabled = saved;
                if (!saved) self.status.text = [NSString stringWithFormat:@"报告保存失败：%@", error.localizedDescription ?: @"未知错误"];
                else if ([report[@"status"] isEqual:@"inspection-complete"]) {
                    NSDictionary *game = report[@"installed_game"];
                    self.status.text = [NSString stringWithFormat:@"检查完成。\n\n和平精英 %@（%@）\n\n请共享检查报告。资源清单仍需分析，完整人物轮廓尚未验证。", game[@"version"], game[@"build"]];
                } else self.status.text = [NSString stringWithFormat:@"检查未完成：%@\n\n请共享报告，以便确定原因。", report[@"status"]];
            });
        }
    });
}
- (void)shareReport {
    UIActivityViewController *activity = [[UIActivityViewController alloc] initWithActivityItems:@[ReportURL()] applicationActivities:nil];
    activity.popoverPresentationController.sourceView = self.shareButton;
    activity.popoverPresentationController.sourceRect = self.shareButton.bounds;
    [self presentViewController:activity animated:YES completion:nil];
}
@end

@interface InspectionDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation InspectionDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [InspectionController new]; [self.window makeKeyAndVisible]; return YES;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(InspectionDelegate.class)); }
}
