#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <ptrauth.h>
#import <stdatomic.h>
#import "CoreCosmeticsRead.h"

// Deliberately no reference to Core's compare/write entry in this build.
// Device evidence is required before any appearance change can be enabled.
typedef struct { uint64_t words[17]; } CCPSnapshot;
typedef bool (*CCPSnapshotFunction)(CCPSnapshot *);
typedef bool (*CCPReadFunction)(uint32_t, uint64_t, size_t, void *);
typedef struct { uint32_t pid; CCPReadFunction read; } CCPTarget;
static const uint8_t CoreUUID[16] = {
    0x4e,0xd1,0xfd,0x29,0xa4,0x6a,0x36,0xb3,
    0x8d,0xfc,0x36,0xf6,0x7e,0xa9,0x29,0xd1
};
static const uint8_t SnapshotPrefix[32] = {
    0xe0,0x03,0x00,0xb4,0x7f,0x23,0x03,0xd5,0xf4,0x4f,0xbe,0xa9,0xfd,0x7b,0x01,0xa9,
    0xfd,0x43,0x00,0x91,0xf3,0x03,0x00,0xaa,0x94,0x5b,0x00,0xb0,0x94,0xe2,0x06,0x91
};
static const uint8_t ReadPrefix[32] = {
    0xc5,0xee,0xff,0x17,0x7f,0x23,0x03,0xd5,0xf6,0x57,0xbd,0xa9,0xf4,0x4f,0x01,0xa9,
    0xfd,0x7b,0x02,0xa9,0xfd,0x83,0x00,0x91,0xf3,0x03,0x00,0xaa,0x40,0x00,0x00,0xb4
};
static bool LocalRead(void *context, uint64_t address, void *out, size_t length) {
    vm_size_t copied = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)address, (vm_size_t)length,
        (vm_address_t)out, &copied) == KERN_SUCCESS && copied == length;
}
static bool TargetRead(void *context, uint64_t address, void *out, size_t length) {
    CCPTarget *target = context;
    return target->read(target->pid, address, length, out);
}
static void *SignedEntry(uintptr_t address) {
#if __has_feature(ptrauth_calls)
    return ptrauth_sign_unauthenticated((void *)address, ptrauth_key_function_pointer, 0);
#else
    return (void *)address;
#endif
}
static bool EntryMatches(uintptr_t address, const uint8_t expected[32]) {
    uint8_t bytes[32];
    return CCPReadBytes((CCPReader){LocalRead, NULL}, address, bytes, sizeof(bytes)) &&
        !memcmp(bytes, expected, sizeof(bytes));
}
static NSArray *ItemsJSON(const CCPItems *items) {
    NSMutableArray *result = [NSMutableArray arrayWithCapacity:items->count];
    for (uint32_t i = 0; i < items->count; i++) {
        CCPItem item = items->items[i];
        [result addObject:@{@"type": @(item.type), @"type_specific_id": @(item.item),
            @"gender": @(item.gender), @"operation": @(item.operation), @"version": @(item.version)}];
    }
    return result;
}
static NSDictionary *Sample(void) {
    CCPReader local = {LocalRead, NULL};
    uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
    if (!CCPIdentity(local, base, CoreUUID) || !EntryMatches(base + 0x64C7C, SnapshotPrefix) ||
        !EntryMatches(base + 0x64D04, ReadPrefix)) return @{@"status": @"core-identity-mismatch"};
    CCPSnapshotFunction snapshot = (CCPSnapshotFunction)SignedEntry(base + 0x64C7C);
    CCPSnapshot before = {0}, after = {0};
    if (!snapshot(&before) || !before.words[1]) return @{@"status": @"core-session-not-ready"};
    CCPTarget target = {CCPU32((const uint8_t *)&before + 0x18),
                        (CCPReadFunction)SignedEntry(base + 0x64D04)};
    if (!target.pid || target.pid > INT32_MAX) return @{@"status": @"target-not-bound"};
    CCPReader remote = {TargetRead, &target};
    uint8_t config[0x3A8], configAfter[sizeof(config)];
    if (!CCPReadBytes(local, base + 0xC54270, config, sizeof(config))) return @{@"status": @"core-context-unavailable"};
    uint64_t game = CCPU64(config + 0x18), pawn = CCPU64(config + 0x3A0);
    if (!CCPIdentity(remote, game, CCPGameUUID)) return @{@"status": @"game-identity-unconfirmed"};
    if (!CCPPointer(pawn)) return @{@"status": @"local-character-unavailable", @"game_uuid_matches": @YES};
    uint64_t avatar = 0, outer = 0, vtable = 0;
    if (!CCPReadBytes(remote, pawn + CCP_AVATAR_OFFSET, &avatar, sizeof(avatar)) || !CCPPointer(avatar))
        return @{@"status": @"avatar-component-unavailable", @"game_uuid_matches": @YES};
    if (!CCPReadBytes(remote, avatar, &vtable, sizeof(vtable)) || vtable < game || vtable - game >= 0x120E0000 ||
        !CCPReadBytes(remote, avatar + 0x20, &outer, sizeof(outer)) || outer != pawn)
        return @{@"status": @"avatar-owner-unconfirmed", @"game_uuid_matches": @YES};
    CCPItems sync = {0}, client = {0};
    bool syncOK = CCPReadItems(remote, avatar + CCP_SYNC_OFFSET, &sync);
    bool clientOK = CCPReadItems(remote, avatar + CCP_CLIENT_OFFSET, &client);
    uint64_t avatarAfter = 0;
    if (!snapshot(&after) || memcmp(&before, &after, 0x28) ||
        !CCPReadBytes(local, base + 0xC54270, configAfter, sizeof(configAfter)) ||
        CCPU64(configAfter + 0x18) != game || CCPU64(configAfter + 0x3A0) != pawn ||
        !CCPReadBytes(remote, pawn + CCP_AVATAR_OFFSET, &avatarAfter, sizeof(avatarAfter)) || avatarAfter != avatar)
        return @{@"status": @"session-or-character-changed"};
    return @{@"status": syncOK || clientOK ? @"candidate-layout-sampled" : @"avatar-arrays-unconfirmed",
        @"game_uuid_matches": @YES, @"component_owner_matches": @YES,
        @"sync_array_stable": @(syncOK), @"client_array_stable": @(clientOK),
        @"sync_items": ItemsJSON(&sync), @"client_items": ItemsJSON(&client)};
}

