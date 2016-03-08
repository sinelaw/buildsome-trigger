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
}
#include <cstdlib>
#include <cinttypes>
#include <sstream>
#include <iostream>

#define LD_PRELOAD_PATH "./fs_override.so"

#define ASSERT(x)  do { if (!(x)) { PANIC("ASSERTION FAILED at %s:%d: " #x, __FILE__, __LINE__); } } while(0)
#define PANIC(fmt, ...) do {                                            \
        perror("FATAL ERROR: errno");                                   \
        fprintf(stderr, "FATAL ERROR: " fmt "\n", ##__VA_ARGS__);       \
        abort();                                                        \
    } while (0)

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

static void trigger_accept(int fd, const struct sockaddr_un *addr) {
    int connection_fd;
    socklen_t addrlen;
    while ((connection_fd = accept(fd,
                                   (struct sockaddr *)addr,
                                   &addrlen)) > -1)
    {
        // HANDLE CONNECTION
        std::cout << getpid() << ": got connection" << std::endl;

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

    if (fork() == 0) {
        trigger_accept(sock_fd, &addr);
    }

    return wait_all(child);
}
