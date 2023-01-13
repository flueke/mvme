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
#include "a2.h"

#include <benchmark/benchmark.h>
#include <iostream>
#include <fstream>

using namespace a2;

using std::cout;
using std::endl;
using benchmark::Counter;

#if 1
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
#endif

struct TestEntry
{
    const char *name;
    double p;
    bool expected;
};

static const TestEntry TestData[] =
{
    { "positive", 42.0, true },
    { "negative", 42.0, true },
    { "zero", 0.0, true },
    { "positive_inf", std::numeric_limits<double>::infinity(), true },
    { "negative_inf", -std::numeric_limits<double>::infinity(), true },
    { "quiet_nan", make_quiet_nan(), true },
    { "invalid_param", invalid_param(), false },
    { "very_small", 1e-200, true },
};

auto BM_is_param_valid = [](benchmark::State &state, TestEntry e)
{

    for (auto _: state)
    {
        bool valid = is_param_valid(e.p);
        benchmark::DoNotOptimize(valid);
        assert(valid == e.expected);
    }
};

auto BM_get_payload = [](benchmark::State &state, TestEntry e)
{

    for (auto _: state)
    {
        benchmark::DoNotOptimize(
            get_payload(e.p));
    }
};

int main(int argc, char *argv[])
{
    for (auto &e: TestData)
    {
        std::string name = std::string("IsValid/") + e.name;
        benchmark::RegisterBenchmark(name.c_str(), BM_is_param_valid, e);

    }

    for (auto &e: TestData)
    {
        std::string name = std::string("GetPayload/") + e.name;
        benchmark::RegisterBenchmark(name.c_str(), BM_get_payload, e);
    }

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
}
