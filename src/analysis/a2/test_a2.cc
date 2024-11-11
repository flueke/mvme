#include <gtest/gtest.h>

#include "a2.h"

TEST(A2, histo_binning_1_to_1)
{
    a2::Binning binning =
    {
        .min = 0.0,
        .range = 1024.0,
    };

    const size_t BinCount = 1024;
    // This is from a2_adapter histo1d_sink_magic().
    const double BinningFactor =  BinCount / binning.range;

    for (size_t i=0; i<1024; ++i)
    {
        double x = 1.0 * i;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, i);
        //spdlog::info("x={}, theBin={}, binningFactor={}, binning={{.min={}, .range={}}}",
        //    x, theBin, BinningFactor, binning.min, binning.range);
    }

    {
        double x = -1.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Underflow);
    }

    {
        double x = 1024.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Overflow);
    }
}

#if 0
TEST(A2, histo_binning)
{
    const double XMin = -1e+20;
    const double XMax = +1e+10;
    const auto XRange = XMax - XMin;

    a2::Binning binning =
    {
        .min = XMin,
        .range = XRange,
    };

    // From the case that leads to crashes due to negative/out of range bin
    // numbers.
    const size_t BinCount = 1024;
    // This is from a2_adapter histo1d_sink_magic().
    const double BinningFactor =  BinCount / binning.range;

    {
        double x = 0.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_GE(theBin, 0);
        ASSERT_LE(theBin, BinCount);
        spdlog::info("x={}, theBin={}, binningFactor={}, binning={{.min={}, .range={}}}",
            x, theBin, BinningFactor, binning.min, binning.range);
    }

    {
        double x = -2151.8275064437571;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_GE(theBin, 0);
        ASSERT_LE(theBin, BinCount);
        spdlog::info("x={}, theBin={}, binningFactor={}, binning={{.min={}, .range={}}}",
        x, theBin, BinningFactor, binning.min, binning.range);
    }
}
#endif
