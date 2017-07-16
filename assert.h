#pragma once

extern "C" {
#include "pthread.h"
}
#include <iostream>
#include <mutex>

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

extern std::mutex debug_lock;

#define DEBUG(x) do {                                                   \
        /* std::unique_lock<std::mutex> lck (debug_lock);                  \ */\
        std::cerr << pthread_self() << ":" << __FILE__ << ":" << __LINE__ << ": " << x << std::endl; \
    } while (0)
