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
        /* std::unique_lock<std::mutex> lck (debug_lock);                  \ */ \
        if ((1)) {                                                      \
            auto now_1 = std::chrono::system_clock::now();              \
            auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now_1.time_since_epoch()); \
            std::cerr << now_us.count() << " " << pthread_self() << ":" << __FILE__ << ":" << __LINE__ << ": " << x << std::endl; \
        }                                                               \
    } while (0)



#define _TIMEIT(act, counter)                                           \
    auto before_1 = std::chrono::steady_clock::now();                   \
    act;                                                                \
    auto after_1 = std::chrono::steady_clock::now();                    \
    auto diff_1 = after_1 - before_1;                                   \
    auto micros_diff_1 = std::chrono::duration_cast<std::chrono::microseconds>(diff_1); \
    if (micros_diff_1.count() > 1000) DEBUG(micros_diff_1.count() << " us: " #act)

#define TIMEIT(act) _TIMEIT(act, __COUNTER__)
