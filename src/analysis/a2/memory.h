 /* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __A2_MEMORY_H__
#define __A2_MEMORY_H__

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <numeric>
#include <type_traits>
#include <vector>

#ifdef A2_HUGE_PAGES
#include <sys/mman.h>
#endif

#include <spdlog/spdlog.h>

#include "util/typedefs.h"
#include "util/sizes.h"

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

#ifdef A2_HUGE_PAGES

// https://rigtorp.se/hugepages/

template <typename T> struct huge_page_allocator {
  constexpr static std::size_t huge_page_size = 1 << 21; // 2 MiB
  using value_type = T;

  huge_page_allocator() = default;
  template <class U>
  constexpr huge_page_allocator(const huge_page_allocator<U> &) noexcept {}

  size_t round_to_huge_page_size(size_t n) {
    return (((n - 1) / huge_page_size) + 1) * huge_page_size;
  }

  T *allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    spdlog::debug("huge page alloc: size={}, rounded_to_page_size={}", n, round_to_huge_page_size(n * sizeof(T)));
    auto p = static_cast<T *>(mmap(
        nullptr, round_to_huge_page_size(n * sizeof(T)), PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
    if (p == MAP_FAILED) {
        spdlog::error("mmap failed: {}", strerror(errno));
      throw std::bad_alloc();
    }
    return p;
  }

  void deallocate(T *p, std::size_t n) {
    spdlog::debug("deallocate huge page: size={}, rounded_to_page_size={}", n, round_to_huge_page_size(n * sizeof(T)));
    munmap(p, round_to_huge_page_size(n * sizeof(T)));
  }
};

template<typename T>
void huge_page_allocator_deleter(huge_page_allocator<T> alloc, T *ptr, std::size_t size)
{
    alloc.deallocate(ptr, size);
}

#endif // A2_HUGE_PAGES

} // namespace detail

class Arena
{
    public:
        explicit Arena(size_t segmentSize = a2::Megabytes(2))
            : m_segmentSize(segmentSize)
            , m_currentSegmentIndex(0)
        {
            addSegment(m_segmentSize);
        }

        ~Arena()
        {
            destroyObjects();
        }

        // can't copy
        Arena(const Arena &other) = delete;
        Arena &operator=(const Arena &other) = delete;

        // can move
        Arena(Arena &&other) = default;
        Arena &operator=(Arena &&other) = default;

        /** Total space used. */
        inline size_t used() const
        {
            return std::accumulate(
                m_segments.begin(), m_segments.end(),
                static_cast<size_t>(0),
                [](size_t sum, const Segment &seg) { return sum + seg.used(); });
        }

        /** Sum of all segment sizes. */
        inline size_t size() const
        {
            return std::accumulate(
                m_segments.begin(), m_segments.end(),
                static_cast<size_t>(0),
                [](size_t sum, const Segment &seg) { return sum + seg.size; });
        }

        /** Destroys objects created via pushObject() and clears all segments.
         * Does not deallocate segments. */
        inline void reset()
        {
            destroyObjects();

            for (auto &seg: m_segments)
            {
                seg.reset();
            }

            m_currentSegmentIndex = 0;
        }

        /** Push size bytes into the arena. */
        inline void *pushSize(size_t size, size_t align = 1)
        {
            return pushSize_impl(size, align);
        }

        /** IMPORTANT: Use for POD types only! It doesn't do construction nor
         * destruction. */
        template<typename T>
        T *pushStruct(size_t align = alignof(T))
        {
            static_assert(std::is_trivial<T>::value, "T must be a trivial type");

            return reinterpret_cast<T *>(pushSize(sizeof(T), align));
        }

        /** IMPORTANT: Use for arrays of POD types only! It doesn't do
         * construction nor destruction. */
        template<typename T>
        T *pushArray(size_t size, size_t align = alignof(T))
        {
            static_assert(std::is_trivial<T>::value, "T must be a trivial type");

            return reinterpret_cast<T *>(pushSize(size * sizeof(T), align));
        }

        char *pushCString(const char *str)
        {
            size_t size = std::strlen(str) + 1;
            void *mem = pushSize(size);
            std::memcpy(mem, str, size);
            return reinterpret_cast<char *>(mem);
        }

        /** Performs pushStruct<T>() and copies the passed in value into the
         * arena. */
        template<typename T>
        T *push(const T &t, size_t align = alignof(T))
        {
            T *result = pushStruct<T>(align);
            *result = t;
            return result;
        }

        /* Construct an object of type T inside the arena. The object will be
         * properly deconstructed on resetting or destroying the arena. */
        template<typename T>
        T *pushObject(size_t align = alignof(T))
        {
            /* Get memory and construct the object using placement new. */
            void *mem = pushSize(sizeof(T), align);
            T *result = new (mem) T;

            /* Now push a lambda calling the object destructor onto the
             * deleters vector.
             * To achieve exception safety a unique_ptr with a custom deleter
             * that only runs the destructor is used to temporarily hold the
             * object pointer. If the vector operation throws the unique_ptr
             * will properly destroy the object. Otherwise the deleter lambda
             * has been stored and thus the unique pointer may release() its
             * pointee. Note that in case of an exception the space for T has
             * already been allocated inside the arena and will not be
             * reclaimed. */
            std::unique_ptr<T, detail::destroy_only_deleter<T>> guard_ptr(result);

            m_deleters.emplace_back([result] () {
                //fprintf(stderr, "%s %p\n", __PRETTY_FUNCTION__, result);
                result->~T();
            });

            /* emplace_back() did not throw. It's safe to release the guard now. */
            guard_ptr.release();

            return result;
        }

        /* Construct an object of type T using the forwarded Args inside the arena. The
         * object will be properly deconstructed on resetting or destroying the arena. */
        template<typename T, typename... Args>
        T *pushObject(Args &&... args)
        {
            /* Get memory and construct the object using placement new. */
            void *mem = pushSize(sizeof(T), alignof(T));
            T *result = new (mem) T(std::forward<Args>(args)...);

            //fprintf(stderr, "%s@%p: constructed result %p from forwarded args\n",
            //        __PRETTY_FUNCTION__, this, result);

            std::unique_ptr<T, detail::destroy_only_deleter<T>> guard_ptr(result);

            m_deleters.emplace_back([result, this] () {
                //fprintf(stderr, "%s deleter for %p (arena=%p)\n", __PRETTY_FUNCTION__, result, this);
                result->~T();
            });

            /* emplace_back() did not throw. It's safe to release the guard now. */
            guard_ptr.release();

            return result;
        }

        inline size_t segmentCount() const
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

            using DeleterFunc = std::function<void (u8 *)>;
            std::unique_ptr<u8[], DeleterFunc> mem;
            void *cur;
            size_t size;
        };

        Segment &currentSegment()
        {
            assert(m_currentSegmentIndex < m_segments.size());

            return m_segments[m_currentSegmentIndex];
        }

        const Segment &currentSegment() const
        {
            assert(m_currentSegmentIndex < m_segments.size());

            return m_segments[m_currentSegmentIndex];
        }

        #ifdef A2_HUGE_PAGES
        void addSegment(size_t size)
        {
            detail::huge_page_allocator<u8> alloc;

            auto roundedSize = alloc.round_to_huge_page_size(size);

            auto deleter = [=] (u8 *ptr)
            {
                detail::huge_page_allocator_deleter(alloc, ptr, roundedSize);
            };

            Segment segment = {};
            segment.mem     = std::unique_ptr<u8[], Segment::DeleterFunc>{ alloc.allocate(roundedSize), deleter};
            segment.cur     = segment.mem.get();
            segment.size    = size;

            m_segments.emplace_back(std::move(segment));

            //fprintf(stderr, "%s: added segment of size %u, segmentCount=%u\n",
            //        __PRETTY_FUNCTION__, (u32)size, (u32)segmentCount());
        }
        #else
        void addSegment(size_t size)
        {
            Segment segment = {};
            segment.mem     = std::unique_ptr<u8[]>{ new u8[size] };
            segment.cur     = segment.mem.get();
            segment.size    = size;

            m_segments.emplace_back(std::move(segment));

            //fprintf(stderr, "%s: added segment of size %u, segmentCount=%u\n",
            //        __PRETTY_FUNCTION__, (u32)size, (u32)segmentCount());
        }
        #endif

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

        /*
         * Check each segment from the current one to the last. If the std::align()
         * call succeeds use that segment and return the pointer.
         *
         * Otherwise add a new segment that's large enough to handle the
         * requested size including alignment. Now the std::align() call must
         * succeed.
         *
         * If the system runs OOM the call to addSegment() will throw a
         * bad_alloc and we're done.
         */

        inline void *pushSize_impl(size_t size, size_t align)
        {
            //fprintf(stderr, "%s: size=%lu, align=%lu\n",
            //        __FUNCTION__, (u64)size, (u64)align);

            assert(m_currentSegmentIndex < segmentCount());

            for (; m_currentSegmentIndex < segmentCount(); m_currentSegmentIndex++)
            {
                auto &seg = m_segments[m_currentSegmentIndex];
                size_t space = seg.free();

                if (std::align(align, size, seg.cur, space))
                {
                    void *result = seg.cur;
                    seg.cur = reinterpret_cast<u8 *>(seg.cur) + size;
                    assert(is_aligned(result, align));
                    return result;
                }
            }

            assert(m_currentSegmentIndex == segmentCount());

            // Point to the last valid segment to stay consistent in case addSegment() throws
            m_currentSegmentIndex--;

            // This amount should guarantee that std::align() succeeds.
            size_t sizeNeeded = size + align;

            // this can throw bad_alloc
            addSegment(sizeNeeded > m_segmentSize ? sizeNeeded : m_segmentSize);

            m_currentSegmentIndex++;

            auto &seg = currentSegment();
            size_t space = seg.free();

            if (std::align(align, size, seg.cur, space))
            {
                void *result = seg.cur;
                seg.cur = reinterpret_cast<u8 *>(seg.cur) + size;
                assert(is_aligned(result, align));
                return result;
            }

            assert(false);
            return nullptr;
        }

        using Deleter = std::function<void ()>;

        std::vector<Deleter> m_deleters;
        std::vector<Segment> m_segments;
        size_t m_segmentSize;
        size_t m_currentSegmentIndex;
};

