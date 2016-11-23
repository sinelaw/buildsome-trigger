#pragma once

#include <cinttypes>
#include <string>
#include <map>

extern "C" {
#include "fshook/protocol.h"
}

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

/* Request for a file to be built. After the callback returns, file
 * access functions will be allowed to execute.
 *
 * Executed at most once per filename. */
typedef void FileRequestCb(enum func func_id, const char *buf, uint32_t buf_size);

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
    void Execute(const char *cmd); //, char *const argv[]);

    void handle_connection(int connection_fd);
    bool trigger_accept(int fd, const struct sockaddr_un *addr);

private:

    std::map<FilePath, RequestedFileStatus> m_fileStatus;
    FileRequestCb *m_cb;
    uint64_t m_child_idx;

    struct Thread {
        pthread_t thread_id;
        pthread_attr_t attr;
        Trigger *trigger;
        bool in_use;
    };

    struct Thread m_threads[100];

    struct Thread *alloc_thread();
};
