#include "histograms.h"
#include <limits>
#include <QDebug>

namespace analysis
{

//
// Histo1D
//
Histo1D::Histo1D(u32 nBins, double xMin, double xMax)
    : m_nBins(nBins)
    , m_xMin(xMin)
    , m_xMax(xMax)
    , m_data(new double[m_nBins])

{
    clear();
}

Histo1D::~Histo1D()
{
    delete[] m_data;
}

s32 Histo1D::fill(double x, double weight)
{
    if (x < m_xMin)
    {
        m_underflow += weight;
        return -1;
    }
    else if (x >= m_xMax)
    {
        m_overflow += weight;
        return -1;
    }
    else
    {
        u32 bin = getBin(x);
        m_data[bin] += weight;
        m_count += weight;
        double value = m_data[bin];
        if (value >= m_maxValue)
        {
            m_maxValue = value;
            m_maxBin = bin;
        }
        return static_cast<s32>(bin);
    }
}

double Histo1D::getValue(double x) const
{
    s64 bin = getBin(x);
    if (bin < 0)
        return 0.0;
    return m_data[bin];
}

void Histo1D::clear()
{
    m_count = 0.0;
    m_maxValue = 0;
    m_maxBin = 0;

    for (u32 i=0; i<getNumberOfBins(); ++i)
    {
        m_data[i] = 0.0;
    }
}

s64 Histo1D::getBin(double x) const
{
    if (x < m_xMin || x >= m_xMax)
    {
        return -1;
    }

    double binWidth = getBinWidth();
    u32 bin = static_cast<u32>(std::floor(x / binWidth));

    return bin;
}

double Histo1D::getBinLowEdge(u32 bin) const
{
    return m_xMin + bin * getBinWidth();
}

double Histo1D::getBinCenter(u32 bin) const
{
    return getBinLowEdge(bin) + getBinWidth() * 0.5;
}

double Histo1D::getBinContent(u32 bin) const
{
    if (bin < getNumberOfBins())
        return m_data[bin];
    return 0.0;
    //return std::numeric_limits<double>::quiet_NaN();
}

void Histo1D::debugDump() const
{
    qDebug() << "Histo1D" << this << m_name;
    qDebug() << "  bins =" << m_nBins << ", xMin =" << m_xMin << ", xMax =" << m_xMax;
    qDebug() << "  underflow =" << m_underflow << ", overflow =" << m_overflow;

    for (u32 bin = 0; bin < m_nBins; ++bin)
    {
        qDebug() << "  bin =" << bin << ", lowEdge=" << getBinLowEdge(bin) << ", value =" << getBinContent(bin);
    }
}

//
// Histo2D
//
Histo2D::Histo2D(u32 xBins, double xMin, double xMax,
                 u32 yBins, double yMin, double yMax)
    : m_xAxis(xBins, xMin, xMax)
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

    if (xBin == BinnedAxis::Underflow || yBin == BinnedAxis::Underflow)
    {
        m_underflow += weight;
    }
    else if (xBin == BinnedAxis::Overflow || yBin == BinnedAxis::Overflow)
    {
        m_overflow += weight;
    }
    else
    {
        u32 linearBin = yBin * m_xAxis.getBins() + xBin;
        m_data[linearBin] += weight;
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
    qDebug() << "Histo2D" << this << m_name;
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

}
