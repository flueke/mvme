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

TEST(A2, histo_binning_large_range)
{
    const a2::Binning binning =
    {
        .min = -1e+16,
        .range = +1e+20,
    };

    // The limit for doubles is +-1e+15, after that the precision is lost.
    ASSERT_EQ(binning.min, binning.min - 1.0);

    const size_t BinCount = 1024;
    // This is from a2_adapter histo1d_sink_magic().
    const double BinningFactor =  BinCount / binning.range;

    //spdlog::info("binning={{.min={}, .range={}}} => binningFactor={}", binning.min, binning.range, BinningFactor);

    const double epsilon = 0.00001;
    const auto xs = { binning.min - 1e10, binning.min - 1e5, binning.min - 1e1, binning.min, binning.min + 1e1, binning.min + 1e5, binning.min + 1e10, binning.min + binning.range, binning.min + binning.range + 1e5, binning.min + binning.range + 1e10 };

    for (double x: xs)
    {
        s32 theBin = get_bin(binning, BinCount, x);

        // the slightly slower variant calculating the BinningFactor each time
        double theBinUnchecked = a2::get_bin_unchecked(binning, BinCount, x);

        // slightly faster variant taking the precalculated BinningFactor
        double theBinUnchecked2 = a2::get_bin_unchecked(x, binning.min, BinningFactor);

        //spdlog::info("x={}, theBin={}, theBinUnchecked={}, theBinUnchecked2={}, binningFactor={}, binning={{.min={}, .range={}}}",
        //    x, theBin, theBinUnchecked, theBinUnchecked2, BinningFactor, binning.min, binning.range);

        ASSERT_NEAR(theBinUnchecked, theBinUnchecked2, epsilon);

        if (theBin != a2::Binning::Underflow && theBin != a2::Binning::Overflow)
        {
            ASSERT_GE(theBinUnchecked, 0.0);
            ASSERT_LT(theBinUnchecked, BinCount);

            ASSERT_GE(theBinUnchecked2, 0.0);
            ASSERT_LT(theBinUnchecked2, BinCount);
        }
        else if (theBin == a2::Binning::Underflow)
        {
            ASSERT_LT(theBinUnchecked, 0.0);
            ASSERT_LT(theBinUnchecked2, 0.0);
        }
        else if (theBin == a2::Binning::Overflow)
        {
            ASSERT_GE(theBinUnchecked, BinCount);
            ASSERT_GE(theBinUnchecked2, BinCount);
        }
    }
}
