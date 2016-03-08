#include <deque>


template <class T> class Pool {
private:
    std::deque<T *> m_free;
    uint32_t m_used_count;
public:

    Pool(uint32_t count) {
        m_used_count = 0;
        for (uint32_t i = 0; i < count; i++) {
            m_free.push_back(new T());
        }
    }

    T *Get() {
        m_used_count++;
        T *res = m_free.front();
        m_free.pop_front();
        return res;
    }

    void Put(T *item) {
        m_free.push_front(item);
    }

    ~Pool();
};
