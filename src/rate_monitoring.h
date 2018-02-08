#ifndef __RATE_MONITORING_H__
#define __RATE_MONITORING_H__

#include "analysis/a2/util/typed_block.h"
#include <memory>

template<typename T>
class CircularBuffer
{
    public:
        using Buffer = TypedBlock<char>;

        CircularBuffer(Buffer buffer)
            : _buffer(buffer)
            , _first(0)
            , _last(0)
        {}

        size_t capacity() const { return _buffer.size; }
        bool full() const { return size() >= capacity(); }
        bool empty() const { return _first == _last; }

        void clear()
        {
            _first = _last = 0;
        }

        size_t size() const
        {
            if (_first <= _last)
                return _last - _first;

            return _last + (capacity() - _first);
        }

        const T &operator[](size_t index) const
        {
            if (_first <= _last)
                return _buffer[_first + index];

            index -= capacity() - _first; 

            return _buffer[_last + index];
        }

        T &operator[](size_t index)
        {
            if (_first <= _last)
                return _buffer[_first + index];

            index -= capacity() - _first; 

            return _buffer[_last + index];
        }

        void push_back(const T &t)
        {
            _buffer[_last++] = t;

            if (_last >= _buffer.size)
                _last = 0;
        }

        void push_back(T &&t)
        {
            _buffer[_last++] = t;

            if (_last >= _buffer.size)
                _last = 0;
        }

    private:
        Buffer _buffer;
        size_t _first;
        size_t _last;
};

#if 0
template<typename T>
struct SharedArenaMemory
{
    std::shared_ptr<memory::Arena> arena;
    T *data = nullptr;
    size_t size;
};
#endif

#endif /* __RATE_MONITORING_H__ */
