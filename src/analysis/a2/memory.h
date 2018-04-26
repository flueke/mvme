#ifndef __A2_MEMORY_H__
#define __A2_MEMORY_H__

#include <cassert>
#include <cstdlib>
#include <exception>
#include <memory>
#include <numeric>
#include <vector>

#include "util/typedefs.h"

namespace memory
{

template<typename T>
inline bool is_aligned(const T *ptr, size_t alignment = alignof(T))
{
    return (((uintptr_t)ptr) % alignment) == 0;
}

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

/* IMPORTANT: Currently doesn't handle allocations larger than the initial
 * segment size.  This could be supported by checking the size required for an
 * allocation that overflows the current segment and then allocating that size
 * plus the desired alignment as padding.
 *
 * TODO:
 * - refactor the allocation code so that the core part exists only once.
 * - avoid the recursion when allocation fails. this can lead to a crash due to
 *   stack overflow.
 * - do not delete the segments in reset(), instead keep track of the current
 *   segment index and increment that in case the current segment overflows.
 *
 *
 * */

class Arena
{
    public:
        explicit Arena(size_t segmentSize)
            : m_segmentSize(segmentSize)
        {
            addSegment(m_segmentSize);
        }

        ~Arena()
        {
            destroyObjects();
        }

        // can't copy
        Arena(Arena &other) = delete;
        Arena &operator=(Arena &other) = delete;

        // can move
        Arena(Arena &&other) = default;
        Arena &operator=(Arena &&other) = default;

        inline size_t free() const
        {
            return currentSegment().free();
        }

        inline size_t used() const
        {
            return std::accumulate(
                m_segments.begin(), m_segments.end(),
                static_cast<size_t>(0),
                [](size_t sum, const Segment &seg) { return sum + seg.used(); });
        }

        inline size_t size() const
        {
            return std::accumulate(
                m_segments.begin(), m_segments.end(),
                static_cast<size_t>(0),
                [](size_t sum, const Segment &seg) { return sum + seg.size; });
        }

        inline void reset()
        {
            destroyObjects();

#if 0
            for (auto &seg: m_segments)
            {
                seg.reset();
            }
#else
            m_segments.clear();
            addSegment(m_segmentSize);
#endif
        }

        /** IMPORTANT: Use for POD types only! It doesn't do any construction
         * nor destruction. */
        template<typename T>
        T *pushStruct(size_t align = alignof(T))
        {
            size_t space = free();
            auto &seg = currentSegment();

            if (std::align(align, sizeof(T), seg.cur, space))
            {
                T *result = reinterpret_cast<T*>(seg.cur);
                seg.cur = reinterpret_cast<u8 *>(seg.cur) + sizeof(T);
                assert(is_aligned(result, align));
                return result;
            }

            addSegment(m_segmentSize);
            return pushStruct<T>(align);
        }

        /** IMPORTANT: Use for arrays of POD types only! It doesn't do any
         * construction nor destruction. */
        template<typename T>
        T *pushArray(size_t size, size_t align = alignof(T))
        {
            size_t space = free();
            auto &seg = currentSegment();

            if (std::align(align, sizeof(T) * size, seg.cur, space))
            {
                T *result = reinterpret_cast<T*>(seg.cur);
                seg.cur = reinterpret_cast<u8 *>(seg.cur) + sizeof(T) * size;
                assert(is_aligned(result, align));
                return result;
            }

            addSegment(m_segmentSize);
            return pushArray<T>(size, align);
        }

        /** Performs pushStruct<T>() and copies the passed in value into the arena. */
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

        /** Push size bytes into the arena. */
        void *pushSize(size_t size, size_t align = 1)
        {
            return reinterpret_cast<void *>(pushArray<u8>(size, align));
        }

        template<typename T>
        T *pushObject(size_t align = alignof(T))
        {
            size_t space = free();
            auto &seg = currentSegment();

            if (std::align(align, sizeof(T), seg.cur, space))
            {
                // Construct the object inside the arena.
                T *result = new (seg.cur) T;
                assert(is_aligned(result, align));

                /* Now push a lambda calling the object destructor onto the
                 * deleters vector. Object construction and vector modification
                 * must be atomic in regards to exceptions: the object must be
                 * destroyed even if the push fails.
                 * To achieve exception safety a unique_ptr with a custom deleter
                 * that only runs the destructor is used to temporarily hold the
                 * object pointer. If the vector push throws the unique_ptr will
                 * properly destroy the object. Otherwise the deleter lambda has
                 * been stored and thus the unique pointer may release() its
                 * pointee. */

                std::unique_ptr<T, detail::destroy_only_deleter<T>> guard_ptr(result);

                // This next call can throw (for example bad_alloc if running OOM).
                m_deleters.emplace_back([result] () {
                    fprintf(stderr, "%s %p\n", __PRETTY_FUNCTION__, result);
                    result->~T();
                });

                // emplace_back() did not throw. It's safe to release the guard now.
                guard_ptr.release();

                seg.cur = reinterpret_cast<u8 *>(seg.cur) + sizeof(T);

                return result;
            }

            addSegment(m_segmentSize);
            return pushObject<T>(align);
        }

        size_t segmentCount() const
        {
            return m_segments.size();
        }

    private:
        struct Segment
        {
            inline size_t free() const
            {
                return (mem.get() + size) - reinterpret_cast<u8 *>(cur);
            }

            inline size_t used() const
            {
                return size - free();
            }

            void reset()
            {
                cur = mem.get();
            }

            std::unique_ptr<u8[]> mem;
            void *cur;
            size_t size;
        };

        Segment &currentSegment()
        {
            assert(!m_segments.empty());
            return m_segments.back();
        }

        const Segment &currentSegment() const
        {
            assert(!m_segments.empty());
            return m_segments.back();
        }

        Segment &addSegment(size_t size)
        {
            Segment segment = {};
            segment.mem     = std::unique_ptr<u8[]>{ new u8[size] };
            segment.cur     = segment.mem.get();
            segment.size    = size;

            m_segments.emplace_back(std::move(segment));

            return currentSegment();
        }

        inline void destroyObjects()
        {
            /* Destroy objects in reverse construction order. */
            for (auto it = m_deleters.rbegin();
                 it != m_deleters.rend();
                 it++)
            {
                (*it)();
            }

            m_deleters.clear();
        }

        using Deleter = std::function<void ()>;

        std::vector<Deleter> m_deleters;
        std::vector<Segment> m_segments;
        size_t m_segmentSize;
};

} // namespace memory

#endif /* __A2_MEMORY_H__ */
