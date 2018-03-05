#ifndef __A2_MEMORY_H__
#define __A2_MEMORY_H__

#include <cassert>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

#include "util/typedefs.h"

namespace memory
{

struct out_of_memory: public std::exception { };

template<typename T>
inline bool is_aligned(const T *ptr, size_t alignment = alignof(T))
{
    return (((uintptr_t)ptr) % alignment) == 0;
}

struct ArenaBase
{
    u8 *mem;
    void *cur;
    size_t size;

    explicit ArenaBase(size_t size)
        : mem(new u8[size])
        , cur(mem)
        , size(size)
    { }

    ~ArenaBase()
    {
        delete[] mem;
    }

    // can't copy
    ArenaBase(ArenaBase &other) = delete;
    ArenaBase &operator=(ArenaBase &other) = delete;

    // can move
    ArenaBase(ArenaBase &&other) = default;
    ArenaBase &operator=(ArenaBase &&other) = default;

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

    /** Use for POD types only! It doesn't do any construction. */
    template<typename T>
    T *pushStruct(size_t align = alignof(T))
    {
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

struct Arena: public ArenaBase
{
    using ArenaBase::ArenaBase;

    // can't copy
    Arena(Arena &other) = delete;
    Arena &operator=(Arena &other) = delete;

    // can move
    Arena(Arena &&other) = default;
    Arena &operator=(Arena &&other) = default;
};

namespace detail
{

template<typename T>
struct destroy_only_deleter
{
    void operator()(T *ptr)
    {
        //fprintf(stderr, "%s %p\n", __PRETTY_FUNCTION__, ptr);
        ptr->~T();
    }
};

} // namespace detail

struct ObjectArena: public ArenaBase
{
    explicit ObjectArena(size_t size)
        : ArenaBase(size)
    { }

    ~ObjectArena()
    {
        reset();
    }

    inline void reset()
    {
        for (auto it = deleters.rbegin();
             it != deleters.rend();
             it++)
        {
            (*it)();
        }

        cur = mem;
    }

    template<typename T>
    T *pushObject(size_t align = alignof(T))
    {
        T *result = nullptr;
        size_t space = free();
        if (std::align(align, sizeof(T), cur, space))
        {
            // Construct the object inside the arena.
            result = new (cur) T;
            assert(is_aligned(result, align));

            // Now push a lambda calling the object destructor onto the
            // deleters vector. Object construction and vector modification
            // must be atomic in regards to exceptions: the object must be
            // destroyed even if the push fails.
            // To achieve exception safety a unique_ptr with a custom deleter
            // that only runs the detructor is used to temporarily hold the
            // object pointer. If the vector push throws the unique_ptr will
            // properly destroy the object. Otherwise the deleter lambda has
            // been stored and thus the pointer can be release() from the
            // unique_ptr.

            std::unique_ptr<T, detail::destroy_only_deleter<T>> guard_ptr(result);

            // This next call can throw (for example bad_alloc if running OOM).
            deleters.emplace_back([result] () {
                //fprintf(stderr, "%s %p\n", __PRETTY_FUNCTION__, result);
                result->~T();
            });

            // emplace_back() did not throw. It's safe to release the guard now.
            guard_ptr.release();

            cur = reinterpret_cast<u8 *>(cur) + sizeof(T);
        }
        else
        {
            throw out_of_memory();
        }
        return result;
    }

    private:
    using Deleter = std::function<void ()>;
    std::vector<Deleter> deleters;
};

/*
 * "std::allocator Is to Allocation what std::vector Is to Vexation", CppCon 2015: Andrei Alexandrescu
 */
#if 0
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
    ArenaBase arena;

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
#endif

} // namespace memory

#endif /* __A2_MEMORY_H__ */
