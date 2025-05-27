#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "histo1d.h"

TEST(Histograms, AxisBinningGetBin)
{
    AxisBinning binning(10, 0.0, 100.0);

    ASSERT_EQ(binning.getBinWidth(), 10.0);

    ASSERT_EQ(binning.getBinLowEdge(0), 0.0);
    ASSERT_EQ(binning.getBinLowEdge(1), 10.0);
    ASSERT_EQ(binning.getBinLowEdge(9), 90.0);

    // getBin() with valid x values
    ASSERT_EQ(binning.getBin(0.0), 0);
    ASSERT_EQ(binning.getBin(5.0), 0);
    ASSERT_EQ(binning.getBin(10.0), 1);

    // getBin() with out of range x values
    ASSERT_EQ(binning.getBin(-1.0), AxisBinning::Underflow);
    ASSERT_EQ(binning.getBin(101.0), AxisBinning::Overflow);

    // unchecked, in range, no resolution reduction
    ASSERT_EQ(binning.getBinUnchecked(0.0), 0.0);
    ASSERT_EQ(binning.getBinUnchecked(2.5), 0.25);
    ASSERT_EQ(binning.getBinUnchecked(5.0), 0.5);
    ASSERT_EQ(binning.getBinUnchecked(7.5), 0.75);

    // unchecked, out-of-range
    ASSERT_EQ(binning.getBinUnchecked(-1.0), -0.1);
    ASSERT_EQ(binning.getBinUnchecked(101.0), 10.1);
}

TEST(Histograms, Histo1DGetValueRange)
{
    Histo1D histo(10, 0.0, 10.0);

    auto binning = histo.getAxisBinning(Qt::XAxis);
    ASSERT_EQ(binning.getBinWidth(), 1.0);

    histo.fill(1.0);

    ASSERT_EQ(histo.getBinContent(0), 0.0);
    ASSERT_EQ(histo.getBinContent(1), 1.0);

    ASSERT_EQ(histo.getCounts(0.0, 1.0), 0.0);
    ASSERT_EQ(histo.getCounts(1.0, 2.0), 1.0);
    ASSERT_EQ(histo.getCounts(1.5, 2.0), 0.5);

}
