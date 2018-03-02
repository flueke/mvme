#ifndef __A2_MEMORY_H__
#define __A2_MEMORY_H__

#include <cassert>
#include <cstdlib>
#include <exception>
#include <memory>

#include "util/typedefs.h"

namespace memory
{

struct out_of_memory: public std::exception { };

template<typename T>
inline bool is_aligned(const T *ptr, size_t alignment = alignof(T))
{
    return (((uintptr_t)ptr) % alignment) == 0;
}

struct Arena
{
    u8 *mem;
    void *cur;
    size_t size;

    explicit Arena(size_t size)
        : mem(new u8[size])
        , cur(mem)
        , size(size)
    { }

    ~Arena()
    {
        delete[] mem;
    }

    // can't copy
    Arena(Arena &other) = delete;
    Arena &operator=(Arena &other) = delete;

    // can move
    Arena(Arena &&other) = default;
    Arena &operator=(Arena &&other) = default;

    inline size_t free() const
    {
        return (mem + size) - reinterpret_cast<u8 *>(cur);
    }

    inline size_t used() const
    {
        return size - free();
    }

    inline void reset()
    {
        cur = mem;
    }

    template<typename T>
    T *pushObject(size_t align = alignof(T))
    {
        T *result = nullptr;
        size_t space = free();
        if (std::align(align, sizeof(T), cur, space))
        {
            result = new (cur) T;
            cur = reinterpret_cast<u8 *>(cur) + sizeof(T);
            assert(is_aligned(result, align));
        }
        else
        {
            throw out_of_memory();
        }
        return result;
    }

    /** Use for POD types only! It doesn't do any construction. */
    template<typename T>
    T *pushStruct(size_t align = alignof(T))
    {
#if 0
        T *result = nullptr;
        size_t space = free();
        if (std::align(align, sizeof(T), cur, space))
        {
            result = reinterpret_cast<T*>(cur);
            cur = reinterpret_cast<u8 *>(cur) + sizeof(T);
            assert(is_aligned(result, align));
        }
        else
        {
            throw out_of_memory();
        }
        return result;
#else
        return pushObject<T>(align);
#endif
    }

    template<typename T>
    T *pushArray(size_t size, size_t align = alignof(T))
    {
        T *result = nullptr;
        size_t space = free();
        if (std::align(align, sizeof(T) * size, cur, space))
        {
            result = reinterpret_cast<T*>(cur);
            cur = reinterpret_cast<u8 *>(cur) + sizeof(T) * size;
            assert(is_aligned(result, align));
        }
        else
        {
            throw out_of_memory();
        }
        return result;
    }

    template<typename T>
    T *push(const T &t, size_t align = alignof(T))
    {
        auto result = pushStruct<T>(align);
        if (result)
        {
            *result = t;
        }
        return result;
    }

    void *pushSize(size_t size, size_t align = 1)
    {
        return reinterpret_cast<void *>(pushArray<u8>(size, align));
    }
};

struct Blk
{
    void *ptr;
    size_t size;
};

template<class Primary, class Fallback>
struct FallbackAllocator
    : private Primary
    , private Fallback
{
    Blk allocate(size_t size, size_t align)
    {
        Blk result = Primary::allocate(size, align);
        if (!result.ptr)
        {
            result = Fallback::allocate(size, align);
        }
        return result;
    }

    void deallocate(Blk block)
    {
        if (Primary::owns(block))
        {
            Primary::deallocate(block);
        }
        else
        {
            Fallback::deallocate(block);
        }
    }

    bool owns(Blk block)
    {
        return Primary::owns(block) || Fallback::owns(block);
    }
};

struct Mallocator
{
    Blk allocate(size_t size)
    {
        Blk result;
        void *ptr = malloc(size);
        if (ptr)
        {
            result = { ptr, size };
        }

        return result;
    }

    void deallocate(Blk block)
    {
        free(block.ptr);
    }
};

struct ArenaAllocator
{
    Arena arena;

    Blk allocate(size_t size, size_t align)
    {
        Blk result;
        result.ptr = arena.pushSize(size, align);
        if (result.ptr)
        {
            result.size = size;
        }

        return result;
    }

    void deallocate(Blk block)
    {
        assert(false);
    }

    void deallocateAll()
    {
        arena.reset();
    }

    bool owns(Blk block)
    {
        return arena.mem <= block.ptr && block.ptr < arena.cur;
    }
};

} // namespace memory

#endif /* __A2_MEMORY_H__ */
