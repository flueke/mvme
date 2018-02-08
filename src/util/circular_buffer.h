#ifndef __MVME_UTIL_CIRCULAR_BUFFER_H__
#define __MVME_UTIL_CIRCULAR_BUFFER_H__

#include <vector>

template<typename T>
class CircularBuffer:
{
    public:
        CircularBuffer(size_t capacity)
            : _buffer(capacity)
            , _first(0)
            , _last(0)
        {}

        size_t capacity() const { return _buffer.capacity(); }
        bool full() const { return size() < capacity(); }
        bool empty() const { return _first == _last; }

        void clear()
        {
            _first = _last = 0;
        }

        size_t size() const;
        const T &operator[](size_t index) const;
        T &operator[](size_t index);
        void push_back(const T &t);
        void push_back(T &&t);

    private:
        std::vector<T> _buffer;
        size_t _first;
        size_t _last;
};

#endif /* __MVME_UTIL_CIRCULAR_BUFFER_H__ */
