#import <Foundation/Foundation.h>
#include <assert.h>
#define CBC_PROBE_TIMEOUT 2.0
#include "../patch/CoreBootProbeClient.h"

static int Fixture(int argc, char **argv) {
    assert(argc == 4);
    NSString *mode = @(getenv("CBC_TEST_PROBE_MODE") ?: "success");
    if ([mode isEqualToString:@"timeout"]) { sleep(4); return 0; }
    if ([mode isEqualToString:@"oversized"]) {
        char bytes[8193]; memset(bytes, 'X', sizeof(bytes));
        assert(write(STDOUT_FILENO, bytes, sizeof(bytes)) == sizeof(bytes)); return 0;
    }
    const char bytes[] = "test-kernelcache";
    assert(write(3, bytes, sizeof(bytes) - 1) == sizeof(bytes) - 1);
    NSString *hash = [@"" stringByPaddingToLength:96 withString:@"A" startingAtIndex:0];
    NSString *pathHash = [mode isEqualToString:@"wrong-path"] ?
        [@"" stringByPaddingToLength:96 withString:@"B" startingAtIndex:0] : hash;
    NSDictionary *report = @{@"protocol":@1, @"success":@(![mode isEqualToString:@"failed"]),
        @"stage":@"fixture", @"errno":@5, @"boot_hash":hash,
        @"source_path":[NSString stringWithFormat:@"/private/preboot/%@/System/Library/Caches/com.apple.kernelcaches/kernelcache", pathHash],
        @"bytes":@([mode isEqualToString:@"wrong-size"] ? 4 : sizeof(bytes) - 1),
        @"product":@(argv[1]), @"build":[mode isEqualToString:@"wrong-build"] ? @"21A329" : @(argv[2]),
        @"board":@(argv[3])};
    NSData *json = [NSJSONSerialization dataWithJSONObject:report options:0 error:NULL];
    assert(write(STDOUT_FILENO, json.bytes, json.length) == (ssize_t)json.length);
    return [mode isEqualToString:@"failed"] ? 1 : 0;
}

static NSUInteger DescriptorCount(void) {
    NSUInteger count = 0;
    for (int descriptor = 3; descriptor < 128; ++descriptor)
        if (fcntl(descriptor, F_GETFD) >= 0) ++count;
    return count;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc == 4) return Fixture(argc, argv);
        NSString *helper = NSBundle.mainBundle.executablePath;
        NSURL *directory = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
        setenv("CBC_TEST_PROBE_MODE", "success", 1);
        NSError *error = nil;
        NSDictionary *result = RunBootProbe(helper, @"iPhone16,2", @"21A327", @"D84AP", directory, &error);
        assert(result && !error);
        NSString *path = result[@"temporary_path"];
        assert([[NSData dataWithContentsOfFile:path] isEqual:[@"test-kernelcache" dataUsingEncoding:NSUTF8StringEncoding]]);
        assert([NSFileManager.defaultManager removeItemAtPath:path error:NULL]);
        NSUInteger descriptors = DescriptorCount();
        for (NSString *mode in @[@"wrong-build", @"wrong-path", @"wrong-size", @"failed", @"oversized", @"timeout"]) {
            setenv("CBC_TEST_PROBE_MODE", mode.UTF8String, 1);
            error = nil;
            result = RunBootProbe(helper, @"iPhone16,2", @"21A327", @"D84AP", directory, &error);
            assert(!result && error);
            assert([NSFileManager.defaultManager contentsOfDirectoryAtURL:directory includingPropertiesForKeys:nil options:0 error:NULL].count == 0);
            assert(DescriptorCount() == descriptors);
        }
        result = RunBootProbe(@"/nonexistent/core-boot-probe", @"iPhone16,2", @"21A327", @"D84AP", directory, &error);
        assert(!result && error);
        assert([NSFileManager.defaultManager contentsOfDirectoryAtURL:directory includingPropertiesForKeys:nil options:0 error:NULL].count == 0);
        assert(DescriptorCount() == descriptors);
        assert([NSFileManager.defaultManager removeItemAtURL:directory error:NULL]);
        unsetenv("CBC_TEST_PROBE_MODE");
        puts("PASS: descriptor transfer; wrong build/path/length rejected; failed, oversized and timed-out children cleaned up; launch failure cleaned up");
    }
    return 0;
}
