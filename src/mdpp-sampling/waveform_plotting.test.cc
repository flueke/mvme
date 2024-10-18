#include <gtest/gtest.h>

#include "mdpp-sampling/waveform_plotting.h"

using namespace mesytec::mvme;
using mesytec::mvlc::util::span;
using waveforms::WaveformPlotData;
using waveforms::Sample;

static const std::vector<std::pair<double, double>> ConstData =
{
    {0, 1},
    {1, 1},
    {2, 2},
    {3, 3},
    {4, 5},
    {5, 8},
    {6, 13},
    {7, 21},
    {8, 34},
    {9, 55}
};

static const QPointF topLeft{0, 55};
static const QPointF bottomRight{9, 1};
static const QRectF ConstBoundingRect(topLeft, bottomRight);

TEST(WaveformPlotting, WaveformPlotData)
{
    // empty
    {
        using Dest = std::vector<Sample>;

        WaveformPlotData plotData;

        ASSERT_TRUE(plotData.getData().empty());
        ASSERT_FALSE(plotData.boundingRect().isValid());
        ASSERT_TRUE(plotData.copyData<Dest>().empty());
        ASSERT_TRUE(plotData.sample(0).isNull());
    }

    // from span to std::vector
    {
        using Dest = std::vector<Sample>;

        span<const Sample> data(ConstData);
        WaveformPlotData plotData;
        plotData.setData(data);

        ASSERT_EQ(plotData.getData().size(), ConstData.size());
        ASSERT_EQ(plotData.getData().data(), ConstData.data());
        ASSERT_EQ(plotData.boundingRect(), ConstBoundingRect);

        auto copy = plotData.copyData<Dest>();

        ASSERT_TRUE(std::equal(ConstData.begin(), ConstData.end(), copy.begin()));

        ASSERT_EQ(plotData.sample(0), QPointF(0, 1));
        ASSERT_EQ(plotData.sample(9), QPointF(9, 55));
    }

    // from std::vector to std::vector
    {
        using Dest = std::vector<Sample>;

        WaveformPlotData plotData;
        plotData.setData(ConstData);

        ASSERT_EQ(plotData.getData().size(), ConstData.size());
        ASSERT_EQ(plotData.getData().data(), ConstData.data());
        ASSERT_EQ(plotData.boundingRect(), ConstBoundingRect);

        auto copy = plotData.copyData<Dest>();

        ASSERT_TRUE(std::equal(ConstData.begin(), ConstData.end(), copy.begin()));

        ASSERT_EQ(plotData.sample(0), QPointF(0, 1));
        ASSERT_EQ(plotData.sample(9), QPointF(9, 55));
    }

    // from std::vector to QVector
    {
        using Dest = QVector<Sample>;

        WaveformPlotData plotData;
        plotData.setData(ConstData);

        ASSERT_EQ(plotData.getData().size(), ConstData.size());
        ASSERT_EQ(plotData.getData().data(), ConstData.data());
        ASSERT_EQ(plotData.boundingRect(), ConstBoundingRect);

        auto copy = plotData.copyData<Dest>();

        ASSERT_TRUE(std::equal(ConstData.begin(), ConstData.end(), copy.begin()));

        ASSERT_EQ(plotData.sample(0), QPointF(0, 1));
        ASSERT_EQ(plotData.sample(9), QPointF(9, 55));
    }
}
