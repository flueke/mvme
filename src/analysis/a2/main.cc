#include <cassert>
#include "multiword_datafilter.h"
#include "memory.h"
#include "util/nan.h"
#include "util/sizes.h"

#if 1
#include <cstdio>
#include <iostream>
#include "a2.cc"

using std::cout;
using std::endl;

static const u32 testdata[] =
{
    0xbb00,
    0xaffe,
    0x1234,
    0xee00,
};

int main(int argc, char *argv[])
{
    using namespace memory;

    Arena arena(Kilobytes(256));

    auto a2 = arena.push<A2>({});


    int eventIndex = 0;
    int moduleIndex = 0;
    int event_iterations = 1;
    int data_iterations = 1;

    a2_begin_event(a2, eventIndex);

    for (int ei = 0; ei < event_iterations; ++ei)
    {
        for (int di = 0; di < data_iterations; ++di)
        {
            a2_process_module_data(a2, eventIndex, moduleIndex, testdata, sizeof(testdata) / sizeof(*testdata));
        }
    }

    a2_end_event(a2, eventIndex);

    return 0;
}
#endif
