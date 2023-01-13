/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __TYPED_BLOCK_H__
#define __TYPED_BLOCK_H__

#include <limits>
#include "../memory.h"
#include <cstdio>

template<typename T, typename SizeType = size_t>
struct TypedBlock
{
    typedef SizeType size_type;
    typedef T value_type;

    static constexpr auto size_max = std::numeric_limits<size_type>::max();
    static constexpr size_t element_size = sizeof(value_type);

    T *data;
    size_type size;

    inline T operator[](size_type index) const
    {
        assert(0 <= index && index < size);
        return data[index];
    }

    inline T &operator[](size_type index)
    {
        assert(0 <= index && index < size);
        return data[index];
    }

    inline T *begin() { return data; }
    inline const T *begin() const { return data; }

    inline T *end() { return data + size; }
    inline const T *end() const { return data + size; }

#if 0
    TypedBlock<T, SizeType>()
        : data(nullptr)
        , size(0)
    {
    }

    TypedBlock<T, SizeType>(T *data, SizeType size)
        : data(data)
        , size(size)
    {
    }

    TypedBlock<T, SizeType>(const TypedBlock<T, SizeType> &other)
        : data(other.data)
        , size(other.size)
    {
        fprintf(stderr, "%s\n",  __PRETTY_FUNCTION__);
    }

    TypedBlock<T, SizeType> &operator=(const TypedBlock<T, SizeType> &other)
    {
        fprintf(stderr, "%s\n",  __PRETTY_FUNCTION__);
        data = other.data;
        size = other.size;
        return *this;
    }
#endif
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
TypedBlock<T, SizeType> push_copy_typed_block(
    memory::Arena *arena,
    TypedBlock<T, SizeType> source,
    size_t align = alignof(T))
{
    auto result = push_typed_block<T, SizeType>(arena, source.size, align);

    for (SizeType i = 0; i < source.size; i++)
    {
        result[i] = source[i];
    }

    return result;
}

template<typename T, typename SizeType = size_t>
TypedBlock<T, SizeType> push_copy_typed_block(
    memory::Arena *arena,
    const std::vector<T> &source,
    size_t align = alignof(T))
{
    auto result = push_typed_block<T, SizeType>(arena, static_cast<SizeType>(source.size()), align);

    for (SizeType i = 0; i < static_cast<SizeType>(source.size()); i++)
    {
        result[i] = source[i];
    }

    return result;
}

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
