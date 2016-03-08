#include "trigger.h"

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
}

#include <iostream>

#define PANIC(msg) do {                                   \
        std::cerr << "FATAL ERROR: " << msg << std::endl; \
        exit(1);                                          \
    } while (0)

Trigger::Trigger(FileRequestCb *cb) {
    m_cb = cb;
}

int Trigger::Execute(const char *filename, char *const argv[], char *const envp[])
{
    pid_t child = fork();
    if (0 == child) {
        execvpe(filename, argv, envp);
        PANIC("execvpe failed?!");
    }
    std::cout << "forked child: " << child << std::endl;
    int res;
    wait(&res);
    return res;
}
