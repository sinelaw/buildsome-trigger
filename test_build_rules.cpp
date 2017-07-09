#include "build_rules.h"
#include <iostream>

int main(int argc, char **argv)
{

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <query program>" << std::endl;
        return 1;
    }

    BuildRules build_rules(argv[1]);

    build_rules.query("default");

    return 0;
}
