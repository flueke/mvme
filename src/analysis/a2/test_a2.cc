#include <gtest/gtest.h>

#include "a2.h"

TEST(A2, histo_binning_1_to_1_pos_only)
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
        double x = binning.min - 1.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Underflow);
    }

    {
        double x = binning.min + binning.range;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Overflow);
    }
}

TEST(A2, histo_binning_1_to_1_neg_to_pos)
{
    a2::Binning binning =
    {
        .min = -512.0,
        .range = 1024.0,
    };

    const size_t BinCount = 1024;
    // This is from a2_adapter histo1d_sink_magic().
    const double BinningFactor =  BinCount / binning.range;

    for (size_t i=0; i<1024; ++i)
    {
        double x = binning.min + 1.0 * i;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, i);
        //spdlog::info("x={}, theBin={}, binningFactor={}, binning={{.min={}, .range={}}}",
        //    x, theBin, BinningFactor, binning.min, binning.range);
    }

    {
        double x = binning.min - 1.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Underflow);
    }

    {
        double x = binning.min + binning.range;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Overflow);
    }
}

TEST(A2, histo_binning_less_bins)
{
    a2::Binning binning =
    {
        .min = -512.0,
        .range = 1024.0,
    };

    const size_t BinCount = 512;
    // This is from a2_adapter histo1d_sink_magic().
    const double BinningFactor =  BinCount / binning.range;

    size_t expectedBin = 0;

    for (size_t i=0; i<1024; ++i)
    {
        double x = binning.min + 1.0 * i;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, expectedBin);
        //spdlog::info("x={}, theBin={}, binningFactor={}, binning={{.min={}, .range={}}}",
        //    x, theBin, BinningFactor, binning.min, binning.range);

        if (i % 2 == 1)
        {
            ++expectedBin;
        }
    }

    {
        double x = binning.min - 1.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Underflow);
    }

    {
        double x = binning.min + binning.range;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Overflow);
    }
}

TEST(A2, histo_binning_more_bins)
{
    a2::Binning binning =
    {
        .min = -512.0,
        .range = 1024.0,
    };

    const size_t BinCount = 2048;
    // This is from a2_adapter histo1d_sink_magic().
    const double BinningFactor =  BinCount / binning.range;

    size_t expectedBin = 0;

    for (size_t i=0; i<2048; ++i)
    {
        double x = binning.min + 0.5 * i;
        s32 theBin = get_bin(binning, BinCount, x);
        //spdlog::info("x={}, theBin={}, binningFactor={}, binning={{.min={}, .range={}}}",
        //    x, theBin, BinningFactor, binning.min, binning.range);
        ASSERT_EQ(theBin, i);
    }

    {
        double x = binning.min - 1.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Underflow);
    }

    {
        double x = binning.min + binning.range;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Overflow);
    }
}

//TEST(A2, histo_binning_get_bin)
//{
//}

TEST(A2, histo_binning_large_range)
{
    const double v1 = -1e+16;
    const long double v2 = v1 - 1.0l;
    const double v3 = v1 - 1.0l;

    spdlog::info("v1={}, v2={}, delta={}", v1, v2, v1 - v2);
    spdlog::info("v1={}, v3={}, delta={}", v1, v3, v1 - v3);

    // TODO: this is basically the limit we can do with doubles in the analysis.
    // TODO: limit spinboxes and values in general to +-1e15
    ASSERT_EQ(v1, v2);


    a2::Binning binning =
    {
        .min = -1e+16, // FIXME: works up to -1e+15 but -1e+16 fails!
        .range = +1e+20,
    };

    const size_t BinCount = 1024;
    // This is from a2_adapter histo1d_sink_magic().
    const double BinningFactor =  BinCount / binning.range;

    spdlog::info("binning={{.min={}, .range={}}} => binningFactor={}", binning.min, binning.range, BinningFactor);

    #if 0
    size_t expectedBin = 0;

    for (size_t i=0; i<1024; ++i)
    {
        double x = binning.min + 0.5 * i;
        s32 theBin = get_bin(binning, BinCount, x);
        spdlog::info("x={}, theBin={}, binningFactor={}, binning={{.min={}, .range={}}}",
            x, theBin, BinningFactor, binning.min, binning.range);
        ASSERT_EQ(theBin, i);
    }
    #endif

    {
        double x = binning.min - 1.0;
        s32 theBin = get_bin(binning, BinCount, x);
        ASSERT_EQ(theBin, a2::Binning::Underflow);
    }

    {
        double x = binning.min + binning.range;
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
