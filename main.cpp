#include "trigger.h"

#include <cstdlib>
#include <iostream>

extern "C" {
#include <signal.h>
#include "fshook/protocol.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ftw.h>
}

static Trigger *trigger;
static int pipefd_to_child[2];
static int pipefd_to_parent[2];
#define WRITE(str)                                                      \
    do {                                                                \
        LOG("WRITE: %s", str);                                          \
        uint32_t _len_ = strlen(str);                                   \
        ASSERT(_len_ == write(pipefd_to_child[1], str, strlen(str)));   \
        ASSERT(1 == write(pipefd_to_child[1], "\n", strlen("\n")));     \
    } while (0)

static uint32_t read_line(int fd, char *output, uint32_t output_max_size)
{
    uint32_t pos = 0;
    while (true) {
        int read_res = read(fd, &output[pos], 1);
        if (read_res < 1) {
            perror("read");
            exit(1);
        }
        if (output[pos] == '\n') {
            break;
        }
        // std::cerr << "read: " << output[pos] << std::endl;
        ASSERT(pos < output_max_size);
        pos++;
    }
    output[pos] = '\0';
    return pos;
}

static uint32_t read_multi_line(int fd, char *output, uint32_t output_max_size)
{
    const uint32_t read_size = read_line(fd, output, output_max_size);
    ASSERT(read_size > 0);
    char *endptr;
    const uint32_t lines_count = strtoul(output, &endptr, 10);
    ASSERT(endptr == output + strlen(output));
    char *cur = output;
    LOG("Reading %u lines", lines_count);
    for (uint32_t i = 0; i < lines_count; i++) {
        const uint32_t line_read_size = read_line(fd, cur, output_max_size - (cur - output) - 1);
        cur += line_read_size;
        *cur = '\n';
        cur++;
    }
    *cur = '\0';
    return cur - output;
}

static int remove_fn(const char *fpath, const struct stat *sb,
                     int typeflag, struct FTW *ftwbuf)
{
    AVOID_UNUSED(sb);
    AVOID_UNUSED(ftwbuf);
    if (typeflag & FTW_D) {
        LOG("rmdir: %s", fpath);
        ASSERT(0 == rmdir(fpath));
    } else {
        LOG("unlink: %s", fpath);
        ASSERT(0 == unlink(fpath));
    }
    return 0;
}

static void remove_dir_recursively(const char *dirpath)
{
    const int nopenfd = 10;
    const int res = nftw(dirpath, remove_fn, nopenfd, FTW_DEPTH);
    ASSERT(0 == res);
}

static void query(const char *const build_target, const struct TargetContext *target_ctx)
{
    static volatile bool query_lock = false;
    while (query_lock) {
        usleep(10);
    }
    query_lock = true;

    WRITE(build_target);

#define MIN(x, y) ((x <= y) ? x : y)

    char target_cmd[0x8000];
    const uint32_t cmd_size = read_multi_line(pipefd_to_parent[0], target_cmd, ARRAY_LEN(target_cmd));
    char target_inputs[0x1000];
    const uint32_t inputs_size = read_multi_line(pipefd_to_parent[0], target_inputs, ARRAY_LEN(target_inputs));
    char target_outputs[0x1000];
    const uint32_t outputs_size = read_multi_line(pipefd_to_parent[0], target_outputs, ARRAY_LEN(target_outputs));
    LOG("For query: '%s', got:\ncmd = '%s'\ninputs = '%s'\noutputs = '%s'", build_target, target_cmd, target_inputs, target_outputs);
    query_lock = false;
    char *input_cur = target_inputs;
    for (uint32_t i = 0; i < inputs_size; i++) {
        if (target_inputs[i] != '\n') continue;
        target_inputs[i] = '\0';
        LOG("Wanting: %s", input_cur);
        trigger->want(input_cur, target_ctx);
        input_cur = &target_inputs[i + 1];
    }
    char *output_cur = target_outputs;
    for (uint32_t i = 0; i < outputs_size; i++) {
        if (target_outputs[i] != '\n') continue;
        target_outputs[i] = '\0';
        struct stat output_file_stat;
        if (0 == stat(output_cur, &output_file_stat)) {
            if (output_file_stat.st_mode & S_IFDIR) {
                remove_dir_recursively(output_cur);
            } else {
                LOG("Unlink: %s", output_cur);
                ASSERT(0 == unlink(output_cur));
            }
        } else {
            ASSERT(ENOENT == errno);
        }
        output_cur = &target_outputs[i + 1];
    }
    // WRITE("Got: '" << target_cmd << "'");
    if (cmd_size > 0) {
        trigger->Execute(target_cmd, target_ctx);
    }
}

void file_request(const char *input_path, const struct TargetContext *target_ctx)
{
    query(input_path, target_ctx);
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
        const char *const args[] = { SHELL_EXE_PATH, "-c", argv[1], NULL };
        execvp("/bin/sh", (char *const*)args);
        fprintf(stderr, "execl failed\n");
        exit(1);
    }
    close(pipefd_to_child[0]);
    close(pipefd_to_parent[1]);

    trigger = new Trigger(&file_request);
    trigger->want(argv[2], NULL);
    LOG("Shutdown");
}
