#define CORE_REMOTE_TRACE_TESTING 1
#include "../patch/CoreRemoteTrace.c"
#include <assert.h>
#include <sys/stat.h>

static mach_msg_return_t Receive(mach_port_t port, int traced, int *error, mach_msg_header_t *output) {
    union { mach_msg_header_t header; char storage[512]; } message = {0};
    errno = 735;
    mach_msg_return_t result = traced
        ? cbr_msg_(&message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(message), port, 1, MACH_PORT_NULL)
        : mach_msg(&message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(message), port, 1, MACH_PORT_NULL);
    *error = errno;
    if (output) *output = message.header;
    return result;
}

static void Send(mach_port_t port, int traced) {
    mach_msg_header_t message = {0};
    message.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    message.msgh_size = sizeof(message);
    message.msgh_remote_port = port;
    message.msgh_id = 4321;
    mach_msg_return_t result = traced
        ? cbr_msg_(&message, MACH_SEND_MSG, sizeof(message), 0, 0, 0, 0)
        : mach_msg(&message, MACH_SEND_MSG, sizeof(message), 0, 0, 0, 0);
    assert(result == MACH_MSG_SUCCESS);
}

static void *ConcurrentWriter(void *unused) {
    (void)unused;
    char payload[900];
    memset(payload, 'x', sizeof(payload) - 1);
    payload[sizeof(payload) - 1] = '\0';
    for (int i = 0; i < 800; ++i) {
        errno = 735;
        Record("concurrent %s", payload);
        assert(errno == 735);
    }
    return NULL;
}

