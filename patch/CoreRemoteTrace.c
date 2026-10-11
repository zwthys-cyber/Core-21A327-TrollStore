// Observe Core's existing remote bootstrap calls. Never change their arguments,
// return values, timeout, exception state, or quarantine policy.
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

#ifdef CORE_REMOTE_TRACE_TESTING
static int TestScope;
void CoreRemoteTraceTestScope(int enabled) { TestScope = enabled; }
#endif

static uintptr_t CallerOffset(void *caller) {
#if __has_feature(ptrauth_calls)
    caller = ptrauth_strip(caller, ptrauth_key_return_address);
#endif
    uintptr_t address = (uintptr_t)caller;
    uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
    return address >= base ? address - base : UINTPTR_MAX;
}

static int InRemoteScope(uintptr_t offset) {
#ifdef CORE_REMOTE_TRACE_TESTING
    if (TestScope) return 1;
#endif
    return offset >= 0x38000 && offset < 0x3c000;
}

static uint64_t Milliseconds(void) {
    struct timespec t = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static void Record(const char *format, ...) {
    int saved = errno;
    const char *home = getenv("CFFIXED_USER_HOME");
    if (!home || !strstr(home, "/Containers/Data/Application/")) home = getenv("HOME");
    if (!home || !strstr(home, "/Containers/Data/Application/")) { errno = saved; return; }
    char path[PATH_MAX], line[1024];
    int n = snprintf(path, sizeof(path), "%s/Documents/Core-remote-bootstrap.log", home);
    if (n <= 0 || (size_t)n >= sizeof(path)) { errno = saved; return; }
    struct timespec now = {0};
    clock_gettime(CLOCK_REALTIME, &now);
    uint64_t thread = 0;
    pthread_threadid_np(NULL, &thread);
    n = snprintf(line, sizeof(line), "patch=12 time=%lld.%03ld pid=%d tid=%llu ",
        (long long)now.tv_sec, now.tv_nsec / 1000000, getpid(), (unsigned long long)thread);
    if (n <= 0 || (size_t)n >= sizeof(line) - 1) { errno = saved; return; }
    va_list args;
    va_start(args, format);
    int count = vsnprintf(line + n, sizeof(line) - (size_t)n - 1, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= sizeof(line) - (size_t)n - 1) { errno = saved; return; }
    size_t length = (size_t)n + (size_t)count;
    line[length++] = '\n';
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        ssize_t result;
        do { result = write(fd, line, length); } while (result < 0 && errno == EINTR);
        close(fd);
    }
    errno = saved;
}

static const char *MessageResult(mach_msg_return_t result) {
    switch (result) {
        case MACH_MSG_SUCCESS: return "success";
        case MACH_RCV_TIMED_OUT: return "receive-timed-out";
        case MACH_RCV_INVALID_NAME: return "receive-invalid-name";
        case MACH_RCV_PORT_DIED: return "receive-port-died";
        case MACH_RCV_TOO_LARGE: return "receive-too-large";
        case MACH_RCV_INTERRUPTED: return "receive-interrupted";
        case MACH_SEND_INVALID_DEST: return "send-invalid-destination";
        case MACH_SEND_TIMED_OUT: return "send-timed-out";
        default: return "other-error";
    }
}

void CoreRemoteTraceStartup(void) {
    Record("startup trace=enabled scope=Core-remote-bootstrap forwarding=original");
}

// Equal-length import aliases keep both Mach-O string pools and fixup indices
// in place. Only the main executable imports these names; this library imports
// the original libSystem functions, so forwarding cannot recurse into itself.
mach_msg_return_t cbr_msg_(mach_msg_header_t *message, mach_msg_option_t options,
    mach_msg_size_t sendSize, mach_msg_size_t receiveSize, mach_port_name_t receivePort,
    mach_msg_timeout_t timeout, mach_port_name_t notify) {
    int incoming = errno;
    uintptr_t offset = CallerOffset(__builtin_return_address(0));
    int observed = InRemoteScope(offset);
    uint64_t start = 0;
    if (observed) {
        Record("mach_msg begin caller=0x%lx options=0x%x send=%u receive=%u port=%u timeout_ms=%u notify=%u",
            (unsigned long)offset, options, sendSize, receiveSize, receivePort, timeout, notify);
        start = Milliseconds();
    }
    errno = incoming;
    mach_msg_return_t result = mach_msg(message, options, sendSize, receiveSize, receivePort, timeout, notify);
    int outgoing = errno;
    if (observed) {
        uint64_t finish = Milliseconds();
        mach_port_type_t type = 0;
        kern_return_t typeResult = KERN_INVALID_ARGUMENT;
        if (options & MACH_RCV_MSG) typeResult = mach_port_type(mach_task_self(), receivePort, &type);
        int received = result == MACH_MSG_SUCCESS && (options & MACH_RCV_MSG) && message;
        Record("mach_msg end caller=0x%lx result=0x%x reason=%s elapsed_ms=%llu port_type_result=%d port_type=0x%x message_id=%d message_size=%u",
            (unsigned long)offset, result, MessageResult(result),
            (unsigned long long)(finish >= start ? finish - start : 0), typeResult, type,
            received ? message->msgh_id : 0, received ? message->msgh_size : 0);
    }
    errno = outgoing;
    return result;
}

kern_return_t cbr_set_exception_ports___(thread_act_t thread, exception_mask_t mask,
    mach_port_t port, exception_behavior_t behavior, thread_state_flavor_t flavor) {
    int incoming = errno;
    uintptr_t offset = CallerOffset(__builtin_return_address(0));
    errno = incoming;
    kern_return_t result = thread_set_exception_ports(thread, mask, port, behavior, flavor);
    int outgoing = errno;
    if (InRemoteScope(offset)) Record("thread_set_exception_ports caller=0x%lx thread=%u mask=0x%x port=%u behavior=0x%x flavor=%d result=%d",
        (unsigned long)offset, thread, mask, port, behavior, flavor, result);
    errno = outgoing;
    return result;
}

kern_return_t cbr_th_resume(thread_act_t thread) {
    int incoming = errno;
    uintptr_t offset = CallerOffset(__builtin_return_address(0));
    errno = incoming;
    kern_return_t result = thread_resume(thread);
    int outgoing = errno;
    if (InRemoteScope(offset)) Record("thread_resume caller=0x%lx thread=%u result=%d",
        (unsigned long)offset, thread, result);
    errno = outgoing;
    return result;
}
