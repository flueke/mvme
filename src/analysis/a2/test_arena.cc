#include "memory.h"

#include <benchmark/benchmark.h>
#include <iostream>

using std::cout;
using std::endl;
using benchmark::Counter;

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

struct Foo
{
    Foo()
    {
        m_instance = instance++;
        cout << __PRETTY_FUNCTION__ << " " << m_instance << " " << this << endl;
        destroyed = false;
    }

    ~Foo()
    {
        cout << __PRETTY_FUNCTION__ << " " << m_instance << " " << this << endl;
        destroyed = true;
        benchmark::DoNotOptimize(destroyed);
    }

    Foo(const Foo &other)
        : destroyed(other.destroyed)
        , m_instance(other.m_instance)
    {
        cout << __PRETTY_FUNCTION__ << " " << m_instance << " " << this << endl;
    }

    Foo &operator=(const Foo &other)
    {
        cout << __PRETTY_FUNCTION__ << " " << m_instance << " " << this << endl;

        this->destroyed = other.destroyed;
        this->m_instance = other.m_instance;

        return *this;
    }

    bool destroyed;
    static int instance;
    int m_instance = 0;
};

int Foo::instance = 0;

static void TEST_ObjectArena(benchmark::State &state)
{
    (void) state;
    using memory::Arena;

    cout << endl << "sizeof(Foo)=" << sizeof(Foo) << ", alignof(Foo)=" << alignof(Foo) << endl << endl;

    //while (state.KeepRunning())
    {
        Foo *rawFoo = nullptr;

        {
            Arena arena(16);

            for (int i =0; i < 10; i++)
                rawFoo = arena.pushObject<Foo>();

            benchmark::DoNotOptimize(rawFoo);
            benchmark::DoNotOptimize(rawFoo->destroyed);

            assert(rawFoo && !rawFoo->destroyed);
        }

        assert(rawFoo && rawFoo->destroyed);
    }
}
BENCHMARK(TEST_ObjectArena);

static void TEST_ArenaAllocator(benchmark::State &state)
{
    (void) state;
    using memory::Arena;
    using memory::ArenaAllocator;

    cout << "int" << endl;

    {
        Arena arena(sizeof(int) * 10);

        std::vector<int, ArenaAllocator<int>> vec_pod((ArenaAllocator<int>(&arena)));

        vec_pod.reserve(10);

        for (int i = 0; i < 10; i++)
            vec_pod.push_back(i);

        cout << "POD vector:" << endl;
        cout << "  vec_pod.size()=" << vec_pod.size()
            << ", vec_pod.capacity()=" << vec_pod.capacity()
            << ", arena.used()=" << arena.used()
            << ", arena.size()=" << arena.size()
            << ", arena.segmentCount()=" << arena.segmentCount()
            << endl
            ;

        assert(arena.used() == vec_pod.size() * sizeof(int));
    }

    cout << "Foo" << endl;

    {
        Arena arena(sizeof(Foo) * 10);

        std::vector<Foo, ArenaAllocator<Foo>> vec_foo((ArenaAllocator<Foo>(&arena)));

        vec_foo.reserve(10);

        for (int i = 0; i < 10; i++)
            vec_foo.push_back(Foo());

        cout << "POD vector:" << endl;
        cout << "  vec_foo.size()=" << vec_foo.size()
            << ", vec_foo.capacity()=" << vec_foo.capacity()
            << ", arena.used()=" << arena.used()
            << ", arena.size()=" << arena.size()
            << ", arena.segmentCount()=" << arena.segmentCount()
            << endl
            ;

        assert(arena.used() == vec_foo.size() * sizeof(Foo));
    }
}
BENCHMARK(TEST_ArenaAllocator);

BENCHMARK_MAIN();
