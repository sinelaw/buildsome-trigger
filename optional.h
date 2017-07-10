#pragma once

#include "assert.h"

#include <cstdint>
#include <type_traits>

template <typename T> class Optional
{
private:
    bool m_has_value;
    union Payload {
        T value;
        Payload() { }
        ~Payload() { }
    } m_payload;

public:
    Optional() : m_has_value(false) {
    }

    Optional(T x) : m_has_value(true) {
        m_payload.value = x;
    }

    Optional(const Optional &other) {
        if (other.m_has_value) {
            this->m_payload.value = other.m_payload.value;
        }
        this->m_has_value = other.m_has_value;
    }

    Optional(const char *src, uint32_t size)
    {
        DEBUG("from src");
        static_assert(std::is_standard_layout<T>::value);
        ASSERT(size == sizeof(T));
        memcpy(&this->m_payload.value, src, sizeof(T));
        this->m_has_value = true;
    }

    bool has_value() const {
        return this->m_has_value;
    }

    const T &get_value() const {
        ASSERT(this->m_has_value);
        return this->m_payload.value;
    }
};
