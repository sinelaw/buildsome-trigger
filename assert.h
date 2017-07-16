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



#define CONCAT_(A, B) A ## B
#define CONCAT(A,B) CONCAT_(A,B)

#define _TIMEIT(act, counter)                                           \
    /* DEBUG("Going to " #act);                                      */ \
    auto CONCAT(before_, counter) = std::chrono::steady_clock::now();   \
    act;                                                                \
    do {                                                                \
        auto CONCAT(after_, counter) = std::chrono::steady_clock::now(); \
        auto CONCAT(diff_, counter) = CONCAT(after_, counter) - CONCAT(before_, counter); \
        auto CONCAT(micros_diff_, counter) = std::chrono::duration_cast<std::chrono::microseconds>(CONCAT(diff_, counter)); \
        if (CONCAT(micros_diff_, counter).count() > 1000) DEBUG(CONCAT(micros_diff_, counter).count() << " us: " #act); \
    } while (0)

#define TIMEIT(act) _TIMEIT(act, __COUNTER__)
