/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "histo1d.h"
#include <QDebug>

Histo1D::Histo1D(u32 nBins, double xMin, double xMax, QObject *parent)
    : QObject(parent)
    , m_xAxisBinning(nBins, xMin, xMax)
    , m_data(new double[nBins])
{
    clear();
}

Histo1D::Histo1D(AxisBinning binning, const SharedHistoMem &mem, QObject *parent)
    : QObject(parent)
    , m_xAxisBinning(binning)
    , m_data(mem.data)
    , m_externalMemory(mem)
{
    clear();
}

Histo1D::~Histo1D()
{
    if (ownsMemory())
    {
        delete[] m_data;
    }
}

void Histo1D::resize(u32 nBins)
{
    Q_ASSERT(nBins > 0);

    if (!canResize())
    {
        throw HistoLogicError("resize() not available when using external memory");
    }

    if (nBins != m_xAxisBinning.getBins())
    {
        delete[] m_data;
        try
        {
            m_data = new double[nBins];
        }
        catch (const std::bad_alloc &)
        {
            m_data = nullptr;
            throw;
        }

        m_xAxisBinning.setBins(nBins);
    }
    clear();
}

void Histo1D::setData(const SharedHistoMem &mem, AxisBinning newBinning)
{
    if (ownsMemory())
    {
        throw HistoLogicError("setData() not available when using internal memory");
    }

    m_externalMemory = mem;
    m_data = mem.data;
    setAxisBinning(Qt::XAxis, newBinning);
}

SharedHistoMem Histo1D::getSharedMemory() const
{
    if (ownsMemory())
    {
        throw HistoLogicError("getSharedMemory() not available when using internal memory");
    }

    return m_externalMemory;
}

s32 Histo1D::fill(double x, double weight)
{
    if (!std::isnan(x))
    {
        s64 bin = m_xAxisBinning.getBin(x);

        if (bin == AxisBinning::Underflow)
            m_underflow += weight;
        else if (bin == AxisBinning::Overflow)
            m_overflow += weight;
        else
        {
            double &value = m_data[bin];
            value += weight;

            if (value >= m_maxValue)
            {
                m_maxValue = value;
                m_maxBin = bin;
            }
        }

        ++m_entryCount;

        return static_cast<s32>(bin);
    }

    return -1; // nan
}

double Histo1D::getValue(double x, u32 rrf) const
{
    s64 bin = m_xAxisBinning.getBin(x, rrf);
    if (bin < 0)
        return 0.0;

    return getBinContent(bin, rrf);
}

std::pair<double, double> Histo1D::getValueAndBinLowEdge(double x, u32 rrf) const
{
    s64 bin = m_xAxisBinning.getBin(x, rrf);
    return std::make_pair(getBinLowEdge(bin, rrf), getBinContent(bin, rrf));
}

void Histo1D::clear()
{
    m_entryCount = 0;
    m_maxValue = 0;
    m_maxBin = 0;
    m_underflow = 0.0;
    m_overflow = 0.0;

    for (u32 i=0; i<m_xAxisBinning.getBins(); ++i)
    {
        m_data[i] = 0.0;
    }
}

bool Histo1D::setBinContent(u32 bin, double value, size_t entryCount)
{
    bool result = false;

    if (bin < getNumberOfBins())
    {
        m_data[bin] = value;
        m_entryCount += entryCount;
        result = true;
    }

    return result;
}

void Histo1D::debugDump(bool dumpEmptyBins) const
{
    qDebug() << "Histo1D" << this;
    qDebug() << "  bins =" << m_xAxisBinning.getBins() << ", xMin =" << m_xAxisBinning.getMin() << ", xMax =" << m_xAxisBinning.getMax();
    qDebug() << "  underflow =" << m_underflow << ", overflow =" << m_overflow;

    for (u32 bin = 0; bin < m_xAxisBinning.getBins(); ++bin)
    {
        if (dumpEmptyBins || m_data[bin] > 0.0)
            qDebug() << "  bin =" << bin << ", lowEdge=" << m_xAxisBinning.getBinLowEdge(bin) << ", value =" << m_data[bin];
    }
}

Histo1DStatistics Histo1D::calcStatistics(double minX, double maxX, u32 rrf) const
{
    s64 minBin = m_xAxisBinning.getBinUnchecked(minX, rrf);
    s64 maxBin = m_xAxisBinning.getBinUnchecked(maxX, rrf);

    minBin = std::max(static_cast<s64>(0), minBin);
    maxBin = std::max(static_cast<s64>(0), maxBin);

    if (minBin >= 0 && maxBin >= 0)
    {
#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "minBin =" << minBin << ", minX =" << minX
            << endl
            << "maxBin =" << maxBin << ", maxX =" << maxX
            << endl
            << ", rrf =" << rrf;
#endif

        return calcBinStatistics(minBin, maxBin, rrf);
    }

    return {};
}

