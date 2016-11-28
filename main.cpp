#include "trigger.h"

#include <cstdlib>
#include <iostream>

extern "C" {
#include <signal.h>
#include "fshook/protocol.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
}

static Trigger *trigger;
static int pipefd_to_child[2];
static int pipefd_to_parent[2];
#define WRITE(str)                                      \
    std::cerr << "WRITE: " << str << std::endl;         \
    write(pipefd_to_child[1], str, strlen(str));        \
    write(pipefd_to_child[1], "\n", strlen("\n"))

static void query(const char *const build_target)
{
    static volatile bool query_lock = false;
    while (query_lock) {
        usleep(100);
    }
    query_lock = true;

    WRITE(build_target);

#define MIN(x, y) ((x <= y) ? x : y)

    char user_input[1024];
    uint32_t pos = 0;
    while (true) {
        int read_res = read(pipefd_to_parent[0], &user_input[pos], 1);
        if (read_res < 1) {
            perror("read");
            exit(1);
        }
        if (user_input[pos] == '\n') {
            break;
        }
        // std::cerr << "read: " << user_input[pos] << std::endl;
        pos++;
    }


    user_input[pos] = '\0';
    std::cerr << "For query: '" << build_target << "', got: '" << user_input << "'" << std::endl;
    query_lock = false;
    // WRITE("Got: '" << user_input << "'");
    if (pos > 0) {
        trigger->Execute(user_input);
    }
}

void file_request(enum func func, const char *buf, uint32_t buf_size)
{
// std::cout << "HANDLING: " << buf << std::endl;
    switch (func) {
    case func_openw: {
        DEFINE_DATA(struct func_openw, buf, buf_size, data);
        return;
    }
    case func_creat: {
        DEFINE_DATA(struct func_creat, buf, buf_size, data);
        return;
    }
    case func_truncate: {
        DEFINE_DATA(struct func_truncate, buf, buf_size, data);
        return;
    }
    case func_unlink: {
        DEFINE_DATA(struct func_unlink, buf, buf_size, data);
        return;
    }
    case func_rename: {
        DEFINE_DATA(struct func_rename, buf, buf_size, data);
        return;
    }
    case func_chmod: {
        DEFINE_DATA(struct func_chmod, buf, buf_size, data);
        return;
    }
    case func_mknod: {
        DEFINE_DATA(struct func_mknod, buf, buf_size, data);
        return;
    }
    case func_mkdir: {
        DEFINE_DATA(struct func_mkdir, buf, buf_size, data);
        return;
    }
    case func_rmdir: {
        DEFINE_DATA(struct func_rmdir, buf, buf_size, data);
        return;
    }
    case func_link: {
        DEFINE_DATA(struct func_link, buf, buf_size, data);
        return;
    }
    case func_chown: {
        DEFINE_DATA(struct func_chown, buf, buf_size, data);
        return;
    }

    case func_openr: {
        DEFINE_DATA(struct func_openr, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }
    case func_stat: {
        DEFINE_DATA(struct func_stat, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }
    case func_lstat: {
        DEFINE_DATA(struct func_lstat, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }
    case func_opendir: {
        DEFINE_DATA(struct func_opendir, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }
    case func_access: {
        DEFINE_DATA(struct func_access, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }
    case func_readlink: {
        DEFINE_DATA(struct func_readlink, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }
    case func_symlink: {
        DEFINE_DATA(struct func_symlink, buf, buf_size, data);
        query(data->target.in_path);
        break;
    }
    case func_exec: {
        DEFINE_DATA(struct func_exec, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }
    case func_realpath: {
        DEFINE_DATA(struct func_realpath, buf, buf_size, data);
        query(data->path.in_path);
        break;
    }

    case func_execp: {
        DEFINE_DATA(struct func_execp, buf, buf_size, data);
        query(data->file);
        break;
    }
    case func_trace: {
        DEFINE_DATA(struct func_trace, buf, buf_size, data);
        // std::cerr << "TRACE: '" << data->msg << "'" << std::endl;
        return;
    }
    default: PANIC("Bad enum: %u", func);
    }
}


static void sighandler(int signum) {
    std::cerr << "ABORTING: Got signal: " << signum << std::endl;
    exit(1);
}


int main(int argc, char *const argv[])
{

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <query command> COMMAND" << std::endl;
        exit(1);
    }

    struct sigaction sa;
    sa.sa_handler =  sighandler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction( SIGINT, &sa, NULL );


    if (0 != pipe(pipefd_to_parent)) {
        perror("pipe");
        exit(1);
    }
    if (0 != pipe(pipefd_to_child)) {
        perror("pipe");
        exit(1);
    }
    const pid_t pid = fork();
    if (pid == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(pipefd_to_child[1]);
        close(pipefd_to_parent[0]);
        dup2(pipefd_to_child[0], STDIN_FILENO);
        dup2(pipefd_to_parent[1], STDOUT_FILENO);
        // dup2(pipefd[1], 3);
        const char *const args[] = { "sh", "-c", argv[1], NULL };
        execvp("/bin/sh", (char *const*)args);
        fprintf(stderr, "execl failed\n");
        exit(1);
    }
    close(pipefd_to_child[0]);
    close(pipefd_to_parent[1]);

    trigger = new Trigger(&file_request);
    query(argv[2]);
    std::cerr << "Shutdown" << std::endl;
}
