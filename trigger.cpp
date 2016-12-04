#include "trigger.h"

extern "C" {
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fshook/protocol.h"
}
#include <cstdlib>
#include <cinttypes>
#include <sstream>
#include <iostream>

#define MIN(x,y) (x < y ? x : y)


#define PROTOCOL_HELLO "PROTOCOL10: HELLO, I AM: "
#define LD_PRELOAD_PATH "./fs_override.so"



Trigger::Trigger(FileRequestCb *cb) {
    m_cb = cb;
    m_child_idx = 0;
    for (uint32_t i = 0; i < ARRAY_LEN(m_threads); i++) {
        m_threads[i].in_use = false;
        m_threads[i].running = false;
    }
    m_free_threads = ARRAY_LEN(m_threads);
    m_threads_lock = false;
}

// static char putenv_buffers[1024][1024];
// static uint32_t putenv_pos = 0;

#define PUTENV(fmt, ...) do {                                           \
        char *result = putenv_buffers[putenv_pos];                      \
        ASSERT(putenv_pos < ARRAY_LEN(putenv_buffers));                 \
        putenv_pos++;                                                   \
        const int32_t buf_size = ARRAY_LEN(putenv_buffers[0]) - 1;      \
        if (buf_size <= snprintf(result, buf_size, fmt, ##__VA_ARGS__)) { \
            PANIC("string too long: %s", fmt);                          \
        }                                                               \
        ASSERT(0 == putenv(result));                                    \
    } while (0);


static int trigger_listen(const char *addr, struct sockaddr_un *out_addr) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    ASSERT(-1 != fd);
    *out_addr = (struct sockaddr_un){
        .sun_family = AF_UNIX,
        .sun_path = {0},
    };
    ASSERT(strlen(addr) < sizeof(out_addr->sun_path));
    strcpy(out_addr->sun_path, addr);

    LOG("Binding to: %s", out_addr->sun_path);
    ASSERT(0 == bind(fd,
                     (struct sockaddr *) out_addr,
                     sizeof(struct sockaddr_un)));
    ASSERT(0 == listen(fd, 5));
    return fd;
}

static bool send_go(int connection_fd) {
    LOG("GO");
    return (2 == send(connection_fd, "GO", 2, 0));
}

static bool checked_recv(int sockfd, void *buf, size_t len)
{
    char *pos = (char *)buf;
    uint32_t received_amount = 0;
    while (true) {
        int received = recv(sockfd, pos, len - received_amount, 0);
        if (received < 0) {
            perror("recv");
            LOG("recv returned: %d, errno: %d", received, errno);
            return false;
        }
        if (received == 0) {
            LOG("recv returned 0, socket closed?");
            return false;
        }
        pos += received;
        received_amount += (uint32_t)received;
        if (received_amount == len) {
            return true;
        }
        LOG("need moar bytes, have %u/%lu", received_amount, len);
    }
}

static bool recv_buf(int connection_fd, char *buf, uint32_t buf_size, uint32_t *out_received)
{
    uint32_t size_n;
    if (!checked_recv(connection_fd, &size_n, sizeof(size_n))) return false;
    const uint32_t size = htonl(size_n);
    *out_received = size;
    if (size == 0) return true;

    // LOG("recv size: " ,  size);
    ASSERT(buf_size > size);
    return checked_recv(connection_fd, buf, size);
}

static void debug_req(enum func func_id, bool delayed, uint32_t str_size)
{
    const char *name;
    switch (func_id) {
    case func_openr: name = "openr"; break;
    case func_openw: name = "openw"; break;
    case func_creat: name = "creat"; break;
    case func_stat: name = "stat"; break;
    case func_lstat: name = "lstat"; break;
    case func_opendir: name = "opendir"; break;
    case func_access: name = "access"; break;
    case func_truncate: name = "truncate"; break;
    case func_unlink: name = "unlink"; break;
    case func_rename: name = "rename"; break;
    case func_chmod: name = "chmod"; break;
    case func_readlink: name = "readlink"; break;
    case func_mknod: name = "mknod"; break;
    case func_mkdir: name = "mkdir"; break;
    case func_rmdir: name = "rmdir"; break;
    case func_symlink: name = "symlink"; break;
    case func_link: name = "link"; break;
    case func_chown: name = "chown"; break;
    case func_exec: name = "exec"; break;
    case func_execp: name = "execp"; break;
    case func_realpath: name = "realpath"; break;
    case func_trace: name ="trace"; break;
    default: PANIC("Invalid func_id: %u", func_id);
    }

    LOG("recv: delayed=%s, func=%s, size=%u",
        (delayed ? "yes" : "no"),  name, str_size);
    (void)name;
    (void)delayed;
    (void)str_size;
}

