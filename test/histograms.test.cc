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

TEST(Histograms, Histo1DGetCountsInRange)
{
    Histo1D histo(10, 0.0, 10.0);

    histo.fill(1.0);
    histo.fill(2.0);
    histo.fill(3.0);
    histo.fill(3.0);

    // value 0 1 1 2 0 0 0 0 0 0
    // start 0 1 2 3 4 5 6 7 8 9
    // bin   0 1 2 3 4 5 6 7 8 9

    ASSERT_DOUBLE_EQ(histo.getBinContent(0), 0.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(1), 1.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(2), 1.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(3), 2.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(4), 0.0);

    // range is one bin wide
    ASSERT_DOUBLE_EQ(histo.getCounts(0.0, 1.0), 0.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(1.0, 2.0), 1.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(2.0, 3.0), 1.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(3.0, 4.0), 2.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(4.0, 5.0), 0.0);

    // range is smaller than a bin
    ASSERT_DOUBLE_EQ(histo.getCounts(0.2, 0.8), 0.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(1.2, 1.8), 0.6);
    ASSERT_DOUBLE_EQ(histo.getCounts(2.2, 2.8), 0.6);
    ASSERT_DOUBLE_EQ(histo.getCounts(3.2, 3.8), 1.2);

    // range crosses multiple bins
    ASSERT_DOUBLE_EQ(histo.getCounts(0.0, 2.0), 1.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(1.0, 3.0), 2.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(2.0, 4.0), 3.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(3.0, 5.0), 2.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(4.0, 6.0), 0.0);

    // multiple bins with fractional parts
    ASSERT_DOUBLE_EQ(histo.getCounts(1.2, 2.8), 0.8 + 0.8);
    ASSERT_DOUBLE_EQ(histo.getCounts(1.2, 3.8), 0.8 + 1.0 + 1.6);
}

TEST(Histograms, Histo1DNegativeAxisGetCountsInRange)
{
    const double epsilon = 0.0000001;
    Histo1D histo(10, -10.0, 0.0);

    histo.fill(-9.0);
    histo.fill(-8.0);
    histo.fill(-7.0);
    histo.fill(-7.0);

    // value   0  1  1  2  0  0  0  0  0  0
    // start -10 -9 -8 -7 -6 -5 -4 -3 -2 -1
    // bin     0  1  2  3  4  5  6  7  8  9

    ASSERT_DOUBLE_EQ(histo.getBinContent(0), 0.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(1), 1.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(2), 1.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(3), 2.0);
    ASSERT_DOUBLE_EQ(histo.getBinContent(4), 0.0);

    // range is one bin wide
    ASSERT_DOUBLE_EQ(histo.getCounts(-10.0, -9.0), 0.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(-9.0,  -8.0), 1.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(-8.0,  -7.0), 1.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(-7.0,  -6.0), 2.0);
    ASSERT_DOUBLE_EQ(histo.getCounts(-6.0,  -5.0), 0.0);

    // range is smaller than a bin
    ASSERT_NEAR(histo.getCounts(-9.8, -9.2), 0.0, epsilon);
    ASSERT_NEAR(histo.getCounts(-8.8, -8.2), 0.6, epsilon);
    ASSERT_NEAR(histo.getCounts(-7.8, -7.2), 0.6, epsilon);
    ASSERT_NEAR(histo.getCounts(-6.8, -6.2), 1.2, epsilon);

    // range crosses multiple bins
    ASSERT_DOUBLE_EQ(histo.getCounts(-10.0, -8.0), 1.0);
    ASSERT_DOUBLE_EQ(histo.getCounts( -9.0, -7.0), 2.0);
    ASSERT_DOUBLE_EQ(histo.getCounts( -8.0, -6.0), 3.0);
    ASSERT_DOUBLE_EQ(histo.getCounts( -7.0, -5.0), 2.0);
    ASSERT_DOUBLE_EQ(histo.getCounts( -6.0, -4.0), 0.0);

    // multiple bins with fractional parts
    ASSERT_NEAR(histo.getCounts(-8.8, -8.0), 0.8, epsilon);
    ASSERT_NEAR(histo.getCounts(-8.8, -7.0), 0.8 + 1.0, epsilon);
    ASSERT_NEAR(histo.getCounts(-8.8, -6.0), 0.8 + 1.0 + 2.0, epsilon);
    ASSERT_NEAR(histo.getCounts(-8.8, -6.2), 0.8 + 1.0 + 0.8 * 2.0, epsilon);
}

TEST(Histograms, AddHistograms)
{
    Histo1D histo1(10, 0.0, 10.0);
    Histo1D histo2(20, 5.0, 20.0);

    histo1.fill(1.0);
    histo1.fill(2.0);
    histo1.fill(2.0);
    histo1.fill(3.0);

    histo2.fill(1.0);
    histo2.fill(2.0);
    histo2.fill(2.0);
    histo2.fill(3.0);

    auto result = add(histo1, histo2);
}
