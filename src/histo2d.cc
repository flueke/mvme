#include "histo2d.h"

Histo2D::Histo2D(u32 xBins, double xMin, double xMax,
                 u32 yBins, double yMin, double yMax,
                 QObject *parent)
    : QObject(parent)
    , m_xAxis(xBins, xMin, xMax)
    , m_yAxis(yBins, yMin, yMax)
    , m_data(new double[xBins * yBins])
{
    clear();
}

Histo2D::~Histo2D()
{
    delete[] m_data;
}

void Histo2D::fill(double x, double y, double weight)
{
    s64 xBin = m_xAxis.getBin(x);
    s64 yBin = m_yAxis.getBin(y);

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
        u32 linearBin = yBin * m_xAxis.getBins() + xBin;
        m_data[linearBin] += weight;
        double newValue = m_data[linearBin];

        if (newValue > m_stats.maxValue)
        {
            m_stats.maxValue = newValue;
            m_stats.maxX = x;
            m_stats.maxY = y;
        }
        m_stats.entryCount += weight;
    }
}

double Histo2D::getValue(double x, double y) const
{
    s64 xBin = m_xAxis.getBin(x);
    s64 yBin = m_xAxis.getBin(y);

    if (xBin < 0 || yBin < 0)
        return 0.0;

    u32 linearBin = yBin * m_xAxis.getBins() + xBin;
    return m_data[linearBin];
}

void Histo2D::clear()
{
    for (size_t bin = 0; bin < m_xAxis.getBins() * m_yAxis.getBins(); ++bin)
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
    qDebug() << "X-Axis: bins =" << m_xAxis.getBins()
        << ", min =" << m_xAxis.getMin()
        << ", max =" << m_xAxis.getMax();
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
        result.minValue = m_xAxis.getMin();
        result.maxValue = m_xAxis.getMax();
    }
    else if (axis == Qt::YAxis)
    {
        result.minValue = m_yAxis.getMin();
        result.maxValue = m_yAxis.getMax();
    }
    else if (axis == Qt::ZAxis)
    {
        result.minValue = 0.0;
        result.maxValue = m_stats.maxValue;
    }

    return result;
}
