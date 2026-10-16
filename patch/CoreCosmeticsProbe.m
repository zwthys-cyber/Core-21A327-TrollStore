#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <ptrauth.h>
#import <stdatomic.h>
#import "CoreCosmeticsRead.h"
#import "CoreCosmeticsWriteCheck.h"
#import "CoreCosmeticsTrial.h"

// r1/r2 established the character read path and checked native writes on device.
// r3 changes one coat and uses the game's own deferred refresh, then restores it.
typedef struct { uint64_t words[17]; } CCPSnapshot;
typedef bool (*CCPSnapshotFunction)(CCPSnapshot *);
typedef bool (*CCPReadFunction)(uint32_t, uint64_t, size_t, void *);
typedef CCPNativeWriteResult (*CCPWriteFunction)(uint32_t, uint64_t, const void *, const void *, size_t);
typedef struct { uint32_t pid; CCPReadFunction read; CCPWriteFunction write; } CCPTarget;
typedef struct {
    uintptr_t coreBase;
    uint64_t game, pawn, avatar;
    CCPSnapshot snapshot;
    uint8_t binding[0x30];
    CCPSnapshotFunction getSnapshot;
    CCPTarget target;
} CCPResolvedTarget;
static CCPResolvedTarget TrialContext;
static CCPCoatTrial TrialState;
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
static const uint8_t WritePrefix[32] = {
    0x7f,0x23,0x03,0xd5,0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,
    0xf6,0x57,0x03,0xa9,0xf4,0x4f,0x04,0xa9,0xfd,0x7b,0x05,0xa9,0xfd,0x43,0x01,0x91
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
static CCPNativeWriteResult TargetWrite(void *context, uint64_t address, const void *expected,
                                        const void *replacement, size_t length) {
    CCPTarget *target = context;
    return target->write(target->pid, address, expected, replacement, length);
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
        if (item.type == 0 && item.item == 0) continue;
        [result addObject:@{@"slot_index": @(i), @"type": @(item.type), @"type_specific_id": @(item.item),
            @"gender": @(item.gender), @"operation": @(item.operation), @"version": @(item.version)}];
    }
    return result;
}
static NSDictionary *Sample(BOOL writeCheck, CCPResolvedTarget *resolved) {
    CCPReader local = {LocalRead, NULL};
    uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
    if (!CCPIdentity(local, base, CoreUUID) || !EntryMatches(base + 0x64C7C, SnapshotPrefix) ||
        !EntryMatches(base + 0x64D04, ReadPrefix)) return @{@"status": @"core-identity-mismatch"};
    CCPSnapshotFunction snapshot = (CCPSnapshotFunction)SignedEntry(base + 0x64C7C);
    CCPSnapshot before = {0}, after = {0};
    if (!snapshot(&before) || !before.words[1]) return @{@"status": @"core-session-not-ready"};
    CCPTarget target = {.pid = CCPU32((const uint8_t *)&before + 0x18),
                        .read = (CCPReadFunction)SignedEntry(base + 0x64D04), .write = NULL};
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
    NSMutableDictionary *result = [@{@"status": syncOK || clientOK ? @"candidate-layout-sampled" : @"avatar-arrays-unconfirmed",
        @"game_uuid_matches": @YES, @"component_owner_matches": @YES,
        @"sync_array_stable": @(syncOK), @"client_array_stable": @(clientOK),
        @"sync_item_count": @(sync.count), @"client_item_count": @(client.count), @"zero_slots_omitted": @YES,
        @"sync_items": ItemsJSON(&sync), @"client_items": ItemsJSON(&client)} mutableCopy];
    uint64_t onrep = 0;
    if (CCPReadBytes(remote, vtable + 0x1320, &onrep, sizeof(onrep)) && onrep >= game && onrep - game < 0xAF23BE4)
        result[@"avatar_onrep_rva"] = [NSString stringWithFormat:@"0x%llx", (unsigned long long)(onrep - game)];
    if (resolved && syncOK && onrep == game + 0x43D2EB0 && EntryMatches(base + 0x620B8, WritePrefix)) {
        uint8_t binding[0x30], repeated[0x30];
        if (CCPReadBytes(local, base + 0xC288B0, binding, sizeof(binding)) &&
            CCPReadBytes(local, base + 0xC288B0, repeated, sizeof(repeated)) &&
            !memcmp(binding, repeated, sizeof(binding)) && CCPU32(binding + 0x28) == target.pid) {
            *resolved = (CCPResolvedTarget){.coreBase = base, .game = game, .pawn = pawn, .avatar = avatar,
                .snapshot = before, .getSnapshot = snapshot, .target = target};
            memcpy(resolved->binding, binding, sizeof(binding));
            resolved->target.write = (CCPWriteFunction)SignedEntry(base + 0x620B8);
        }
    }
    if (writeCheck && syncOK) {
        if (!EntryMatches(base + 0x620B8, WritePrefix)) {
            result[@"write_check"] = @{@"attempted": @NO, @"status": @"core-write-entry-unconfirmed"};
        } else {
            target.write = (CCPWriteFunction)SignedEntry(base + 0x620B8);
            uint8_t capabilityA = 0, capabilityB = 0, primitiveReady = 0;
            BOOL capabilitiesRead = CCPReadBytes(local, base + 0xC27B2C, &capabilityA, 1) &&
                CCPReadBytes(local, base + 0xC27CA4, &capabilityB, 1) &&
                CCPReadBytes(local, base + 0xC3D59D, &primitiveReady, 1);
            CCPWriteCheck check = CCPCheckInitialOutfitWrite(remote, TargetWrite, &target, avatar + CCP_SYNC_OFFSET);
            result[@"write_check"] = @{@"attempted": @(check.attempted),
                @"status": !check.attempted ? @"initial-outfit-required" :
                    (check.nativeCommitted && check.readbackStable ? @"same-value-write-confirmed" : @"native-write-not-confirmed"),
                @"native_status": @(check.nativeStatus), @"written_bytes": @(check.writtenBytes),
                @"readback_stable": @(check.readbackStable), @"appearance_value_changed": @NO,
                @"capability_snapshot_read": @(capabilitiesRead), @"capability_a": @(capabilityA),
                @"capability_b": @(capabilityB), @"primitive_ready_bit": @(primitiveReady & 1)};
        }
    }
    return result;
}

