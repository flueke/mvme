#include "histo2d.h"
#include "histo1d.h"
#include "util.h"

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

    // Always update the number of bins on both axes, even if the total number
    // stayed the same, because 10bit:10bit might become 9bit:11bit.
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


double Histo2D::getBinContent(u32 xBin, u32 yBin) const
{
    double result = make_quiet_nan();

    if (xBin < getAxisBinning(Qt::XAxis).getBins() && yBin < getAxisBinning(Qt::YAxis).getBins())
    {
        u32 linearBin = yBin * getAxisBinning(Qt::XAxis).getBins() + xBin;
        result = m_data[linearBin];
    }

    return result;
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

Histo1DPtr make_x_projection(Histo2D *histo)
{
    return make_projection(histo, Qt::XAxis);
}

Histo1DPtr make_x_projection(Histo2D *histo, double startValue, double endValue)
{
    return make_projection(histo, Qt::XAxis, startValue, endValue);
}

Histo1DPtr make_y_projection(Histo2D *histo)
{
    return make_projection(histo, Qt::YAxis);
}

Histo1DPtr make_y_projection(Histo2D *histo, double startValue, double endValue)
{
    return make_projection(histo, Qt::YAxis, startValue, endValue);
}

std::shared_ptr<Histo1D> make_projection(Histo2D *histo, Qt::Axis axis)
{
    auto binning = histo->getAxisBinning(axis);
    return make_projection(histo, axis, binning.getMin(), binning.getMax());
}

std::shared_ptr<Histo1D> make_projection(Histo2D *histo, Qt::Axis axis, double startValue, double endValue)
{
    auto projBinning  = histo->getAxisBinning(axis);
    auto otherBinning = histo->getAxisBinning(axis == Qt::XAxis ? Qt::YAxis : Qt::XAxis);

    s64 startBin = projBinning.getBinBounded(startValue);
    s64 endBin = projBinning.getBinBounded(endValue);
    s64 nBins = (endBin - startBin) + 1;

    // adjust start and end to low edge of corresponding bin
    startValue = projBinning.getBinLowEdge(startBin);
    endValue = projBinning.getBinLowEdge(endBin);

    auto result = std::make_shared<Histo1D>(nBins, startValue, endValue);
    result->setAxisInfo(Qt::XAxis, histo->getAxisInfo(axis));
    result->setObjectName(histo->objectName() + (axis == Qt::XAxis ? QSL(" X") : QSL(" Y")) + QSL(" Projection"));

    u32 destBin = 0;

    for (u32 binI = startBin;
         binI < endBin;
         ++binI)
    {
        double value = 0.0;

        for (u32 binJ = 0;
             binJ < otherBinning.getBins();
             ++binJ)
        {
            if (axis == Qt::XAxis)
            {
                value += histo->getBinContent(binI, binJ);
            }
            else
            {
                value += histo->getBinContent(binJ, binI);
            }
        }

        result->setBinContent(destBin++, value);
    }

    return result;
}
