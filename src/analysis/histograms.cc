#include "histograms.h"
#include <limits>
#include <QDebug>

namespace analysis
{

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

void Histo1D::fill(double x, double weight)
{
    if (x < m_xMin)
    {
        m_underflow += weight;
    }
    else if (x >= m_xMax)
    {
        m_overflow += weight;
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
    }
}

double Histo1D::getValue(double x) const
{
    u32 bin = getBin(x);
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
    qDebug() << "Histo1D" << this;
    qDebug() << "  bins =" << m_nBins << ", xMin =" << m_xMin << ", xMax =" << m_xMax;
    qDebug() << "  underflow =" << m_underflow << ", overflow =" << m_overflow;

    for (u32 bin = 0; bin < m_nBins; ++bin)
    {
        qDebug() << "  bin =" << bin << ", lowEdge=" << getBinLowEdge(bin) << ", value =" << getBinContent(bin);
    }
}

}
