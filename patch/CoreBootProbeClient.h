#ifndef CORE_BOOT_PROBE_CLIENT_H
#define CORE_BOOT_PROBE_CLIENT_H
#import <Foundation/Foundation.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CoreBootPolicy.h"
#ifndef CBC_PROBE_TIMEOUT
#define CBC_PROBE_TIMEOUT 20.0
#endif
extern char **environ;

static double ProbeClock(void) {
    struct timespec value; clock_gettime(CLOCK_MONOTONIC, &value);
    return value.tv_sec + value.tv_nsec / 1e9;
}

static NSError *ProbeError(NSString *message) {
    return [NSError errorWithDomain:@"CoreBootProbe" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
}

// Move descriptors away from stdin/stdout/stderr and the output slot (3).
static int ProbeDescriptor(int descriptor) {
    int moved = fcntl(descriptor, F_DUPFD_CLOEXEC, 10);
    int error = errno; close(descriptor); errno = error;
    return moved;
}

static NSDictionary *RunBootProbe(NSString *helper, NSString *product, NSString *build,
                                  NSString *board, NSURL *directory, NSError **error) {
    NSError *failure = nil; NSDictionary *result = nil;
    int destination = -1, channel[2] = {-1, -1}; pid_t child = -1;
    BOOL childReaped = NO; int status = 0;
    char temporary[PATH_MAX] = {0};
    posix_spawn_file_actions_t actions; BOOL actionsInitialized = NO;
    do {
        if (!helper.length || !product.length || !build.length || !board.length || !directory.isFileURL) {
            failure = ProbeError(@"Probe arguments unavailable"); break;
        }
        if (![NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES
            attributes:@{NSFilePosixPermissions:@0700} error:&failure]) break;
        int count = snprintf(temporary, sizeof(temporary), "%s/.kernelcache-XXXXXX", directory.fileSystemRepresentation);
        if (count < 0 || (size_t)count >= sizeof(temporary)) { failure = ProbeError(@"Temporary cache path too long"); break; }
        destination = mkstemp(temporary);
        if (destination < 0) { failure = ProbeError([NSString stringWithFormat:@"Temporary cache open failed: %s", strerror(errno)]); break; }
        destination = ProbeDescriptor(destination);
        if (destination < 0) { failure = ProbeError(@"Temporary cache descriptor allocation failed"); break; }
        if (pipe(channel)) { failure = ProbeError(@"Probe output pipe failed"); break; }
        channel[0] = ProbeDescriptor(channel[0]);
        channel[1] = ProbeDescriptor(channel[1]);
        if (channel[0] < 0 || channel[1] < 0) { failure = ProbeError(@"Probe pipe descriptor allocation failed"); break; }
        int spawnError = posix_spawn_file_actions_init(&actions);
        if (spawnError) { failure = ProbeError(@"Probe spawn actions initialization failed"); break; }
        actionsInitialized = YES;
        spawnError = posix_spawn_file_actions_adddup2(&actions, destination, 3);
        if (!spawnError) spawnError = posix_spawn_file_actions_adddup2(&actions, channel[1], STDOUT_FILENO);
        if (!spawnError) spawnError = posix_spawn_file_actions_adddup2(&actions, channel[1], STDERR_FILENO);
        if (!spawnError) spawnError = posix_spawn_file_actions_addclose(&actions, channel[0]);
        if (!spawnError) spawnError = posix_spawn_file_actions_addclose(&actions, channel[1]);
        if (!spawnError) spawnError = posix_spawn_file_actions_addclose(&actions, destination);
        if (spawnError) { failure = ProbeError(@"Probe descriptor actions failed"); break; }
        char *arguments[] = {(char *)helper.fileSystemRepresentation, (char *)product.UTF8String,
            (char *)build.UTF8String, (char *)board.UTF8String, NULL};
        spawnError = posix_spawn(&child, helper.fileSystemRepresentation, &actions, NULL, arguments, environ);
        if (spawnError) {
            failure = ProbeError([NSString stringWithFormat:@"Probe launch failed (%d): %s", spawnError, strerror(spawnError)]);
            break;
        }
        close(channel[1]); channel[1] = -1;
        NSMutableData *output = [NSMutableData new]; BOOL finished = NO;
        double deadline = ProbeClock() + CBC_PROBE_TIMEOUT;
        while (ProbeClock() < deadline) {
            struct pollfd pending = {.fd = channel[0], .events = POLLIN | POLLHUP};
            int ready = poll(&pending, 1, 100);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (pending.revents & (POLLERR | POLLNVAL))) { failure = ProbeError(@"Probe output read failed"); break; }
            if (ready > 0) {
                char bytes[1024]; ssize_t count = read(channel[0], bytes, sizeof(bytes));
                if (count < 0 && errno == EINTR) continue;
                if (count < 0) { failure = ProbeError(@"Probe output read failed"); break; }
                if (count == 0) { finished = YES; break; }
                if (output.length + (NSUInteger)count > 8192) { failure = ProbeError(@"Probe response exceeds size limit"); break; }
                [output appendBytes:bytes length:(NSUInteger)count];
            }
        }
        if (!finished && !failure) failure = ProbeError([NSString stringWithFormat:
            @"Probe timed out after %.1f seconds", (double)CBC_PROBE_TIMEOUT]);
        while (!failure && ProbeClock() < deadline) {
            pid_t reaped = waitpid(child, &status, WNOHANG);
            if (reaped == child) { childReaped = YES; break; }
            if (reaped < 0 && errno != EINTR) {
                if (errno == ECHILD) childReaped = YES;
                failure = ProbeError(@"Probe exit status unavailable"); break;
            }
            // A bounded poll permits the just-closed child to finish without a busy loop.
            poll(NULL, 0, 20);
        }
        if (!failure && !childReaped) failure = ProbeError(@"Probe exit timed out");
        if (failure) break;
        id decoded = [NSJSONSerialization JSONObjectWithData:output options:0 error:NULL];
        NSDictionary *report = [decoded isKindOfClass:NSDictionary.class] ? decoded : nil;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || ![report[@"success"] isEqual:@YES]) {
            NSString *detail = report ? [NSString stringWithFormat:@"stage=%@ errno=%@", report[@"stage"], report[@"errno"]] :
                ([[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding] ?: @"no readable output");
            failure = ProbeError([NSString stringWithFormat:@"Probe failed (status=%d): %@", status,
                [detail substringToIndex:MIN(detail.length, 1024)]]); break;
        }
        NSString *hash = report[@"boot_hash"], *path = report[@"source_path"];
        NSString *actualProduct = report[@"product"], *actualBuild = report[@"build"], *actualBoard = report[@"board"];
        NSNumber *size = report[@"bytes"];
        if (![report[@"protocol"] isEqual:@1] || ![hash isKindOfClass:NSString.class] ||
            ![path isKindOfClass:NSString.class] || ![size isKindOfClass:NSNumber.class] ||
            ![actualProduct isKindOfClass:NSString.class] || ![actualBuild isKindOfClass:NSString.class] ||
            ![actualBoard isKindOfClass:NSString.class] ||
            !CBCIdentityMatches(product.UTF8String, build.UTF8String, board.UTF8String,
                actualProduct.UTF8String, actualBuild.UTF8String, actualBoard.UTF8String) ||
            !CBCPathMatchesBoot(path.UTF8String, hash.UTF8String)) {
            failure = ProbeError(@"Probe response has a mismatched identity or current-boot path"); break;
        }
        struct stat file;
        if (fstat(destination, &file) || !S_ISREG(file.st_mode) || file.st_size < 4 ||
            file.st_size > 0x20000000 || file.st_size != size.longLongValue) {
            failure = ProbeError(@"Probe cache length does not match its response"); break;
        }
        result = @{@"temporary_path":@(temporary), @"source_path":path, @"boot_hash":hash, @"bytes":size};
    } while (0);
    if (child > 0 && !childReaped) {
        kill(child, SIGKILL); // Only the child created by this invocation.
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    if (actionsInitialized) posix_spawn_file_actions_destroy(&actions);
    if (destination >= 0) close(destination);
    for (int index = 0; index < 2; ++index) if (channel[index] >= 0) close(channel[index]);
    if (!result && temporary[0]) unlink(temporary);
    if (error) *error = failure;
    return result;
}
#endif
