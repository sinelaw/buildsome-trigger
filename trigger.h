#pragma once

#include <cinttypes>
#include <string>
#include <map>

/* Request for a file to be built. After the callback returns, file
 * access functions will be allowed to execute.
 *
 * Executed at most once per filename. */
typedef void FileRequestCb(const char *filename);

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
    int Execute(const char *filename, char *const argv[]);

private:
    std::map<FilePath, RequestedFileStatus> m_fileStatus;
    FileRequestCb *m_cb;
    uint64_t m_child_idx;
};