static bool TrialBindingValid(void *context) {
    CCPResolvedTarget *target = context;
    CCPReader local = {LocalRead, NULL}, remote = {TargetRead, &target->target};
    CCPSnapshot now = {0}; uint8_t binding[0x30]; uint64_t game = 0, pawn = 0;
    return target->coreBase == (uintptr_t)_dyld_get_image_header(0) &&
        EntryMatches(target->coreBase + 0x620B8, WritePrefix) &&
        target->getSnapshot(&now) && now.words[1] == target->snapshot.words[1] &&
        CCPU32((const uint8_t *)&now + 0x18) == target->target.pid &&
        CCPReadBytes(local, target->coreBase + 0xC288B0, binding, sizeof(binding)) &&
        !memcmp(binding, target->binding, sizeof(binding)) &&
        CCPReadBytes(local, target->coreBase + 0xC54270 + 0x18, &game, sizeof(game)) && game == target->game &&
        CCPReadBytes(local, target->coreBase + 0xC54270 + 0x3A0, &pawn, sizeof(pawn)) && pawn == target->pawn &&
        CCPIdentity(remote, target->game, CCPGameUUID);
}
static CCPNativeWriteResult TrialTargetWrite(void *context, uint64_t address, const void *expected,
                                            const void *replacement, size_t length) {
    return TargetWrite(&((CCPResolvedTarget *)context)->target, address, expected, replacement, length);
}
static NSDictionary *TrialResultJSON(CCPTrialResult result) {
    NSArray *statuses = @[@"applied", @"restored", @"already-restored", @"binding-changed",
        @"layout-changed", @"initial-white-shirt-required", @"refresh-mode-unsupported", @"native-write-failed",
        @"readback-failed", @"slot-changed-by-game", @"no-active-change"];
    return @{@"status": statuses[result.status], @"native_status": @(result.nativeStatus),
        @"written_bytes": @(result.writtenBytes), @"id_write_confirmed": @(result.idConfirmed),
        @"refresh_queued": @(result.refreshQueued), @"change_still_owned": @(TrialState.active)};
}

