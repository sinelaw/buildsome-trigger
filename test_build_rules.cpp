#include "build_rules.h"
#include "optional.h"

#include <iostream>

int main(int argc, char **argv)
{

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <query program> <target>" << std::endl;
        return 1;
    }

    BuildRules build_rules(argv[1]);

    Optional<BuildRule> orule = build_rules.query(argv[2]);

    if (orule.has_value()) {
        const BuildRule rule = orule.get_value();
        std::cout << "Command lines:" << std::endl;
        for (auto cmd : rule.commands) {
            std::cout << "\t" << cmd << std::endl;
        }
        std::cout << "Inputs:" << std::endl;
        for (auto item : rule.inputs) {
            std::cout << "\t" << item << std::endl;
        }
        std::cout << "Outputs:" << std::endl;
        for (auto item : rule.outputs) {
            std::cout << "\t" << item << std::endl;
        }
    } else {
        std::cout << "NOT FOUND: " << argv[2] << std::endl;
    }
    return 0;
}
