#pragma once

#include <iostream>

#define PANIC(msg) do {                                                 \
        std::cerr << "PANIC: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        exit(1);                                                        \
    } while (0)

#define ASSERT(x) {                                                     \
        if (!(x)) {                                                     \
            std::cerr << "ERROR " << #x << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define DEBUG(x) std::cout << x << std::endl;