static bool wait_for(pid_t child, const char *cmd) {
    int wait_res;
    int wait_child = waitpid(child, &wait_res, WNOHANG);
    if ((wait_child < 0) && (errno == EINTR)) {
        return false;
    }
    ASSERT(wait_child >= 0);
    if (wait_child == 0) {
        return false;
    }
    ASSERT(wait_child == child);
    if (WEXITSTATUS(wait_res) != 0) {
        fprintf(stderr, "BUILD FAILED: Child process %d exited with status: %d\n", child, WEXITSTATUS(wait_res));
        fprintf(stderr, "BUILD FAILED: Failing command: %s\n", cmd);
        exit(1);
    }
    return true;
}

struct ConnectionParams {
    Trigger *trigger;
    int connection_fd;
    volatile bool started;
    const struct TargetContext *target_ctx;
};

static __thread uint64_t connections_accepted = 0;
static __thread uint64_t connections_completed = 0;

void *thread_start(void *arg)
{
    struct ConnectionParams *orig_params = (struct ConnectionParams *)arg;
    const struct ConnectionParams params = *orig_params;
    orig_params->started = true;
    LOG("Handling: %d",  params.connection_fd);
    params.trigger->handle_connection(params.connection_fd, params.target_ctx);
    close(params.connection_fd);
    LOG("Terminating");
    return NULL;
}

