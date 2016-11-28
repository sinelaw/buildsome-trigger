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
    LOG("WRITE: %s", str);                              \
    write(pipefd_to_child[1], str, strlen(str));        \
    write(pipefd_to_child[1], "\n", strlen("\n"))

static void query(const char *const build_target, const struct TargetContext *target_ctx)
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
    LOG("For query: '%s', got: '%s'", build_target, user_input);
    query_lock = false;
    // WRITE("Got: '" << user_input << "'");
    if (pos > 0) {
        trigger->Execute(user_input, target_ctx);
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
        const char *const args[] = { "sh", "-c", argv[1], NULL };
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