int main(void) {
    char root[] = "/tmp/core-remote-test.XXXXXX";
    assert(mkdtemp(root));
    char data[PATH_MAX], path[PATH_MAX];
    assert(snprintf(data, sizeof(data), "%s/Containers", root) > 0); assert(!mkdir(data, 0700));
    assert(snprintf(data, sizeof(data), "%s/Containers/Data", root) > 0); assert(!mkdir(data, 0700));
    assert(snprintf(data, sizeof(data), "%s/Containers/Data/Application", root) > 0); assert(!mkdir(data, 0700));
    assert(snprintf(data, sizeof(data), "%s/Containers/Data/Application/test", root) > 0); assert(!mkdir(data, 0700));
    assert(!setenv("CFFIXED_USER_HOME", data, 1));
    assert(snprintf(path, sizeof(path), "%s/Documents", data) > 0); assert(!mkdir(path, 0700));
    assert(snprintf(path, sizeof(path), "%s/Documents/Core-remote-bootstrap.log", data) > 0);

    mach_port_t port;
    assert(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) == KERN_SUCCESS);
    assert(mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS);
    int before, after;
    CoreRemoteTraceTestScope(0);
    assert(Receive(port, 1, &after, NULL) == MACH_RCV_TIMED_OUT);
    assert(access(path, F_OK) != 0); // unrelated calls do not create a log

    CoreRemoteTraceTestScope(1);
    mach_msg_header_t expected = {0}, actual = {0};
    Send(port, 0); assert(Receive(port, 0, &before, &expected) == MACH_MSG_SUCCESS);
    Send(port, 1); assert(Receive(port, 1, &after, &actual) == MACH_MSG_SUCCESS);
    assert(before == after);
    assert(!memcmp(&expected, &actual, sizeof(actual))); // no header/result/ABI changes
    assert(Receive(port, 0, &before, NULL) == MACH_RCV_TIMED_OUT);
    assert(Receive(port, 1, &after, NULL) == MACH_RCV_TIMED_OUT);
    assert(before == after);
    mach_msg_return_t bad = Receive(MACH_PORT_NULL, 0, &before, NULL);
    assert(bad == MACH_RCV_INVALID_NAME);
    assert(Receive(MACH_PORT_NULL, 1, &after, NULL) == bad && before == after);

    errno = 735;
    kern_return_t control = thread_set_exception_ports(MACH_PORT_DEAD, 0, 0, EXCEPTION_DEFAULT, 0);
    before = errno;
    errno = 735;
    assert(cbr_set_exception_ports___(MACH_PORT_DEAD, 0, 0, EXCEPTION_DEFAULT, 0) == control);
    assert(errno == before);
    errno = 735; control = thread_resume(MACH_PORT_DEAD); before = errno;
    errno = 735; assert(cbr_th_resume(MACH_PORT_DEAD) == control && errno == before);

    char output[16384] = {0};
    int fd = open(path, O_RDONLY); assert(fd >= 0);
    ssize_t count = read(fd, output, sizeof(output) - 1); assert(count > 0); close(fd);
    assert(strstr(output, "reason=success") && strstr(output, "reason=receive-timed-out"));
    assert(strstr(output, "reason=receive-invalid-name") && strstr(output, "message_id=4321"));
    assert(strstr(output, "thread_set_exception_ports") && strstr(output, "thread_resume"));

    // An old oversized log retains its last 2 MiB, including the newest tail.
    char backup[PATH_MAX];
    assert(snprintf(backup, sizeof(backup), "%s.1", path) > 0);
    const char tail[] = "retained-tail\n";
    off_t oversized = CORE_REMOTE_LOG_LIMIT + 777;
    fd = open(path, O_RDWR); assert(fd >= 0);
    assert(!ftruncate(fd, oversized));
    assert(pwrite(fd, tail, sizeof(tail) - 1, oversized - (sizeof(tail) - 1)) == sizeof(tail) - 1);
    close(fd);
    errno = 735; Record("rotation marker"); assert(errno == 735);
    struct stat status;
    assert(!stat(path, &status) && status.st_size > 0 && status.st_size <= CORE_REMOTE_LOG_LIMIT);
    assert(!stat(backup, &status) && status.st_size == CORE_REMOTE_LOG_LIMIT);
    assert(S_ISREG(status.st_mode) && (status.st_mode & 0777) == 0600);
    fd = open(backup, O_RDONLY); assert(fd >= 0);
    char retained[sizeof(tail)] = {0};
    assert(pread(fd, retained, sizeof(tail) - 1, status.st_size - (sizeof(tail) - 1)) == sizeof(tail) - 1);
    close(fd); assert(!strcmp(retained, tail));
    fd = open(path, O_RDONLY); assert(fd >= 0);
    memset(output, 0, sizeof(output));
    count = read(fd, output, sizeof(output) - 1); assert(count > 0); close(fd);
    assert(strstr(output, "patch=16") && strstr(output, "rotation marker"));

    // Concurrent writes cannot exceed either file's bound or interleave records.
    pthread_t writers[4];
    for (int i = 0; i < 4; ++i) assert(!pthread_create(&writers[i], NULL, ConcurrentWriter, NULL));
    for (int i = 0; i < 4; ++i) assert(!pthread_join(writers[i], NULL));
    assert(!stat(path, &status) && status.st_size > 0 && status.st_size <= CORE_REMOTE_LOG_LIMIT);
    assert(!stat(backup, &status) && status.st_size <= CORE_REMOTE_LOG_LIMIT);
    FILE *active = fopen(path, "r"); assert(active);
    while (fgets(output, sizeof(output), active)) {
        assert(!strncmp(output, "patch=16 ", 9));
        assert(output[strlen(output) - 1] == '\n');
        assert(!strstr(output + 9, "patch=16 "));
    }
    assert(!ferror(active)); fclose(active);

    // Busy diagnostics drop records without blocking or changing Mach results.
    fd = open(path, O_RDWR); assert(fd >= 0); assert(!flock(fd, LOCK_EX | LOCK_NB));
    assert(!fstat(fd, &status)); off_t lockedSize = status.st_size;
    assert(Receive(port, 0, &before, NULL) == MACH_RCV_TIMED_OUT);
    assert(Receive(port, 1, &after, NULL) == MACH_RCV_TIMED_OUT && after == before);
    assert(!fstat(fd, &status) && status.st_size == lockedSize); close(fd);

    // Failed backup creation leaves existing diagnostics intact.
    assert(!unlink(backup)); assert(!symlink("/dev/null", backup));
    fd = open(path, O_RDWR); assert(fd >= 0); assert(!ftruncate(fd, oversized)); close(fd);
    assert(Receive(port, 0, &before, NULL) == MACH_RCV_TIMED_OUT);
    assert(Receive(port, 1, &after, NULL) == MACH_RCV_TIMED_OUT && after == before);
    assert(!stat(path, &status) && status.st_size == oversized);
    assert(!lstat(backup, &status) && S_ISLNK(status.st_mode)); assert(!unlink(backup));

    // A failed diagnostic write must not affect the native call or errno.
    assert(Receive(port, 0, &before, NULL) == MACH_RCV_TIMED_OUT);
    assert(!unlink(path)); assert(!symlink("/dev/null", path));
    assert(Receive(port, 1, &after, NULL) == MACH_RCV_TIMED_OUT && after == before);
    assert(!lstat(path, &status) && S_ISLNK(status.st_mode));
    assert(!unlink(path));
    assert(mach_port_deallocate(mach_task_self(), port) == KERN_SUCCESS);
    assert(mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1) == KERN_SUCCESS);
    assert(snprintf(path, sizeof(path), "%s/Documents", data) > 0); assert(!rmdir(path));
    assert(!rmdir(data));
    assert(snprintf(path, sizeof(path), "%s/Containers/Data/Application", root) > 0); assert(!rmdir(path));
    assert(snprintf(path, sizeof(path), "%s/Containers/Data", root) > 0); assert(!rmdir(path));
    assert(snprintf(path, sizeof(path), "%s/Containers", root) > 0); assert(!rmdir(path));
    assert(!rmdir(root));
    puts("PASS: native Mach delivery, headers, timeouts, errors and errno; bounded tail rotation, concurrent records, busy-lock and failed-log isolation");
    return 0;
}
