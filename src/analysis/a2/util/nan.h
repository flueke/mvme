#ifndef __A2_NAN_H__
#define __A2_NAN_H__

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "typedefs.h"

namespace a2
{

inline double make_quiet_nan()
{
    double result = std::numeric_limits<double>::quiet_NaN();
    assert(((uintptr_t)(result) & 0xffffffff) == 0);
    return result;
}

inline double make_nan(u32 payload)
{
    double result = make_quiet_nan();
    memcpy(reinterpret_cast<char *>(&result),
           &payload, sizeof(payload));
    return result;
}

inline u32 get_payload(double d)
{
    u32 result;
    memcpy(&result,
           reinterpret_cast<char *>(&d),
           sizeof(result));
    return result;
}

inline constexpr double inf()
{
    return std::numeric_limits<double>::infinity();
}

}

#if 0
#include <cstdio>
void print_double(double d)
{
    u64 du;
    memcpy(&du, &d, sizeof(d));
    printf("%lf 0x%lx", d, du);

    if (std::isnan(d))
    {
        printf(", payload = 0x%08x\n", get_payload(d));
    }
    else
    {
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    print_double(1.0);
    print_double(2.0);
    print_double(1e10);
    print_double(make_quiet_nan());
    print_double(make_nan(1));
    print_double(make_nan(0xffff));
    print_double(make_nan(0x12345678));

    return 0;
}
#endif

#endif /* __A2_NAN_H__ */
