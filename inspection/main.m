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
    NSMutableDictionary *report = [@{@"diagnostic_version":@3, @"date":NSDate.date.description,
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
@property(nonatomic, strong) UIButton *exportButton;
@property(nonatomic, strong) NSDictionary *latestReport;
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
    self.shareButton.enabled = NO;
    self.exportButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.exportButton setTitle:@"导出资源样本" forState:UIControlStateNormal];
    [self.exportButton addTarget:self action:@selector(exportSamples) forControlEvents:UIControlEventTouchUpInside];
    self.exportButton.enabled = NO;
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[title, self.status, self.inspectButton, self.shareButton, self.exportButton]];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 24; stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-24],
        [stack.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:48]]];
}
- (void)inspect {
    self.inspectButton.enabled = NO; self.shareButton.enabled = NO; self.exportButton.enabled = NO; self.latestReport = nil;
    self.status.text = @"正在检查游戏版本和资源清单…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSDictionary *report;
            @try { report = InspectGame(); }
            @catch (NSException *exception) { report = @{@"diagnostic_version":@3,
                @"status":@"inspection-exception", @"exception":exception.name,
                @"reason":exception.reason ?: @"", @"live_mesh_verified":@NO}; }
            NSError *error = nil;
            NSData *data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
            BOOL saved = data && [data writeToURL:ReportURL() options:NSDataWritingAtomic error:&error];
            dispatch_async(dispatch_get_main_queue(), ^{
                self.inspectButton.enabled = YES; self.shareButton.enabled = saved;
                self.latestReport = saved ? report : nil;
                BOOL hasCandidate = NO;
                for (NSDictionary *sample in report[@"pak_edge_samples"]) if (CSIHasClothingCandidate(sample)) hasCandidate = YES;
                self.exportButton.enabled = saved && hasCandidate;
                if (!saved) self.status.text = [NSString stringWithFormat:@"报告保存失败：%@", error.localizedDescription ?: @"未知错误"];
                else if ([report[@"status"] isEqual:@"inspection-complete"]) {
                    NSDictionary *game = report[@"installed_game"];
                    self.status.text = [NSString stringWithFormat:@"检查完成。\n\n和平精英 %@（%@）\n\n%@\n\n完整人物轮廓和遮挡分色仍待验证。", game[@"version"], game[@"build"], hasCandidate ? @"已找到小型衣服或配饰候选包，可点击“导出资源样本”。" : @"请共享检查报告，继续确定资源来源。"];
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
- (void)exportSamples {
    NSDictionary *report = self.latestReport;
    self.exportButton.enabled = NO; self.inspectButton.enabled = NO; self.shareButton.enabled = NO;
    self.status.text = @"正在导出候选资源样本…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSMutableArray *files = [NSMutableArray new], *records = [NSMutableArray new], *failures = [NSMutableArray new];
            NSURL *documents = [ReportURL() URLByDeletingLastPathComponent];
            NSURL *parent = [documents URLByAppendingPathComponent:@"CoreModelSamples" isDirectory:YES];
            NSURL *directory = [parent
                URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES];
            NSError *error = nil;
            @try {
                BOOL parentReady = [NSFileManager.defaultManager createDirectoryAtURL:parent withIntermediateDirectories:NO
                    attributes:@{NSFilePosixPermissions:@0700} error:&error];
                struct stat parentStatus;
                parentReady = parentReady && !lstat(parent.fileSystemRepresentation, &parentStatus) && S_ISDIR(parentStatus.st_mode);
                if (parentReady && [NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:NO
                    attributes:@{NSFilePosixPermissions:@0700} error:&error]) {
                    Class cls = NSClassFromString(@"LSApplicationProxy");
                    id proxy = [cls respondsToSelector:@selector(applicationProxyForIdentifier:)] ? [cls applicationProxyForIdentifier:GameID] : nil;
                    NSURL *bundle = [proxy respondsToSelector:@selector(bundleURL)] ? [proxy bundleURL] : nil;
                    NSURL *container = [proxy respondsToSelector:@selector(dataContainerURL)] ? [proxy dataContainerURL] : nil;
                    NSDictionary *info = bundle ? [NSDictionary dictionaryWithContentsOfURL:[bundle URLByAppendingPathComponent:@"Info.plist"]] : nil;
                    NSDictionary *game = report[@"installed_game"];
                    BOOL sameGame = [info[@"CFBundleIdentifier"] isEqual:GameID] &&
                        [info[@"CFBundleShortVersionString"] isEqual:game[@"version"]] && [info[@"CFBundleVersion"] isEqual:game[@"build"]];
                    if (sameGame) for (NSDictionary *sample in report[@"pak_edge_samples"]) {
                        if (!CSIHasClothingCandidate(sample)) continue;
                        NSURL *root = [sample[@"root"] isEqual:@"bundle"] ? bundle : container;
                        NSDictionary *copied = root ? CSICopyPakSample(root, sample, directory) : CSIError(@"source-root-unavailable");
                        if ([copied[@"status"] isEqual:@"exported"]) {
                            [records addObject:copied]; [files addObject:[directory URLByAppendingPathComponent:copied[@"file_name"]]];
                        } else [failures addObject:@{@"file":sample[@"relative_path"] ?: @"", @"result":copied}];
                        if (records.count >= 2) break;
                    }
                    else [failures addObject:CSIError(@"installed-game-changed-since-scan")];
                    if (records.count) {
                        NSDictionary *manifest = @{@"export_version":@1, @"diagnostic_version":@3,
                            @"installed_game":game ?: @{}, @"files":records, @"failures":failures,
                            @"live_mesh_verified":@NO, @"pixel_depth_available":@NO, @"occlusion_coloring_implemented":@NO};
                        NSData *encoded = [NSJSONSerialization dataWithJSONObject:manifest options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
                        NSURL *manifestURL = [directory URLByAppendingPathComponent:@"Core-model-samples-manifest.json"];
                        if (encoded && [encoded writeToURL:manifestURL options:NSDataWritingAtomic error:&error]) [files addObject:manifestURL];
                        else [files removeAllObjects];
                    }
                }
            } @catch (NSException *exception) {
                error = [NSError errorWithDomain:@"CoreModelExport" code:1 userInfo:@{NSLocalizedDescriptionKey:exception.reason ?: exception.name}];
                [files removeAllObjects];
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                self.inspectButton.enabled = YES; self.shareButton.enabled = YES; self.exportButton.enabled = YES;
                if (!files.count) {
                    self.status.text = [NSString stringWithFormat:@"导出未完成：%@\n\n请重新检查并共享报告。", error.localizedDescription ?: failures.description];
                    return;
                }
                self.status.text = [NSString stringWithFormat:@"已导出 %lu 个候选资源包及清单。\n\n请共享这些文件交给分析人员。完整人物轮廓及白色可见／红色遮挡效果尚未实现。", (unsigned long)records.count];
                UIActivityViewController *activity = [[UIActivityViewController alloc] initWithActivityItems:files applicationActivities:nil];
                activity.popoverPresentationController.sourceView = self.exportButton;
                activity.popoverPresentationController.sourceRect = self.exportButton.bounds;
                [self presentViewController:activity animated:YES completion:nil];
            });
        }
    });
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