@interface CCPCosmeticsController : NSObject
@property(nonatomic, strong) UIButton *button;
@property(nonatomic, strong) dispatch_source_t attachmentTimer;
@property(nonatomic, strong) dispatch_source_t captureTimer;
@property(nonatomic, strong) dispatch_queue_t captureQueue;
@property(nonatomic, strong) NSMutableArray *samples;
@property(nonatomic) UIBackgroundTaskIdentifier backgroundTask;
@property(nonatomic) BOOL capturing;
@property(nonatomic) uint32_t generation;
- (void)attach;
- (void)panel;
- (void)capture;
- (void)finishGeneration:(uint32_t)generation;
@end
static atomic_bool CaptureStopped = true;

@implementation CCPCosmeticsController
- (NSURL *)reportURL {
    NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                             inDomains:NSUserDomainMask].firstObject;
    return [documents URLByAppendingPathComponent:@"Core-cosmetics-probe.json"];
}
- (UIViewController *)presenter {
    UIViewController *controller = self.button.window.rootViewController;
    while (controller.presentedViewController) controller = controller.presentedViewController;
    return controller;
}
- (void)attach {
    if (UIApplication.sharedApplication.applicationState != UIApplicationStateActive || self.button.window) return;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class] || scene.activationState != UISceneActivationStateForegroundActive) continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows) {
            if (window.hidden || !window.rootViewController || window.windowLevel != UIWindowLevelNormal) continue;
            if (!self.button) {
                self.button = [UIButton buttonWithType:UIButtonTypeSystem];
                [self.button setTitle:@"美化适配" forState:UIControlStateNormal];
                self.button.accessibilityLabel = @"美化适配数据采集";
                self.button.backgroundColor = [UIColor colorWithWhite:0.12 alpha:0.92];
                [self.button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
                self.button.layer.cornerRadius = 10;
                self.button.translatesAutoresizingMaskIntoConstraints = NO;
                [self.button addTarget:self action:@selector(panel) forControlEvents:UIControlEventTouchUpInside];
            }
            [window addSubview:self.button];
            [NSLayoutConstraint activateConstraints:@[
                [self.button.trailingAnchor constraintEqualToAnchor:window.safeAreaLayoutGuide.trailingAnchor constant:-16],
                [self.button.bottomAnchor constraintEqualToAnchor:window.safeAreaLayoutGuide.bottomAnchor constant:-16],
                [self.button.widthAnchor constraintEqualToConstant:104], [self.button.heightAnchor constraintEqualToConstant:44]]];
            return;
        }
    }
}
- (void)panel {
    UIViewController *controller = [self presenter];
    if (!controller || [controller isKindOfClass:UIAlertController.class]) return;
    NSString *message = self.capturing ? @"正在采集。请切回和平精英训练场，约 24 秒后返回导出报告。" :
        @"适用于和平精英 1.38.12。先进入训练场，返回 Core 完成初始化并启动原有游戏采集，再点开始并切回训练场，约 24 秒后返回导出报告。\n\n此版采集角色外观 ID，用于完成美化适配，尚未启用换装。";
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"美化适配采集版 r1"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    if (!self.capturing) [alert addAction:[UIAlertAction actionWithTitle:@"开始采集" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { [self capture]; }]];
    if ([[NSFileManager defaultManager] fileExistsAtPath:[self reportURL].path]) {
        [alert addAction:[UIAlertAction actionWithTitle:@"导出报告" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            UIActivityViewController *share = [[UIActivityViewController alloc] initWithActivityItems:@[[self reportURL]] applicationActivities:nil];
            share.popoverPresentationController.sourceView = self.button;
            share.popoverPresentationController.sourceRect = self.button.bounds;
            [controller dismissViewControllerAnimated:YES completion:^{
                [controller presentViewController:share animated:YES completion:nil];
            }];
        }]];
    }
    [alert addAction:[UIAlertAction actionWithTitle:@"关闭" style:UIAlertActionStyleCancel handler:nil]];
    [controller presentViewController:alert animated:YES completion:nil];
}
- (void)capture {
    if (self.capturing) return;
    self.capturing = YES;
    uint32_t generation = ++self.generation;
    atomic_store(&CaptureStopped, false);
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.read-only", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core cosmetics adaptation"
        expirationHandler:^{
            if (self.generation != generation || !self.capturing) return;
            atomic_store(&CaptureStopped, true);
            dispatch_async(self.captureQueue, ^{
                if (self.captureTimer) { dispatch_source_cancel(self.captureTimer); self.captureTimer = nil; }
                dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
            });
        }];
    dispatch_async(self.captureQueue, ^{
        self.samples = [NSMutableArray array];
        self.captureTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self.captureQueue);
        dispatch_source_set_timer(self.captureTimer, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
                                  3 * NSEC_PER_SEC, NSEC_PER_SEC / 4);
        dispatch_source_set_event_handler(self.captureTimer, ^{
            @autoreleasepool {
                if (atomic_load(&CaptureStopped)) { dispatch_source_cancel(self.captureTimer); self.captureTimer = nil; return; }
                [self.samples addObject:Sample()];
                NSDictionary *report = @{@"schema_version": @1, @"probe_revision": @1,
                    @"mode": @"read-only-adaptation", @"game_version": @"1.38.12", @"game_build": @"15915",
                    @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
                    @"captured_at_unix": @([NSDate date].timeIntervalSince1970),
                    @"kernel_writes_implemented": @NO, @"runtime_layout_validated": @NO,
                    @"samples": self.samples.copy};
                NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];
                [json writeToURL:[self reportURL] options:NSDataWritingAtomic error:NULL];
                if (self.samples.count >= 8) {
                    atomic_store(&CaptureStopped, true);
                    dispatch_source_cancel(self.captureTimer); self.captureTimer = nil;
                    dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
                }
            }
        });
        dispatch_resume(self.captureTimer);
    });
}
- (void)finishGeneration:(uint32_t)generation {
    if (self.generation != generation || !self.capturing) return;
    self.capturing = NO;
    if (self.backgroundTask != UIBackgroundTaskInvalid) {
        [UIApplication.sharedApplication endBackgroundTask:self.backgroundTask];
        self.backgroundTask = UIBackgroundTaskInvalid;
    }
}
@end

__attribute__((constructor)) static void StartCosmeticsProbe(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        static CCPCosmeticsController *controller;
        controller = [CCPCosmeticsController new];
        controller.backgroundTask = UIBackgroundTaskInvalid;
        controller.attachmentTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        dispatch_source_set_timer(controller.attachmentTimer, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC),
                                  3 * NSEC_PER_SEC, NSEC_PER_SEC);
        dispatch_source_set_event_handler(controller.attachmentTimer, ^{ [controller attach]; });
        dispatch_resume(controller.attachmentTimer);
    });
}
