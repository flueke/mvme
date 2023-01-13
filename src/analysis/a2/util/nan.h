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
