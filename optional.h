#pragma once

#include "assert.h"

#include <cstdint>
#include <type_traits>

template <typename T> class Optional
{
private:
    bool m_has_value;
    T *value;

public:
    ~Optional() {
        if (m_has_value) delete value;
    }

    Optional() : m_has_value(false), value(nullptr) {
    }

    Optional(const T &x) : m_has_value(true) {
        value = new T(x);
    }

    Optional(const Optional &other) {
        if (other.m_has_value) {
            this->value = new T(*other.value);
        }
        this->m_has_value = other.m_has_value;
    }

    Optional(const char *src, uint32_t size)
    {
        DEBUG("from src");
        static_assert(std::is_standard_layout<T>::value);
        ASSERT(size == sizeof(T));
        char temp[sizeof(T)];
        memcpy(temp, src, sizeof(T));
        this->value = new T(*((T*)temp));
        this->m_has_value = true;
    }

    bool has_value() const {
        return this->m_has_value;
    }

    const T &get_value() const {
        ASSERT(this->m_has_value);
        return *this->value;
    }
};
