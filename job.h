#pragma once

#include "optional.h"
#include "fs_tree.h"
#include "build_rules.h"

#include <vector>
#include <string>
#include <functional>

class Job {
    BuildRule m_rule;
    std::function<void(std::string,
                       std::function<void(void)>)> m_resolve_input_cb;
    std::function<void(Outcome)> m_completion_cb;


public:
    explicit Job(const BuildRule &rule,
                 std::function<void(std::string,
                                    std::function<void(void)>)> resolve_input_cb,
                 std::function<void(Outcome)> completion_cb)
        : m_rule(rule), m_resolve_input_cb(resolve_input_cb), m_completion_cb(completion_cb) { };

    void execute(void);
    void want(std::string);
};
