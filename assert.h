#pragma once

#include <iostream>

#define PANIC(msg) do {                                                 \
        const int errno_ = errno;                                       \
        std::cerr << "PANIC: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::cerr << "\terrno was: " << errno_ << std::endl;             \
        abort();                                                        \
    } while (0)

#define ASSERT(x) do {                                                  \
        if (!(x)) {                                                     \
            const int errno_ = errno;                                   \
            std::cerr << "ERROR " << #x << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::cerr << "\terrno was: " << errno_ << std::endl;        \
            abort();                                                    \
        }                                                               \
    } while (0)

#define DEBUG(x) std::cerr << __FILE__ << ":" << __LINE__ << ": " << x << std::endl;