Histo1DStatistics Histo1D::calcBinStatistics(u32 startBin, u32 onePastEndBin, u32 rrf) const
{
#if 0
    qDebug() << __PRETTY_FUNCTION__
        << "startBin =" << startBin
        << ", onePastEndBin =" << onePastEndBin
        << ", rrf =" << rrf;
#endif

    Histo1DStatistics result = {};

    result.rrf = rrf;
    result.entryCount = getEntryCount();
    result.minValue = std::numeric_limits<double>::max();
    result.maxValue = std::numeric_limits<double>::lowest();

    if (startBin > onePastEndBin)
        std::swap(startBin, onePastEndBin);

    onePastEndBin = std::min(onePastEndBin, getNumberOfBins(rrf));

    result.statsBinRange = { startBin, onePastEndBin };
    result.statsRange =
    {
        getBinLowEdge(startBin, rrf),
        getBinLowEdge(onePastEndBin, rrf)
    };

    if (startBin >= onePastEndBin)
    {
        // No bins to calculate actual min/max values
        result.minValue = 0.0;
        result.maxValue = 0.0;
    }
    else
    {
        result.minValue = std::numeric_limits<double>::max();
        result.maxValue = std::numeric_limits<double>::lowest();
    }

    for (u32 bin = startBin; bin < onePastEndBin; ++bin)
    {
        double v = getBinContent(bin, rrf);
        result.mean += v * getBinLowEdge(bin, rrf);

        if (v > result.maxValue)
        {
            result.maxValue = v;
            result.maxBin = bin;
        }

        if (v < result.minValue)
        {
            result.minValue = v;
            result.minBin = bin;
        }
    }

    if (result.entryCount)
        result.mean /= result.entryCount;
    else
        result.mean = 0.0; // qQNaN();

    if (result.mean != 0.0 && result.entryCount > 0)
    {
        for (u32 bin = startBin; bin < onePastEndBin; ++bin)
        {
            u32 v = getBinContent(bin, rrf);
            if (v)
            {
                double d = getBinLowEdge(bin, rrf) - result.mean;
                d *= d;
                result.sigma += d * v;
            }
        }
        result.sigma = sqrt(result.sigma / result.entryCount);
    }

    // FWHM
    if (result.maxValue > 0.0)
    {
        const double halfMax = result.maxValue * 0.5;

        // find first bin to the left with  value < halfMax
        double leftBin = 0.0;
        for (s64 bin = result.maxBin; bin >= startBin; --bin)
        {
            if (getBinContent(bin, rrf) < halfMax)
            {
                leftBin = bin;
                break;
            }
        }

        // find first bin to the right with  value < halfMax
        double rightBin = 0.0;
        for (s64 bin = result.maxBin; bin < onePastEndBin; ++bin)
        {
            if (getBinContent(bin, rrf) < halfMax)
            {
                rightBin = bin;
                break;
            }
        }

        auto interp = [](double x0, double y0, double x1, double y1, double x)
        {
            return y0 + ((y1 - y0) / (x1 - x0)) *  (x - x0);
        };

#if 0
        qDebug() << __PRETTY_FUNCTION__
            << endl
            << "  rrf =" << rrf
            << ", startBin =" << startBin
            << ", onePastEndBin =" << onePastEndBin
            << endl
            << "  leftBin =" << leftBin
            << ", rightBin =" << rightBin
            ;
#endif

        double leftBinFraction  = interp(getBinContent(leftBin+1, rrf), leftBin+1,
                                         getBinContent(leftBin, rrf), leftBin, halfMax);

        double rightBinFraction = interp(getBinContent(rightBin-1, rrf), rightBin-1,
                                         getBinContent(rightBin, rrf), rightBin, halfMax);

#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "FWHM: leftbin" << leftBinFraction
            << ", rightBin" << rightBinFraction << "maxBin" << result.maxBin
            << endl
            << "  fwhm" << result.fwhm << "fwhmCenter" << result.fwhmCenter;
#endif

        auto binning = getAxisBinning(Qt::XAxis);
        double rightLowEdge = binning.getBinLowEdgeFractional(rightBinFraction, rrf);
        double leftLowEdge = binning.getBinLowEdgeFractional(leftBinFraction, rrf);
        result.fwhm = std::abs(rightLowEdge - leftLowEdge);
        double halfbinWidth = binning.getBinWidth(rrf) * 0.5;
        // moves the fwhm center by half a bin width to the right
        result.fwhmCenter = (rightLowEdge + leftLowEdge) * 0.5 + halfbinWidth;
    }

    return result;
}

/* The Histo1D text format is
 * 1st line: bins xMin xMax underflow overflow entryCount
 * subsequent lines: one double per line representing the bin content starting at bin=0
 */

QTextStream &writeHisto1D(QTextStream &out, Histo1D *histo)
{
    out << histo->getNumberOfBins() << " " << histo->getXMin() << " " << histo->getXMax()
        << " " << histo->getUnderflow() << " " << histo->getOverflow() << histo->getEntryCount() << endl;

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
    size_t entryCount;

    in >> nBins >> xMin >> xMax >> underflow >> overflow >> entryCount;

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

        result->setBinContent(bin, value, 1);
    }

    result->setEntryCount(entryCount);

    return result;
}

Histo1D::ValueAndBin Histo1D::getMinValueAndBin(u32 rrf) const
{
    ValueAndBin result = {};
    const u32 binCount = getNumberOfBins(rrf);

    if (binCount > 0)
    {
        result.value = getBinContent(0, rrf);

        for (u32 bin = 1; bin < binCount; bin++)
        {
            auto v = getBinContent(bin, rrf);
            if (v < result.value)
            {
                result.value = v;
                result.bin   = bin;
            }
        }
    }

    return result;
}

Histo1D::ValueAndBin Histo1D::getMaxValueAndBin(u32 rrf) const
{
    ValueAndBin result = {};
    const u32 binCount = getNumberOfBins(rrf);

    if (binCount > 0)
    {
        result.value = getBinContent(0, rrf);

        for (u32 bin = 1; bin < binCount; bin++)
        {
            auto v = getBinContent(bin, rrf);
            if (v >= result.value)
            {
                result.value = v;
                result.bin   = bin;
            }
        }
    }

    return result;
}
