#pragma once

#include <vector>
#include <string>

struct BuildRule {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> commands;
};

class BuildRules {
public:
    explicit BuildRules(std::string query_program);
    ~BuildRules();

    BuildRule query(std::string output) const;

    BuildRules(const BuildRules &) =delete;
    BuildRules& operator=(const BuildRules &) =delete;

private:
    int m_pipefd_to_child[2];
    int m_pipefd_to_parent[2];
};
