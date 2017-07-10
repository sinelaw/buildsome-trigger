#include "build_rules.h"
#include "assert.h"
#include "optional.h"

#include <vector>
#include <string>
#include <iostream>

extern "C" {
#include <unistd.h>
#include <string.h>
}


BuildRules::BuildRules(std::string query_program)
{
    if (0 != pipe(m_pipefd_to_parent)) {
        perror("pipe");
        exit(1);
    }
    if (0 != pipe(m_pipefd_to_child)) {
        perror("pipe");
        exit(1);
    }
    const pid_t pid = fork();
    if (pid == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(m_pipefd_to_child[1]);
        close(m_pipefd_to_parent[0]);
        dup2(m_pipefd_to_child[0], STDIN_FILENO);
        dup2(m_pipefd_to_parent[1], STDOUT_FILENO);
        // dup2(pipefd[1], 3);
        const char *const args[] = { "/bin/sh", "-c", query_program.c_str(), NULL };
        execvp("/bin/sh", (char *const*)args);
        std::cerr << "execl failed: " << errno << std::endl;
        exit(1);
    }
    close(m_pipefd_to_child[0]);
    close(m_pipefd_to_parent[1]);

}

BuildRules::~BuildRules()
{
    close(m_pipefd_to_child[1]);
    close(m_pipefd_to_parent[0]);
}

static void do_write(int fd, const std::string &str)
{
    // LOG("WRITE: %s", str);
    int written = write(fd, str.c_str(), str.size());
    ASSERT(written >= 0);
    ASSERT(str.size() == (uint32_t)written);
    ASSERT(1 == write(fd, "\n", strlen("\n")));
}

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

static std::vector<std::string> read_multi_line(int fd)
{
    char line[2048];
    constexpr const uint32_t line_max_size = sizeof(line);
    const uint32_t read_size = read_line(fd, line, line_max_size);
    ASSERT(read_size > 0);
    char *endptr;
    const int32_t res = strtol(line, &endptr, 10);
    ASSERT(res >= 0);
    const uint32_t lines_count = res;
    ASSERT(endptr == line + strlen(line));
    std::vector<std::string> result;
    // LOG("Reading %u lines", lines_count);
    for (uint32_t i = 0; i < lines_count; i++) {
        const uint32_t line_size __attribute__((unused)) = read_line(fd, line, line_max_size);
        result.push_back(std::string(line));
    }
    return result;
}

Optional<BuildRule> BuildRules::query(std::string output) const
{
    DEBUG("write");
    do_write(m_pipefd_to_child[1], output);
    BuildRule result;
    DEBUG("read commands");
    result.commands = read_multi_line(m_pipefd_to_parent[0]);
    DEBUG("read inputs");
    result.inputs = read_multi_line(m_pipefd_to_parent[0]);
    DEBUG("read outputs");
    result.outputs = read_multi_line(m_pipefd_to_parent[0]);
    if ((result.outputs.size() == 0)
        && (result.inputs.size() == 0)
        && (result.commands.size() == 0))
    {
        DEBUG("no result");
        return Optional<BuildRule>();
    }
    DEBUG("result");
    return Optional<BuildRule>(result);
}
