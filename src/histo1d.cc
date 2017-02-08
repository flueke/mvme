#include "histo1d.h"

Histo1D::Histo1D(u32 nBins, double xMin, double xMax, QObject *parent)
    : QObject(parent)
    , m_xAxis(nBins, xMin, xMax)
    , m_data(new double[nBins])

{
    clear();
}

Histo1D::~Histo1D()
{
    delete[] m_data;
}

s32 Histo1D::fill(double x, double weight)
{
    s64 bin = m_xAxis.getBin(x);

    if (bin == AxisBinning::Underflow)
    {
        m_underflow += weight;
    }
    else if (bin == AxisBinning::Overflow)
    {
        m_overflow += weight;
    }
    else
    {
        m_data[bin] += weight;
        m_count += weight;
        double value = m_data[bin];
        if (value >= m_maxValue)
        {
            m_maxValue = value;
            m_maxBin = bin;
        }
    }

    return static_cast<s32>(bin);
}

double Histo1D::getValue(double x) const
{
    s64 bin = m_xAxis.getBin(x);
    if (bin < 0)
        return 0.0;
    return m_data[bin];
}

void Histo1D::clear()
{
    m_count = 0.0;
    m_maxValue = 0;
    m_maxBin = 0;

    for (u32 i=0; i<m_xAxis.getBins(); ++i)
    {
        m_data[i] = 0.0;
    }
}

bool Histo1D::setBinContent(u32 bin, double value)
{
    bool result = false;

    if (bin < getNumberOfBins())
    {
        m_data[bin] = value;
        result = true;
    }

    return result;
}

void Histo1D::debugDump(bool dumpEmptyBins) const
{
    qDebug() << "Histo1D" << this;
    qDebug() << "  bins =" << m_xAxis.getBins() << ", xMin =" << m_xAxis.getMin() << ", xMax =" << m_xAxis.getMax();
    qDebug() << "  underflow =" << m_underflow << ", overflow =" << m_overflow;

    for (u32 bin = 0; bin < m_xAxis.getBins(); ++bin)
    {
        if (dumpEmptyBins || m_data[bin] > 0.0)
            qDebug() << "  bin =" << bin << ", lowEdge=" << m_xAxis.getBinLowEdge(bin) << ", value =" << m_data[bin];
    }
}

Histo1DStatistics Histo1D::calcStatistics(double minX, double maxX) const
{
    s64 minBin = m_xAxis.getBinUnchecked(minX);
    s64 maxBin = m_xAxis.getBinUnchecked(maxX);

    if (minBin >= 0 && maxBin >= 0)
    {
        return calcBinStatistics(minBin, maxBin);
    }

    return {};
}

Histo1DStatistics Histo1D::calcBinStatistics(u32 startBin, u32 onePastEndBin) const
{
    Histo1DStatistics result;

    if (startBin > onePastEndBin)
        std::swap(startBin, onePastEndBin);

    onePastEndBin = std::min(onePastEndBin, getNumberOfBins());

    for (u32 bin = startBin; bin < onePastEndBin; ++bin)
    {
        double v = getBinContent(bin);
        result.mean += v * getBinLowEdge(bin);
        result.entryCount += v;

        if (v > result.maxValue)
        {
            result.maxValue = v;
            result.maxBin = bin;
        }
    }

    if (result.entryCount)
        result.mean /= result.entryCount;
    else
        result.mean = qQNaN();

    if (result.mean > 0 && result.entryCount > 0)
    {
        for (u32 bin = startBin; bin < onePastEndBin; ++bin)
        {
            u32 v = getBinContent(bin);
            if (v)
            {
                double d = getBinLowEdge(bin) - result.mean;
                d *= d;
                result.sigma += d * v;
            }
        }
        result.sigma = sqrt(result.sigma / result.entryCount);
    }

    // FWHM
    if (result.maxValue > 0.0)
    {
        const double halfMax = result.maxValue / 2.0;

        // find first bin to the left with  value < halfMax
        double leftBin = 0.0;
        for (s64 bin = result.maxBin; bin >= startBin; --bin)
        {
            if (getBinContent(bin) < halfMax)
            {
                leftBin = bin;
                break;
            }
        }

        // find first bin to the right with  value < halfMax
        double rightBin = 0.0;
        for (u32 bin = result.maxBin; bin < onePastEndBin; ++bin)
        {
            if (getBinContent(bin) < halfMax)
            {
                rightBin = bin;
                break;
            }
        }

#if 0
        // Using the delta
        double deltaX = 1.0; // distance from bin to bin+1;
        double deltaY = value(leftBin+1) - value(leftBin);
        double deltaHalfY = value(leftBin+1) - halfMax;
        double deltaHalfX = deltaHalfY * (deltaX / deltaY);
        leftBin = (leftBin + 1.0) - deltaHalfX;

        qDebug() << "leftbin" << leftBin
            << "deltaY" << deltaY
            << "deltaHalfY" << deltaHalfY
            << "deltaHalfX" << deltaHalfX;

        deltaY = value(rightBin-1) - value(rightBin);
        deltaHalfY = value(rightBin-1) - halfMax;
        deltaHalfX = deltaHalfY * (deltaX / deltaY);
        rightBin = (rightBin - 1.0) + deltaHalfX;

        qDebug() << "rightBin" << rightBin
            << "deltaY" << deltaY
            << "deltaHalfY" << deltaHalfY
            << "deltaHalfX" << deltaHalfX;
#else
        auto interp = [](double x0, double y0, double x1, double y1, double x)
        {
            return y0 + ((y1 - y0) / (x1 - x0)) *  (x - x0);
        };


        // FIXME: this is not correct. am I allowed to swap x/y when calling interp()?

        leftBin = interp(getBinContent(leftBin+1), leftBin+1, getBinContent(leftBin), leftBin, halfMax);
        rightBin = interp(getBinContent(rightBin-1), rightBin-1, getBinContent(rightBin), rightBin, halfMax);

        //qDebug() << "leftbin" << leftBin << "rightBin" << rightBin << "maxBin" << result.maxChannel;
#endif

        result.fwhm = std::abs(rightBin - leftBin);
    }

    return result;
}

/* The Histo1D text format is
 * 1st line: bins xMin xMax underflow overflow
 * subsequent lines: one double per line representing the bin content starting at bin=0
 */

QTextStream &writeHisto1D(QTextStream &out, Histo1D *histo)
{
    out << histo->getNumberOfBins() << histo->getXMin() << histo->getXMax()
        << histo->getUnderflow() << histo->getOverflow();

    for (u32 bin = 0; bin < histo->getNumberOfBins(); ++bin)
    {
        out << histo->getBinContent(bin) << endl;
    }

    return out;
}

Histo1D *readHisto1D(QTextStream &in)
{
    double xMin, xMax, underflow, overflow;
    u32 nBins;

    in >> nBins >> xMin >> xMax >> underflow >> overflow;

    if (in.status() != QTextStream::Ok)
        return nullptr;

    auto result = new Histo1D(nBins, xMin, xMax);
    result->setUnderflow(underflow);
    result->setOverflow(overflow);

    for (u32 bin = 0; bin < result->getNumberOfBins(); ++bin)
    {
        double value;
        in >> value;

        if (in.status() != QTextStream::Ok)
            break;

        result->setBinContent(bin, value);
    }

    return result;
}
