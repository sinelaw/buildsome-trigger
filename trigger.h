#pragma once

#include <cinttypes>
#include <string>
#include <map>
#include <mutex>

extern "C" {
#include "fshook/protocol.h"
}

#define AVOID_UNUSED(x) ((void)x)

#define SHELL_EXE_PATH "/usr/bin/bash"

static inline void panic(void) __attribute__((noreturn));
static inline void panic(void) {
    abort();
}

#define ASSERT(x)  do { if (!(x)) { PANIC("ASSERTION FAILED at %s:%d: " #x, __FILE__, __LINE__); } } while(0)
#define PANIC(fmt, ...) do {                                            \
        perror("FATAL ERROR: errno");                                   \
        fprintf(stderr, "FATAL ERROR: " fmt "\n", ##__VA_ARGS__);       \
        panic();                                                        \
    } while (0)

#define DEFINE_DATA(type, buf, buf_size, name)  \
    ASSERT(buf_size == sizeof(type));           \
    const type *name __attribute__((unused)) = (type *)buf;

#define ARRAY_LEN(x) (sizeof(x)/sizeof(x[0]))

static volatile bool log_lock = false;

/* #define DEBUG */

#ifdef DEBUG

#define LOG(fmt, ...) do {                                              \
        bool expected_false = false;                                    \
        while (!__atomic_compare_exchange_n(&log_lock, &expected_false, true, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) { \
            usleep(10);                                                 \
        }                                                               \
        fprintf(stderr, "%u:%lX: " fmt "\n", getpid(), pthread_self(), ##__VA_ARGS__); \
        log_lock = false;                                               \
    } while (0)

#else

#define LOG(...) do { } while (0)

#endif

void safer_dirname(const char *path, char *dirname, uint32_t dirname_max_size);

struct TargetContext {
    const char *path;
    const struct TargetContext *parent;
};


/* Request for a file to be built. After the callback returns, file
 * access functions will be allowed to execute.
 *
 * Executed at most once per filename. */
typedef void FileRequestCb(const char *input_path, const struct TargetContext *);

typedef std::string FilePath;

typedef enum RequestedFileStatus {
    REQUESTED_FILE_STATUS_UNKNOWN,
    REQUESTED_FILE_STATUS_PENDING,
    REQUESTED_FILE_STATUS_READY,
} RequestedFileStatus;

/* struct Command { */
/* public: */
/*     Command(const char *filename, char *const argv[], char *const envp[]); */

/* private: */
/*     const char *m_filename; */
/*     char *const m_argv[]; */
/*     char *const m_envp[]; */
/* }; */

class Trigger {
public:
    Trigger(FileRequestCb *cb);

    /* Executes the given command while hooking its file accesses. */
    void Execute(const char *cmd, const struct TargetContext *); //, char *const argv[]);

    RequestedFileStatus get_status(const char *file_path);
    void mark_pending(const char *file_path);
    void mark_ready(const char *file_path);
    void want(const char *file_path, const struct TargetContext *);
    void handle_connection(int connection_fd, const struct TargetContext *);
    bool trigger_accept(int fd, const struct sockaddr_un *addr, const struct TargetContext *);
private:

    void harvest_threads();
    void take_thread_lock();
    void release_thread_lock();

    std::map<FilePath, RequestedFileStatus> m_fileStatus;
    std::mutex m_map_mutex;

    FileRequestCb *m_cb;
    uint64_t m_child_idx;
    volatile bool m_threads_lock;

    struct Thread {
        pthread_t thread_id;
        pthread_attr_t attr;
        Trigger *trigger;
        bool in_use;
        bool running;
    };


    uint32_t m_free_threads;
    struct Thread m_threads[2048];

    struct Thread *alloc_thread();
};
