#pragma once

#include "optional.h"
#include "fs_tree.h"
#include "build_rules.h"

#include <vector>
#include <string>
#include <functional>

class Job {
    BuildRule m_rule;
public:
    explicit Job(const BuildRule &rule) : m_rule(rule) { };
    void execute(
        std::function<void(std::string,
                           std::function<void(void)>)> resolve_input_cb,
        std::function<void(Outcome)> completion_cb);
};
