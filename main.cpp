#include "assert.h"
#include "build_rules.h"

#include <vector>
#include <deque>
#include <string>


void resolve_all(BuildRules &build_rules, std::deque<std::string> &resolve_queue, std::deque<BuildRule> &rule_queue)
{
    while (resolve_queue.size() > 0) {
        auto target = resolve_queue.front();
        resolve_queue.pop_front();
        const BuildRule rule = build_rules.query(target);
        for (auto input : rule.inputs) {
            resolve_queue.push_back(input);
        }
        rule_queue.push_back(rule);
    }
}

void build(BuildRules &build_rules, const std::vector<std::string> &targets)
{
    std::deque<std::string> resolve_queue;
    for (auto target : targets) {
        resolve_queue.push_back(target);
    }
    std::deque<BuildRule> rule_queue;

    while (true) {
        // TODO: bg thread? or use async IO and a reactor?
        resolve_all(build_rules, resolve_queue, rule_queue);

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
