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
        DEBUG("Dying " << this);
        if (m_has_value) delete value;
        value = nullptr;
    }

    explicit Optional() : m_has_value(false), value(nullptr) {
        DEBUG("Optional() " << this);
    }

    explicit Optional(const T &x) {
        DEBUG("Optional(x) " << this);
        value = new T(x);
        m_has_value = true;
    }

    Optional& operator=(const Optional &other) {
        DEBUG("Assigning: " << &other << "->" << this);
        if (m_has_value && other.m_has_value) {
            *this->value = *other.value;
            return *this;
        }
        if (other.m_has_value) {
            this->value = new T(*other.value);
        }
        this->m_has_value = other.m_has_value;
        return *this;
    }

    Optional(const Optional &other) {
        DEBUG("Copying: " << &other << "->" << this);
        if (other.m_has_value) {
            this->value = new T(*other.value);
        }
        this->m_has_value = other.m_has_value;
    }

    Optional(const char *src, uint32_t size)
    {
        DEBUG("from src: " << this);
        static_assert(std::is_standard_layout<T>::value, "Value must have standard_layout");
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
