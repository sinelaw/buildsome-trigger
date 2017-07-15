#pragma once

#include "optional.h"

#include <vector>
#include <string>

struct BuildRule {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> commands;

    bool operator<(const BuildRule &other) const {
        return (outputs < other.outputs);
    }

    std::string to_string() const {
        return outputs.front();
    }
};

class BuildRules {
public:
    explicit BuildRules(std::string query_program);
    ~BuildRules();

    Optional<BuildRule> query(std::string output) const;

    BuildRules(const BuildRules &) =delete;
    BuildRules& operator=(const BuildRules &) =delete;

private:
    int m_pipefd_to_child[2];
    int m_pipefd_to_parent[2];
};
