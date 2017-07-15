#pragma once

#include "optional.h"

#include <vector>
#include <string>

struct BuildRule {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> commands;

    bool operator==(const BuildRule &other) const {
        return (inputs == other.inputs)
            && (outputs == other.outputs)
            && (commands == other.commands);
    }

    struct Hash {
        static std::size_t hash_vector(const std::vector<std::string> &v, std::size_t seed) {
            std::size_t res = seed;
            for (auto s : v) {
                res ^= std::hash<std::string>{}(s);
            }
            return res;
        }

        std::size_t operator()(BuildRule const& s) const noexcept {
            return hash_vector(s.inputs,
                               hash_vector(s.outputs,
                                           hash_vector(s.commands, 1)));
        }
    };
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
