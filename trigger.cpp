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
}

#define PUTENV(fmt, ...) do {                                           \
        char *result = (char *)malloc(1024 * sizeof(char));             \
        if (1023 <= snprintf(result, 1023, fmt, ##__VA_ARGS__)) {       \
            PANIC("string too long: %s", fmt);                          \
        }                                                               \
        ASSERT(0 == putenv(result));                                    \
    } while (0);


static int trigger_listen(const char *addr, struct sockaddr_un *out_addr) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT(-1 != fd);
    *out_addr = (struct sockaddr_un){
        .sun_family = AF_UNIX,
        .sun_path = {0},
    };
    ASSERT(strlen(addr) < sizeof(out_addr->sun_path));
    strcpy(out_addr->sun_path, addr);

    std::cout << "binding to: " << out_addr->sun_path << std::endl;
    ASSERT(0 == bind(fd,
                     (struct sockaddr *) out_addr,
                     sizeof(struct sockaddr_un)));
    ASSERT(0 == listen(fd, 5));
    return fd;
}

static void send_go(int connection_fd) {
    send(connection_fd, "GO", 2, 0);
}

static bool recv_buf(int connection_fd, char *buf, uint32_t buf_size, uint32_t *out_received)
{
    uint32_t size_n;
    int received = recv(connection_fd, &size_n, sizeof(size_n), 0);
    if (received <= 0) return false;
    if ((uint32_t)received < sizeof(size_n)) return false;
    const uint32_t size = htonl(size_n);
    *out_received = size;
    if (size == 0) return true;

    // std::cout << "recv size: " << size << std::endl;
    ASSERT(buf_size > size);
    received = recv(connection_fd, buf, size, 0);
    if (received < 0) return false;
    if ((uint32_t)received != size) return false;
    return true;
}

static void debug_req(enum func func_id, bool delayed, const char *pos, uint32_t str_size)
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

    std::cout
        << "recv: delayed=" << (delayed ? "yes" : "no")
        << ", func=" << name
        << ", buf(" << str_size << ")=" << pos << std::endl;

}

static void trigger_accept(int fd, const struct sockaddr_un *addr, FileRequestCb *cb) {
    int connection_fd;
    socklen_t addrlen;
    while ((connection_fd = accept(fd,
                                   (struct sockaddr *)addr,
                                   &addrlen)) > -1)
    {
        // HANDLE CONNECTION
        std::cout << getpid() << ": got connection" << std::endl;

        bool first = true;
        while (true) {

            char buf[4096];
            uint32_t size;
            if (!recv_buf(connection_fd, buf, sizeof(buf), &size)) break;

            if (first) {
                if (0 != strncmp(PROTOCOL_HELLO, buf, MIN(size, strlen(PROTOCOL_HELLO)))) {
                    PANIC("Exepcting HELLO message, got: %s", buf);
                }
                first = false;
            } else {
                char *pos = buf;
                bool delayed = 0 != *pos;
                pos += 1;
                enum func func_id = *(enum func *)pos;
                pos += sizeof(uint32_t);
                const uint32_t str_size = size - 1 - sizeof(uint32_t);
                debug_req(func_id, delayed, pos, str_size);

                if (delayed) {
                    cb(func_id, pos, str_size);
                }
            }
            send_go(connection_fd);
        }

        close(connection_fd);
        return;
    }
}

static int wait_all(pid_t child) {
    bool found = false;
    int res = 1;
    while (true) {
        int wait_res;
        int wait_child = wait(&wait_res);
        if ((wait_child == -1) && (errno == ECHILD)) {
            ASSERT(found);
            return res;
        } else if (wait_child == child) {
            found = true;
            res = wait_res;
        }
    }
    PANIC("wat");
}

int Trigger::Execute(const char *filename, char *const argv[])
{
    uint64_t child_idx = m_child_idx;
    m_child_idx++;

    std::ostringstream stringStream;
    stringStream << "/tmp/trigger.sock." << getpid() << "." << child_idx;
    const std::string sockAddr = stringStream.str();

    struct sockaddr_un addr;
    int sock_fd = trigger_listen(sockAddr.c_str(), &addr);

    const pid_t child = fork();
    if (0 == child) {
        PUTENV("LD_PRELOAD=%s", LD_PRELOAD_PATH);
        PUTENV("BUILDSOME_MASTER_UNIX_SOCKADDR=%s", sockAddr.c_str());
        PUTENV("BUILDSOME_JOB_ID=%lu", child_idx);
        char *cwd = get_current_dir_name();
        PUTENV("BUILDSOME_ROOT_FILTER=%s", cwd);
        free(cwd);
        execvp(filename, argv);
        PANIC("exec failed?!");
    }

    std::cout << "forked child: " << child << std::endl;

    trigger_accept(sock_fd, &addr, m_cb);

    return wait_all(child);
}
