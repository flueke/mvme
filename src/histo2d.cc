#include "histo2d.h"

Histo2D::Histo2D(u32 xBins, double xMin, double xMax,
                 u32 yBins, double yMin, double yMax,
                 QObject *parent)
    : QObject(parent)
    , m_data(new double[xBins * yBins])
{
    m_axisBinnings[Qt::XAxis] = AxisBinning(xBins, xMin, xMax);
    m_axisBinnings[Qt::YAxis] = AxisBinning(yBins, yMin, yMax);
    clear();
}

Histo2D::~Histo2D()
{
    delete[] m_data;
}

void Histo2D::resize(s32 xBins, s32 yBins)
{
    Q_ASSERT(xBins > 0 && yBins > 0);

    if (xBins * yBins != m_axisBinnings[Qt::XAxis].getBins() * m_axisBinnings[Qt::YAxis].getBins())
    {
        // Reallocate memory for the new size
        delete[] m_data;
        try
        {
            m_data = new double[xBins * yBins];
        }
        catch (const std::bad_alloc &)
        {
            m_data = nullptr;
            throw;
        }
    }

    // Always update the axis binnings even if the size stayed the same as
    // individual axis resolutions might still have changed, e.g.
    // 10bit:10bit -> 9bit:11bit
    m_axisBinnings[Qt::XAxis].setBins(xBins);
    m_axisBinnings[Qt::YAxis].setBins(yBins);
    clear();
}

void Histo2D::fill(double x, double y, double weight)
{
    s64 xBin = m_axisBinnings[Qt::XAxis].getBin(x);
    s64 yBin = m_axisBinnings[Qt::YAxis].getBin(y);

    if (xBin == AxisBinning::Underflow || yBin == AxisBinning::Underflow)
    {
        m_underflow += weight;
    }
    else if (xBin == AxisBinning::Overflow || yBin == AxisBinning::Overflow)
    {
        m_overflow += weight;
    }
    else
    {
        u32 linearBin = yBin * m_axisBinnings[Qt::XAxis].getBins() + xBin;
        m_data[linearBin] += weight;
        double newValue = m_data[linearBin];

        if (newValue > m_stats.maxValue)
        {
            m_stats.maxValue = newValue;
            m_stats.maxBinX  = xBin;
            m_stats.maxBinY  = yBin;
            m_stats.maxX = m_axisBinnings[Qt::XAxis].getBinLowEdge(xBin);
            m_stats.maxY = m_axisBinnings[Qt::YAxis].getBinLowEdge(yBin);
        }
        m_stats.entryCount += weight;
    }
}

double Histo2D::getValue(double x, double y) const
{
    s64 xBin = m_axisBinnings[Qt::XAxis].getBin(x);
    s64 yBin = m_axisBinnings[Qt::YAxis].getBin(y);

    if (xBin < 0 || yBin < 0)
        return 0.0;

    u32 linearBin = yBin * m_axisBinnings[Qt::XAxis].getBins() + xBin;
    return m_data[linearBin];
}

void Histo2D::clear()
{
    size_t maxBin = m_axisBinnings[Qt::XAxis].getBins() * m_axisBinnings[Qt::YAxis].getBins();
    for (size_t bin = 0; bin < maxBin; ++bin)
    {
        m_data[bin] = 0.0;
    }

    m_underflow = 0.0;
    m_overflow = 0.0;
    m_stats = {};
}

void Histo2D::debugDump() const
{
    qDebug() << "Histo2D" << this << objectName();
    auto xAxis = m_axisBinnings[Qt::XAxis];
    qDebug() << "X-Axis: bins =" << xAxis.getBins()
        << ", min =" << xAxis.getMin()
        << ", max =" << xAxis.getMax();
    qDebug() << "  underflow =" << m_underflow << ", overflow =" << m_overflow;

#if 0
    for (s32 y = 0;
         y < m_yAxis.getBins();
         ++y)
    {
        for (s32 x = 0;
             x < m_xAxis.getBins();
             ++x)
    }
#endif
}

AxisInterval Histo2D::getInterval(Qt::Axis axis) const
{
    AxisInterval result = {};
    if (axis == Qt::XAxis)
    {
        result.minValue = m_axisBinnings[Qt::XAxis].getMin();
        result.maxValue = m_axisBinnings[Qt::XAxis].getMax();
    }
    else if (axis == Qt::YAxis)
    {
        result.minValue = m_axisBinnings[Qt::YAxis].getMin();
        result.maxValue = m_axisBinnings[Qt::YAxis].getMax();
    }
    else if (axis == Qt::ZAxis)
    {
        result.minValue = 0.0;
        result.maxValue = m_stats.maxValue;
    }

    return result;
}

Histo2DStatistics Histo2D::calcStatistics(AxisInterval xInterval, AxisInterval yInterval) const
{
    if (xInterval == getInterval(Qt::XAxis)
        && yInterval == getInterval(Qt::YAxis))
    {
        // global range for both intervals, return global stats
        return m_stats;
    }

    Histo2DStatistics result;

    // x
    s64 xMinBin = m_axisBinnings[Qt::XAxis].getBin(xInterval.minValue);
    s64 xMaxBin = m_axisBinnings[Qt::XAxis].getBin(xInterval.maxValue);

    if (xMinBin < 0)
        xMinBin = 0;

    if (xMaxBin < 0)
        xMaxBin = m_axisBinnings[Qt::XAxis].getBins() - 1;

    // y
    s64 yMinBin = m_axisBinnings[Qt::YAxis].getBin(yInterval.minValue);
    s64 yMaxBin = m_axisBinnings[Qt::YAxis].getBin(yInterval.maxValue);

    if (yMinBin < 0)
        yMinBin = 0;

    if (yMaxBin < 0)
        yMaxBin = m_axisBinnings[Qt::YAxis].getBins() - 1;

    for (s64 yBin = yMinBin;
         yBin <= yMaxBin;
         ++yBin)
    {
        for (s64 xBin = xMinBin;
             xBin <= xMaxBin;
             ++xBin)
        {
            s64 linearBin = yBin * m_axisBinnings[Qt::XAxis].getBins() + xBin;
            double v = m_data[linearBin];

            if (!std::isnan(v))
            {
                if (v > result.maxValue)
                {
                    result.maxValue = v;
                    result.maxBinX  = xBin;
                    result.maxBinY  = yBin;
                }
                result.entryCount += v;
            }
        }
    }

    result.maxX = m_axisBinnings[Qt::XAxis].getBinLowEdge(result.maxBinX);
    result.maxY = m_axisBinnings[Qt::YAxis].getBinLowEdge(result.maxBinY);

    return result;
}
