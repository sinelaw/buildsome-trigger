#include "trigger.h"

#include <cstdlib>
#include <iostream>

void file_request(const char *filename) {
    std::cout << filename << std::endl;
}

int main(int argc, char *const argv[]) {

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " COMMAND..." << std::endl;
        exit(1);
    }

    Trigger trigger(&file_request);
    trigger.Execute(argv[1], (argv + 1));
}