@interface CCPCosmeticsController : NSObject
@property(nonatomic, strong) UIButton *button;
@property(nonatomic, strong) dispatch_source_t attachmentTimer;
@property(nonatomic, strong) dispatch_source_t captureTimer;
@property(nonatomic, strong) dispatch_queue_t captureQueue;
@property(nonatomic, strong) NSMutableArray *samples;
@property(nonatomic, strong) NSDictionary *writeCheckResult;
@property(nonatomic, strong) NSDictionary *latestLayout;
@property(nonatomic, strong) dispatch_source_t trialTimer;
@property(nonatomic, strong) NSMutableDictionary *trialReport;
@property(nonatomic) BOOL trialFinishing;
@property(nonatomic) uint32_t trialGeneration;
@property(nonatomic) UIBackgroundTaskIdentifier backgroundTask;
@property(nonatomic) BOOL capturing;
@property(nonatomic) uint32_t generation;
- (void)attach;
- (void)panel;
- (void)capture;
- (void)startTrial;
- (void)prepareTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt;
- (void)restoreTrial;
- (void)finishTrialOnQueue:(uint32_t)generation reason:(NSString *)reason;
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
                [self.button setTitle:@"内核美化" forState:UIControlStateNormal];
                self.button.accessibilityLabel = @"内核美化试穿与恢复";
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
    NSString *message = self.capturing ? @"试穿处理中。请切回训练场观察外套，约 15 秒后自动恢复，也可以点“恢复原样”。结束后可导出报告。" :
        @"先穿回刚才的初始白 T 恤，进入训练场；返回 Core 完成初始化并开启原有游戏采集，再开始试穿。\n\n测试将白 T 恤替换为包内的至尊金龙外套，约 15 秒自动恢复。实际画面效果仍需你确认。";
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Core 内核美化试穿 r3"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    if (!self.capturing) [alert addAction:[UIAlertAction actionWithTitle:@"试穿至尊金龙外套" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { [self startTrial]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"恢复原样" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { [self restoreTrial]; }]];
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
- (void)writeTrialReport {
    if (!self.trialReport) return;
    self.trialReport[@"captured_at_unix"] = @([NSDate date].timeIntervalSince1970);
    NSData *json = [NSJSONSerialization dataWithJSONObject:self.trialReport options:NSJSONWritingPrettyPrinted error:NULL];
    if (json) [json writeToURL:[self reportURL] options:NSDataWritingAtomic error:NULL];
}
- (void)startTrial {
    if (self.capturing) return;
    self.capturing = YES;
    uint32_t generation = ++self.generation;
    if (!self.captureQueue) self.captureQueue = dispatch_queue_create("Core.cosmetics.operations", DISPATCH_QUEUE_SERIAL);
    self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Core cosmetics trial"
        expirationHandler:^{
            if (self.generation != generation || !self.capturing) return;
            dispatch_async(self.captureQueue, ^{ [self finishTrialOnQueue:generation reason:@"background-expiration"]; });
        }];
    dispatch_async(self.captureQueue, ^{
        self.trialGeneration = generation;
        self.trialFinishing = NO;
        if (TrialState.active) {
            dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
            return; // Preserve the journal of an earlier change until restored.
        }
        self.trialReport = [@{@"schema_version": @3, @"trial_revision": @3, @"mode": @"single-coat-trial",
            @"game_version": @"1.38.12", @"game_build": @"15915",
            @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
            @"kernel_writes_implemented": @YES, @"appearance_changes_implemented": @YES,
            @"visual_effect_verified": @NO, @"preset": @"至尊金龙外套", @"original_id": @(CCP_ORIGINAL_COAT),
            @"target_id": @(CCP_TRIAL_COAT), @"automatic_restore_seconds": @15} mutableCopy];
        [self prepareTrialOnQueue:generation attempt:0];
    });
}
- (void)prepareTrialOnQueue:(uint32_t)generation attempt:(unsigned)attempt {
        if (self.trialGeneration != generation || self.trialFinishing) return;
        TrialContext = (CCPResolvedTarget){0};
        NSDictionary *before = Sample(NO, &TrialContext);
        self.trialReport[@"before"] = before;
        if (!TrialContext.avatar || !TrialBindingValid(&TrialContext)) {
            self.trialReport[@"status"] = @"waiting-for-core-or-character";
            self.trialReport[@"waiting_attempts"] = @(attempt + 1);
            [self writeTrialReport];
            dispatch_async(dispatch_get_main_queue(), ^{
                if (self.generation == generation && self.capturing)
                    [self.button setTitle:@"等待 Core 就绪" forState:UIControlStateNormal];
            });
            if (attempt >= 60) [self finishTrialOnQueue:generation reason:@"readiness-timeout"];
            else dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), self.captureQueue,
                ^{ [self prepareTrialOnQueue:generation attempt:attempt + 1]; });
            return;
        }
        // Leave time to switch back to the training ground. Revalidate the
        // binding and object chain at the actual point of use.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), self.captureQueue, ^{
            if (self.trialGeneration != generation || self.trialFinishing) return;
            TrialState = (CCPCoatTrial){.game = TrialContext.game, .pawn = TrialContext.pawn, .avatar = TrialContext.avatar};
            CCPReader reader = {TargetRead, &TrialContext.target};
            CCPTrialResult applied = CCPApplyCoat(reader, TrialTargetWrite, &TrialContext, TrialBindingValid, &TrialState);
            self.trialReport[@"apply"] = TrialResultJSON(applied);
            self.trialReport[@"status"] = applied.idConfirmed ? @"id-applied-awaiting-visual-confirmation" : @"apply-not-confirmed";
            [self writeTrialReport];
            if (!applied.idConfirmed) {
                [self finishTrialOnQueue:generation reason:@"apply-failed"];
                return;
            }
            self.samples = [NSMutableArray array];
            self.trialTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self.captureQueue);
            dispatch_source_set_timer(self.trialTimer, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
                                      3 * NSEC_PER_SEC, NSEC_PER_SEC / 4);
            dispatch_source_set_event_handler(self.trialTimer, ^{
                @autoreleasepool {
                    if (self.trialGeneration != generation || self.trialFinishing) return;
                    uint32_t current = 0; uint8_t pending = 0;
                    BOOL bound = TrialBindingValid(&TrialContext);
                    BOOL sameLayout = bound && CCPTrialLayout(reader, &TrialState, &current);
                    BOOL flagRead = sameLayout && CCPReadBytes(reader, TrialState.avatar + CCP_REFRESH_PENDING, &pending, 1);
                    [self.samples addObject:@{@"binding_matches": @(bound), @"layout_matches": @(sameLayout),
                        @"current_coat_id": @(current), @"refresh_flag_read": @(flagRead), @"refresh_pending": @(pending)}];
                    self.trialReport[@"observations"] = self.samples.copy;
                    [self writeTrialReport];
                    if (!bound || !sameLayout || self.samples.count >= 5)
                        [self finishTrialOnQueue:generation reason:bound && sameLayout ? @"automatic-restore" : @"context-changed"];
                }
            });
            dispatch_resume(self.trialTimer);
        });
}
- (void)restoreTrial {
    if (!self.captureQueue) return;
    uint32_t generation = self.generation;
    dispatch_async(self.captureQueue, ^{
        if (TrialState.active) self.trialFinishing = NO;
        [self finishTrialOnQueue:generation reason:@"manual-restore"];
    });
}
- (void)finishTrialOnQueue:(uint32_t)generation reason:(NSString *)reason {
    if (self.trialGeneration != generation || self.trialFinishing) return;
    self.trialFinishing = YES;
    if (self.trialTimer) { dispatch_source_cancel(self.trialTimer); self.trialTimer = nil; }
    CCPReader reader = {TargetRead, &TrialContext.target};
    CCPTrialResult restored = CCPRestoreCoat(reader, TrialTargetWrite, &TrialContext, TrialBindingValid, &TrialState);
    self.trialReport[@"restore"] = TrialResultJSON(restored);
    self.trialReport[@"finish_reason"] = reason;
    [self writeTrialReport];
    dispatch_async(dispatch_get_main_queue(), ^{ [self finishGeneration:generation]; });
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
        self.writeCheckResult = nil;
        self.latestLayout = @{};
        self.captureTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self.captureQueue);
        dispatch_source_set_timer(self.captureTimer, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
                                  3 * NSEC_PER_SEC, NSEC_PER_SEC / 4);
        dispatch_source_set_event_handler(self.captureTimer, ^{
            @autoreleasepool {
                if (atomic_load(&CaptureStopped)) { dispatch_source_cancel(self.captureTimer); self.captureTimer = nil; return; }
                NSDictionary *sample = Sample(self.writeCheckResult == nil, NULL);
                if (sample[@"write_check"]) self.writeCheckResult = sample[@"write_check"];
                self.latestLayout = sample;
                NSMutableDictionary *summary = [sample mutableCopy];
                [summary removeObjectsForKeys:@[@"sync_items", @"client_items", @"write_check"]];
                [self.samples addObject:summary];
                NSDictionary *report = @{@"schema_version": @2, @"probe_revision": @2,
                    @"mode": @"same-value-write-adaptation", @"game_version": @"1.38.12", @"game_build": @"15915",
                    @"game_uuid": @"34B785B2-0DAB-3992-985D-359E6BF45585",
                    @"captured_at_unix": @([NSDate date].timeIntervalSince1970),
                    @"kernel_writes_implemented": @YES, @"appearance_changes_implemented": @NO,
                    @"runtime_layout_validated": @NO, @"write_check": self.writeCheckResult ?: @{},
                    @"latest_layout": self.latestLayout, @"samples": self.samples.copy};
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
    [self.button setTitle:@"内核美化" forState:UIControlStateNormal];
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