/* Minimal Allocator requirements implementation using an Arena for allocation.
 * https://en.cppreference.com/w/cpp/named_req/Allocator
 *
 * Does not perform any actual deallocation. This means having containers realloc
 * internally will lead to holes in the arena memory segments. This for example happens if
 * you push elements onto a vector without having reserved memory upfront: the vector
 * implementation will usually allocate space for N initial elements and on realloc double
 * that amount moving any existing elements into the new memory.
 */

template<typename T>
struct ArenaAllocator
{
    using value_type = T;

    Arena *arena;

    explicit ArenaAllocator(Arena *arena)
        : arena(arena)
    {}

    template<typename U> ArenaAllocator(const ArenaAllocator<U> &other) noexcept
        : arena(other.arena)
    {}

    T *allocate(std::size_t n)
    {

        auto result = reinterpret_cast<T *>(arena->pushSize(n * sizeof(T), alignof(T)));

        //fprintf(stderr, "%s (%p): n=%lu, result=%p\n", __PRETTY_FUNCTION__, this, n, result);

        return result;
    }

    void deallocate(T* ptr, std::size_t n) noexcept
    {
        (void) ptr;
        (void) n;
        /* noop
         *
         * Note: deallocate() could handle the case where the memory block to be
         * deallocated is at the end of the current memory segment but that's not
         * implemented right now.
         */

        //fprintf(stderr, "%s (%p): ptr=%p, n=%lu\n", __PRETTY_FUNCTION__, this, ptr, n);
    }
};

template<typename T, typename U>
bool operator==(const ArenaAllocator<T> &a, const ArenaAllocator<U> &b)
{
    return a.arena == b.arena;
}

template<typename T, typename U>
bool operator!=(const ArenaAllocator<T> &a, const ArenaAllocator<U> &b)
{
    return !(a == b);
}

} // namespace memory

#endif /* __A2_MEMORY_H__ */
