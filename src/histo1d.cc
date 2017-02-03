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

void Histo1D::debugDump() const
{
    qDebug() << "Histo1D" << this;
    qDebug() << "  bins =" << m_xAxis.getBins() << ", xMin =" << m_xAxis.getMin() << ", xMax =" << m_xAxis.getMax();
    qDebug() << "  underflow =" << m_underflow << ", overflow =" << m_overflow;

    for (u32 bin = 0; bin < m_xAxis.getBins(); ++bin)
    {
        qDebug() << "  bin =" << bin << ", lowEdge=" << m_xAxis.getBinLowEdge(bin) << ", value =" << m_data[bin];
    }
}

Histo1DStatistics Histo1D::calcStatistics(u32 startChannel, u32 onePastEndChannel) const
{
    Q_ASSERT(!"not implemented");
    Histo1DStatistics result;
    return result;
#if 0
    if (startChannel > onePastEndChannel)
        std::swap(startChannel, onePastEndChannel);

    startChannel = std::min(startChannel, getResolution());
    onePastEndChannel = std::min(onePastEndChannel, getResolution());

    Hist1DStatistics result;

    for (u32 i = startChannel; i < onePastEndChannel; ++i)
    {
        double v = value(i);
        result.mean += v * i;
        result.entryCount += v;

        if (v > result.maxValue)
        {
            result.maxValue = v;
            result.maxChannel = i;
        }
    }

    if (result.entryCount)
        result.mean /= result.entryCount;
    else
        result.mean = qQNaN();

    if (result.mean > 0 && result.entryCount > 0)
    {
        for (u32 i = startChannel; i < onePastEndChannel; ++i)
        {
            u32 v = value(i);
            if (v)
            {
                double d = i - result.mean;
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
        for (s64 bin = result.maxChannel; bin >= startChannel; --bin)
        {
            if (value(bin) < halfMax)
            {
                leftBin = bin;
                break;
            }
        }

        // find first bin to the right with  value < halfMax
        double rightBin = 0.0;
        for (u32 bin = result.maxChannel; bin < onePastEndChannel; ++bin)
        {
            if (value(bin) < halfMax)
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

        leftBin = interp(value(leftBin+1), leftBin+1, value(leftBin), leftBin, halfMax);
        rightBin = interp(value(rightBin-1), rightBin-1, value(rightBin), rightBin, halfMax);

        //qDebug() << "leftbin" << leftBin << "rightBin" << rightBin << "maxBin" << result.maxChannel;
#endif

        result.fwhm = std::abs(rightBin - leftBin);
    }

    return result;
#endif
}

QTextStream &writeHisto1D(QTextStream &out, Histo1D *histo)
{
    Q_ASSERT(!"not implemented");
#if 0
    for (quint32 valueIndex=0; valueIndex<histo->getResolution(); ++valueIndex)
    {
        out << valueIndex << " "
            << histo->value(valueIndex)
            << endl;
    }
#endif

    return out;
}

Histo1D *readHisto1D(QTextStream &in)
{
    Q_ASSERT(!"not implemented");
    return nullptr;
#if 0
    double value;
    QVector<double> values;

    while (true)
    {
        u32 channelIndex;
        in >> channelIndex >> value;
        if (in.status() != QTextStream::Ok)
            break;
        values.push_back(value);
    }

    u32 bits = std::ceil(std::log2(values.size()));

    qDebug() << values.size() << bits;

    auto result = new Hist1D(bits);

    for (int channelIndex = 0;
         channelIndex < values.size();
        ++channelIndex)
    {
        result->fill(channelIndex, static_cast<u32>(values[channelIndex]));
    }

    return result;
#endif
}
