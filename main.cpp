#include "assert.h"
#include "build_rules.h"

#include <vector>
#include <deque>
#include <string>
#include <map>

void resolve_all(BuildRules &build_rules, std::deque<std::string> &resolve_queue,
                 uint32_t &rule_index, std::map<uint32_t, BuildRule> &rules)
{
    std::map<std::string, Optional<BuildRule>> known_rules;
    while (resolve_queue.size() > 0) {
        auto target = resolve_queue.front();
        resolve_queue.pop_front();
        if (known_rules.find(target) == known_rules.end()) continue;
        const Optional<BuildRule> orule = build_rules.query(target);
        known_rules[target] = orule;
        if (orule.has_value()) {
            const BuildRule &rule = orule.get_value();
            for (auto input : rule.inputs) {
                resolve_queue.push_back(input);
            }
            rules[rule_index] = rule;
            rule_index++;
        }
    }
}

void build(BuildRules &build_rules, const std::vector<std::string> &targets)
{
    std::deque<std::string> resolve_queue;
    for (auto target : targets) {
        resolve_queue.push_back(target);
    }
    std::map<uint32_t, BuildRule> rules;
    uint32_t rule_index = 0;
    while (true) {
        // TODO: bg thread? or use async IO and a reactor?
        resolve_all(build_rules, resolve_queue, rule_index, rules);

    }
}

int main(int argc, char **argv)
{
    ASSERT(argc >= 0);
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <query program> <target>" << std::endl;
        return 1;
    }

    BuildRules build_rules(argv[1]);

    std::vector<std::string> targets;
    for (uint32_t i = 2; i < (uint32_t)argc; i++) {
        targets.emplace_back(argv[i]);
    }

    build(build_rules, targets);

    return 0;
}
