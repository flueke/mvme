#include <gtest/gtest.h>
#include <iostream>

#include "memory.h"

using std::cout;
using std::endl;

TEST(Arena, Basic)
{
    memory::Arena arena(1024);

    ASSERT_EQ(arena.size(), 1024);
    ASSERT_EQ(arena.used(), 0);
    ASSERT_EQ(arena.segmentCount(), 1);

    auto p = arena.pushStruct<int>();
    ASSERT_EQ(arena.size(), 1024);
    ASSERT_EQ(arena.used(), sizeof(int));
    ASSERT_EQ(arena.segmentCount(), 1);
    ASSERT_EQ(*p, 0);

    arena.reset();
    ASSERT_EQ(arena.size(), 1024);
    ASSERT_EQ(arena.used(), 0);
    ASSERT_EQ(arena.segmentCount(), 1);
}

TEST(Arena, Grow)
{
    memory::Arena arena(1024);

    ASSERT_EQ(arena.size(), 1024);
    ASSERT_EQ(arena.used(), 0);
    ASSERT_EQ(arena.segmentCount(), 1);

    auto p = arena.pushArray<int>(1024);
    ASSERT_GE(arena.size(), sizeof(int) * 1024);
    ASSERT_EQ(arena.used(), sizeof(int) * 1024);
    ASSERT_GE(arena.segmentCount(), 2);
    ASSERT_TRUE(std::all_of(p, p + 1024, [](int i) { return i == 0; }));

    arena.reset();
    ASSERT_GE(arena.size(), sizeof(int) * 1024);
    ASSERT_EQ(arena.used(), 0);
    ASSERT_GE(arena.segmentCount(), 2);
    ASSERT_EQ(*p, 0);
}

// Note: cannot run the following tests multi-threaded because of the static
// variables!
struct Foo
{
    Foo()
    {
        m_instance = created++;
        m_destroyed = false;
        //cout << __PRETTY_FUNCTION__ << " " << m_instance << " " << this << endl;
    }

    ~Foo()
    {
        //cout << __PRETTY_FUNCTION__ << " " << m_instance << " " << this << endl;
        m_destroyed = true;
        ++destroyed;
    }

    static size_t created;
    static size_t destroyed;

    int m_instance = 0;
    volatile bool m_destroyed;
};

size_t Foo::created = 0;
size_t Foo::destroyed = 0;

TEST(Arena, Object)
{

    Foo::created = 0;
    Foo::destroyed = 0;
    Foo *rawFoo = nullptr;

    {
        memory::Arena arena(1024);
        rawFoo = arena.pushObject<Foo>();

        ASSERT_EQ(arena.size(), 1024);
        ASSERT_EQ(arena.used(), sizeof(Foo));
        ASSERT_EQ(arena.segmentCount(), 1);
        ASSERT_EQ(rawFoo->m_instance, 0);
        ASSERT_FALSE(rawFoo->m_destroyed);
        ASSERT_EQ(Foo::created, 1u);
        ASSERT_EQ(Foo::destroyed, 0u);
    }

    ASSERT_TRUE(rawFoo->destroyed);
    ASSERT_EQ(Foo::created, Foo::destroyed);
}

TEST(Arena, Objects)
{
    Foo::created = 0;
    Foo::destroyed = 0;
    Foo *rawFoo = nullptr;

    {
        memory::Arena arena(1024);

        for (size_t i=0; i<10; i++)
        {
            rawFoo = arena.pushObject<Foo>();
            ASSERT_EQ(rawFoo->m_instance, i);
            ASSERT_FALSE(rawFoo->m_destroyed);
            ASSERT_EQ(Foo::created, i+1);
            ASSERT_EQ(Foo::destroyed, 0u);
        }
    }

    ASSERT_TRUE(rawFoo->destroyed);
    ASSERT_EQ(Foo::created, 10);
    ASSERT_EQ(Foo::created, Foo::destroyed);
}

TEST(Arena, Allocator)
{
    using memory::Arena;
    using memory::ArenaAllocator;

    Arena arena(sizeof(int) * 10);

    {

        std::vector<int, ArenaAllocator<int>> vec_pod((ArenaAllocator<int>(&arena)));

        vec_pod.reserve(10);

        for (int i = 0; i < 10; i++)
            vec_pod.push_back(i);

        #if 0
        cout << "POD vector:" << endl;
        cout << "  vec_pod.size()=" << vec_pod.size()
            << ", vec_pod.capacity()=" << vec_pod.capacity()
            << ", arena.used()=" << arena.used()
            << ", arena.size()=" << arena.size()
            << ", arena.segmentCount()=" << arena.segmentCount()
            << endl
            ;
        #endif

        ASSERT_EQ(arena.used(), sizeof(int) * 10);
    }

    // The vec_pod variable went out of scope, but ArenaAllocator::deallocate()
    // does nothing and so the memory is still in use.
    ASSERT_EQ(arena.used(), sizeof(int) * 10);

    arena.reset();
    ASSERT_GE(arena.size(), sizeof(int) * 10);
    ASSERT_EQ(arena.used(), 0);
}
