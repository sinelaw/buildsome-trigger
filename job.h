#pragma once

#include "optional.h"
#include "fs_tree.h"
#include "build_rules.h"

#include <vector>
#include <string>
#include <functional>
#include <thread>

class Job {
    BuildRule m_rule;
    std::function<void(std::string,
                       std::function<void(void)>)> m_resolve_input_cb;

public:
    explicit Job(const BuildRule &rule,
                 std::function<void(std::string,
                                    std::function<void(void)>)> resolve_input_cb)
        : m_rule(rule)
        , m_resolve_input_cb(resolve_input_cb)
    {
    };

    const BuildRule &get_rule() const { return m_rule; }
    void execute();
    void want(std::string);
};
