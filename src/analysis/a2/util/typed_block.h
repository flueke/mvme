#ifndef __TYPED_BLOCK_H__
#define __TYPED_BLOCK_H__

#include <limits>
#include "../memory.h"

template<typename T, typename SizeType = size_t>
struct TypedBlock
{
    typedef SizeType size_type;
    static constexpr auto size_max = std::numeric_limits<SizeType>::max();

    T *data;
    size_type size;

    inline T operator[](size_type index) const
    {
        assert(index < size);
        return data[index];
    }

    inline T &operator[](size_type index)
    {
        assert(index < size);
        return data[index];
    }

    inline T *begin()
    {
        return data;
    }

    inline T *end()
    {
        return data + size;
    }
};

template<typename T, typename SizeType = size_t>
TypedBlock<T, SizeType> push_typed_block(
    memory::Arena *arena,
    SizeType size,
    size_t align = alignof(T))
{
    TypedBlock<T, SizeType> result;

    result.data = arena->pushArray<T>(size, align);
    result.size = result.data ? size : 0;
    assert(memory::is_aligned(result.data, align));

    return result;
};

template<typename T, typename SizeType = size_t>
TypedBlock<T, SizeType> make_typed_block(
    T *data,
    SizeType size)
{
    TypedBlock<T, SizeType> result;
    result.data = data;
    result.size = size;
    return result;
}


#endif /* __TYPED_BLOCK_H__ */
