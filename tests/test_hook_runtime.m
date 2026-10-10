// Runs the actual production hook installer on macOS with a fake application class.
// The setter calls the new hook before returning, reproducing the v1 publication race.
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <objc/runtime.h>
#import <assert.h>

@interface QXA107 : NSObject
- (BOOL)qm509:(NSURL *)cache metadataURL:(NSURL *)metadata
  matchesProduct:(NSString *)product build:(NSString *)build board:(NSString *)board;
- (BOOL)qm539:(NSData *)data source:(NSURL *)source productType:(NSString *)product
  buildVersion:(NSString *)build boardConfig:(NSString *)board error:(NSError **)error;
@end

@implementation QXA107
- (BOOL)qm509:(NSURL *)cache metadataURL:(NSURL *)metadata
  matchesProduct:(NSString *)product build:(NSString *)build board:(NSString *)board {
    return [cache.path isEqualToString:@"/test/kc"] &&
           [metadata.path isEqualToString:@"/test/kc.meta.plist"] &&
           [product isEqualToString:@"iPhone16,2"] &&
           [build isEqualToString:@"21A327"] && [board isEqualToString:@"d84ap"];
}
- (BOOL)qm539:(NSData *)data source:(NSURL *)source productType:(NSString *)product
  buildVersion:(NSString *)build boardConfig:(NSString *)board error:(NSError **)error {
    assert(!"Publisher must not be invoked for this host's mismatched identity");
    return NO;
}
@end

static IMP (*RealSetter)(Method, IMP) = method_setImplementation;
static BOOL CallbackDuringPublication;
static IMP SetterWithImmediateCallback(Method method, IMP replacement) {
    IMP previous = RealSetter(method, replacement);
    @autoreleasepool {
        QXA107 *object = [QXA107 new];
        BOOL valid = [object qm509:[NSURL fileURLWithPath:@"/test/kc"]
            metadataURL:[NSURL fileURLWithPath:@"/test/kc.meta.plist"]
            matchesProduct:@"iPhone16,2" build:@"21A327" board:@"d84ap"];
        assert(valid && "Original implementation must already be available inside setter");
        CallbackDuringPublication = YES;
    }
    return previous;
}

#define method_setImplementation SetterWithImmediateCallback
#define CBC_HOST_TEST 1
#include "../patch/CoreBootCache.m"
#undef method_setImplementation

int main(void) {
    @autoreleasepool {
        assert(CallbackDuringPublication);
        dispatch_apply(2000, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t index) {
            @autoreleasepool {
                QXA107 *object = [QXA107 new];
                assert([object qm509:[NSURL fileURLWithPath:@"/test/kc"]
                    metadataURL:[NSURL fileURLWithPath:@"/test/kc.meta.plist"]
                    matchesProduct:@"iPhone16,2" build:@"21A327" board:@"d84ap"]);
            }
        });
        QXA107 *object = [QXA107 new];
        assert(![object qm509:[NSURL fileURLWithPath:@"/test/kc"]
            metadataURL:[NSURL fileURLWithPath:@"/test/kc.meta.plist"]
            matchesProduct:@"iPhone16,2" build:@"21A329" board:@"d84ap"]);
        puts("PASS: replacement callable during setter; 2000 concurrent original calls; wrong build rejected");
    }
    return 0;
}