void Trigger::take_thread_lock()
{
    bool unlocked = false;
    while (!__atomic_compare_exchange_n(&m_threads_lock, &unlocked, true, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        usleep(10);
    }
    ASSERT(m_threads_lock == true);
}

void Trigger::release_thread_lock()
{
    bool unlocked = false;
    __atomic_store(&m_threads_lock, &unlocked, __ATOMIC_RELAXED);
}

void Trigger::harvest_threads()
{
    if (m_free_threads > 0) return;
    this->take_thread_lock();
    for (uint32_t i = 0; i < ARRAY_LEN(m_threads); i++) {
        if (!m_threads[i].in_use) continue;
        if (!m_threads[i].running) continue;
        void *res;
        if (0 == pthread_tryjoin_np(m_threads[i].thread_id, &res)) {
            m_threads[i].running = false;
            m_threads[i].in_use = false;
            m_free_threads++;
            ASSERT(m_free_threads < ARRAY_LEN(m_threads));
            connections_completed++;
            LOG("connections_accepted: %lu, connections_completed: %lu", connections_accepted, connections_completed);
        }
    }
    this->release_thread_lock();
}

struct Trigger::Thread *Trigger::alloc_thread()
{
    while (true) {
        this->take_thread_lock();
        for (uint32_t i = 0; i < ARRAY_LEN(m_threads); i++) {
            if (!m_threads[i].in_use) {
                ASSERT(m_free_threads > 0);
                m_free_threads--;
                m_threads[i].in_use = true;
                m_threads_lock = false;
                return &m_threads[i];
            }
            if (!m_threads[i].running) continue; /* started but not yet running, can't use it */
            void *res;
            if (0 == pthread_tryjoin_np(m_threads[i].thread_id, &res)) {
                m_threads[i].running = false;
                m_threads_lock = false;
                return &m_threads[i];
            }
        }
        this->release_thread_lock();
        usleep(10);
    }
}

bool Trigger::trigger_accept(int fd, const struct sockaddr_un *addr, const struct TargetContext *target_ctx)
{
    socklen_t addrlen = sizeof(struct sockaddr_un);
    int connection_fd;
    this->harvest_threads();
    // if (connections_accepted > 0 && (connections_completed == connections_accepted)) {
    //     LOG("All connections closed");
    //     return false;
    // }
    connection_fd = accept(fd, (struct sockaddr *)addr, &addrlen);
    if (connection_fd < 0) {
        ASSERT(errno == EAGAIN || errno == EWOULDBLOCK);
        return false;
    }
    ASSERT(connection_fd >= 0);
    // HANDLE CONNECTION
    connections_accepted++;
    LOG("Got connection, connections_accepted: %lu", connections_accepted);

    Thread *const thread = this->alloc_thread();
    ASSERT(thread != nullptr);
    ASSERT(thread->running == false);
    thread->trigger = this;
    struct ConnectionParams params = {
        .trigger = this,
        .connection_fd = connection_fd,
        .started = false,
        .target_ctx = target_ctx,
    };
    ASSERT(0 == pthread_attr_init(&thread->attr));
    ASSERT(0 == pthread_create(&thread->thread_id, &thread->attr,
                               &thread_start, &params));
    ASSERT(0 == pthread_attr_destroy(&thread->attr));
    LOG("Waiting for handler thread: %lX", thread->thread_id);
    while (!params.started) {
        usleep(10);
    }
    thread->running = true;
    LOG("Done waiting for handler thread: %lX", thread->thread_id);
    return true;
}

struct OperationPaths {
    const char *input_paths[2];
    uint32_t input_count;
    const char *output_paths[2];
    uint32_t output_count;
};

void safer_dirname(const char *path, char *dirname, uint32_t dirname_max_size)
{
    const uint32_t len = strlen(path);

    for (uint32_t i = len; i > 0; i--) {
        if (path[i - 1] == '/') {
            ASSERT(i < dirname_max_size);
            memcpy(dirname, path, i - 1);
            dirname[i] = '\0';
            return;
        }
    }
    dirname[0] = '\0';
}

static void get_input_paths(enum func func_id, const char *buf, uint32_t buf_size,
                            struct OperationPaths *out_paths)
{
    LOG("func_id: %X", func_id);
    out_paths->input_count = 0;
    out_paths->output_count = 0;
    switch (func_id) {
    case func_openr: {
        DEFINE_DATA(struct func_openr, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_stat: {
        DEFINE_DATA(struct func_stat, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_lstat: {
        DEFINE_DATA(struct func_lstat, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_opendir: {
        DEFINE_DATA(struct func_opendir, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_access: {
        DEFINE_DATA(struct func_access, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_readlink: {
        DEFINE_DATA(struct func_readlink, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_symlink: {
        DEFINE_DATA(struct func_symlink, buf, buf_size, data);
        out_paths->input_paths[0] = data->linkpath.out_path; // TODO - out
        out_paths->input_count = 1;
        break;
    }
    case func_exec: {
        DEFINE_DATA(struct func_exec, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_realpath: {
        DEFINE_DATA(struct func_realpath, buf, buf_size, data);
        out_paths->input_paths[0] = data->path.in_path;
        out_paths->input_count = 1;
        break;
    }
    case func_execp: {
        DEFINE_DATA(struct func_execp, buf, buf_size, data);
        out_paths->input_paths[0] = data->file;
        out_paths->input_count = 1;
        break;
    }
    // TODO: For all outputs, return an input which is the directory which contains the output.
    case func_openw: {
        DEFINE_DATA(struct func_openw, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_creat: {
        DEFINE_DATA(struct func_creat, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_truncate: {
        DEFINE_DATA(struct func_truncate, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_unlink: {
        DEFINE_DATA(struct func_unlink, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_chmod: {
        DEFINE_DATA(struct func_chmod, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_mknod: {
        DEFINE_DATA(struct func_mknod, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_mkdir: {
        DEFINE_DATA(struct func_mkdir, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_rmdir: {
        DEFINE_DATA(struct func_rmdir, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_chown: {
        DEFINE_DATA(struct func_chown, buf, buf_size, data);
        out_paths->output_paths[0] = data->path.out_path;
        out_paths->output_count = 1;
        break;
    }
    case func_rename: {
        DEFINE_DATA(struct func_rename, buf, buf_size, data);
        out_paths->output_paths[0] = data->oldpath.out_path;
        out_paths->output_paths[1] = data->newpath.out_path;
        out_paths->output_count = 2;
        break;
    }
    case func_link: {
        DEFINE_DATA(struct func_link, buf, buf_size, data);
        out_paths->output_paths[0] = data->oldpath.out_path;
        out_paths->output_paths[1] = data->newpath.out_path;
        out_paths->output_count = 2;
        break;
    }
    case func_trace: {
        DEFINE_DATA(struct func_trace, buf, buf_size, data);
        LOG("TRACE: %s", data->msg);
        break;
    }
    default: PANIC("Unknown command: %d", func_id);
    }
}

RequestedFileStatus Trigger::get_status(const char *input_path)
{
    m_map_mutex.lock();
    auto it = m_fileStatus.find(input_path);
    const RequestedFileStatus status
        = (it == m_fileStatus.end())
        ? REQUESTED_FILE_STATUS_UNKNOWN
        : it->second;
    m_map_mutex.unlock();
    return status;
}

void Trigger::mark_pending(const char *file_path)
{
    m_map_mutex.lock();
    auto it = m_fileStatus.find(file_path);
    if (it == m_fileStatus.end()) {
        m_fileStatus[file_path] = REQUESTED_FILE_STATUS_PENDING;
    } else {
        const RequestedFileStatus status = it->second;
        switch (status) {
        case REQUESTED_FILE_STATUS_READY: PANIC("Can't mark pending, already ready!");
        case REQUESTED_FILE_STATUS_UNKNOWN:
            m_fileStatus[file_path] = REQUESTED_FILE_STATUS_PENDING;
            break;
        case REQUESTED_FILE_STATUS_PENDING:
            break;
        default: PANIC("enum");
        }
    }
    m_map_mutex.unlock();
}

void Trigger::mark_ready(const char *file_path)
{
    m_map_mutex.lock();
    auto it = m_fileStatus.find(file_path);
    LOG("marking ready: %s", file_path);
    ASSERT(it != m_fileStatus.end());
    ASSERT(it->second == REQUESTED_FILE_STATUS_PENDING || it->second == REQUESTED_FILE_STATUS_READY);
    m_fileStatus[file_path] = REQUESTED_FILE_STATUS_READY;
    m_map_mutex.unlock();
}

void Trigger::want(const char *input_path, const struct TargetContext *target_ctx)
{
    const struct TargetContext new_target_ctx = {
        .path = input_path,
        .parent = target_ctx,
    };
    LOG("WANT: %s", input_path);
    const struct TargetContext *cur = target_ctx;
    while (cur) {
        if (0 == strcmp(cur->path, input_path)) {
            /* in child of parent building this path, just release it */
            return;
        }
        cur = cur->parent;
    }
    switch (this->get_status(input_path)) {
    case REQUESTED_FILE_STATUS_READY: return;
    case REQUESTED_FILE_STATUS_UNKNOWN:
        this->mark_pending(input_path);
        m_cb(input_path, &new_target_ctx);
        this->mark_ready(input_path);
        break;
    case REQUESTED_FILE_STATUS_PENDING: {
        LOG("Waiting for: '%s'...", input_path);
        while (true) {
            m_map_mutex.lock();
            auto it2 = m_fileStatus.find(input_path);
            ASSERT(it2 != m_fileStatus.end());
            const RequestedFileStatus status = it2->second;
            m_map_mutex.unlock();
            if (status == REQUESTED_FILE_STATUS_READY) {
                break;
            }
            usleep(10);
        }
        break;
    }
    }
}

void Trigger::handle_connection(int connection_fd, const struct TargetContext *target_ctx)
{
    char buf[0x8000];
    uint32_t size;
    if (!recv_buf(connection_fd, buf, sizeof(buf), &size)) return;
    if (0 != strncmp(PROTOCOL_HELLO, buf, MIN(size, strlen(PROTOCOL_HELLO)))) {
        PANIC("Exepcting HELLO message, got: %s", buf);
    }
    if (!send_go(connection_fd)) return;

    while (true) {
        if (!recv_buf(connection_fd, buf, sizeof(buf), &size)) break;
        const char *pos = buf;
        const bool delayed = *(const bool*)pos;
        pos += sizeof(delayed);
        const enum func func_id = *(enum func *)pos;
        pos += sizeof(func_id);
        const uint32_t str_size = size - (pos - buf);
        debug_req(func_id, delayed, str_size);

        struct OperationPaths paths;
        get_input_paths(func_id, pos, str_size, &paths);
        // LOG(input_path);
        if (!delayed) continue;
        for (uint32_t i = 0; i < paths.input_count; i++) {
            this->want(paths.input_paths[i], target_ctx);
        }
        for (uint32_t i = 0; i < paths.output_count; i++) {
            char output_path[0x1000];
            LOG("OUTPUT: %s", paths.output_paths[i]);
            safer_dirname(paths.output_paths[i], output_path, ARRAY_LEN(output_path));
            struct stat output_dir_stat;
            if (0 != stat(output_path, &output_dir_stat)) {
                ASSERT(ENOENT == errno);
                this->want(output_path, target_ctx);
            }
        }
        if (!send_go(connection_fd)) break;
    }
}

void Trigger::Execute(const char *cmd, const struct TargetContext *target_ctx)//, char *const argv[])
{
    uint64_t child_idx = m_child_idx;
    m_child_idx++;

    std::ostringstream stringStream;
    stringStream << "/tmp/trigger.sock." << getpid() << "." << child_idx;
    const std::string sockAddr = stringStream.str();


    LOG("Forking child: %s", cmd);
    std::cerr << "Build: '" << target_ctx->path << "'" << std::endl;

    int parent_child_pipe[2];
    ASSERT(0 == pipe(parent_child_pipe));
    const pid_t child = fork();
    char *const cwd = get_current_dir_name();

    auto ld_preload_full = std::string(cwd) + std::string("/") + std::string(LD_PRELOAD_PATH);
    auto path                           =    std::string("PATH=") + std::string(getenv("PATH"));
    auto ld_preload                     =    std::string("LD_PRELOAD=") + ld_preload_full;
    auto buildsome_master_unix_sockaddr =    std::string("BUILDSOME_MASTER_UNIX_SOCKADDR=") + std::string(sockAddr);
    auto buildsome_job_id               =    std::string("BUILDSOME_JOB_ID=") + std::to_string(child_idx);
    auto buildsome_root_filter          =    std::string("BUILDSOME_ROOT_FILTER=") + std::string(cwd);
//  , ("DYLD_FORCE_FLAT_NAMESPACE", "1")
    auto dyld_insert_libraries          =    std::string("DYLD_INSERT_LIBRARIES=") + ld_preload_full;

    const char *envir[] = {
        path.c_str(),
        ld_preload.c_str(),
        buildsome_master_unix_sockaddr.c_str(),
        buildsome_job_id.c_str(),
        buildsome_root_filter.c_str(),
        dyld_insert_libraries.c_str(),
        "DYLD_FORCE_FLAT_NAMESPACE=1",
        "PYTHONDONTWRITEBYTECODE=1",
    };
    free(cwd);
    if (0 == child) {
        close(parent_child_pipe[1]);
        LOG("Waiting for parent...");

        bool yup = false;
        ASSERT(sizeof(yup) == read(parent_child_pipe[0], &yup, sizeof(yup)));
        ASSERT(yup);
        LOG("Starting...");

        close(parent_child_pipe[0]);
        const char *const args[] = { SHELL_EXE_PATH, "-ec", cmd, NULL };
        execvpe("/bin/sh", (char *const*)args, (char *const*)envir);
        PANIC("exec failed?!");
    }

    close(parent_child_pipe[0]);
    LOG("Forked child: %d", child);

    struct sockaddr_un addr;
    int sock_fd = trigger_listen(sockAddr.c_str(), &addr);

    const bool parent_yup = true;
    ASSERT(sizeof(parent_yup) == write(parent_child_pipe[1], &parent_yup, sizeof(parent_yup)));

    LOG("Sent yup");

    while (!wait_for(child, cmd)) {
        this->trigger_accept(sock_fd, &addr, target_ctx);
        usleep(10);
    }
    LOG("Done accepting, waiting for child: %d", child);
    LOG("Child %d terminated", child);
    close(sock_fd);

    std::cerr << "Build: '" << target_ctx->path << "' - Done" << std::endl;
}
