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

    bool destroyed;
    static int instance;
    int m_instance = 0;
};

int Foo::instance = 0;

static void TEST_ObjectArena(benchmark::State &state)
{
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

BENCHMARK_MAIN();
