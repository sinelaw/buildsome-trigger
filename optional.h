#pragma once

template <typename T> class Optional
{
private:
    bool m_has_value;
    union Payload {
        T value;
        Payload() { }
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
        static_assert(std::is_standard_layout<T>::value);
        assert(size == sizeof(T));
        memcpy(&this->m_payload.value, src, sizeof(T));
        this->m_has_value = true;
    }

    bool has_value() const {
        return this->m_has_value;
    }

    const T &get_value() const {
        assert(this->m_has_value);
        return this->m_payload.value;
    }
};
