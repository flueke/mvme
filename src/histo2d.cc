/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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

    u32 xBinsNew = static_cast<u32>(xBins);
    u32 yBinsNew = static_cast<u32>(yBins);

    if (xBinsNew * yBinsNew != m_axisBinnings[Qt::XAxis].getBins() * m_axisBinnings[Qt::YAxis].getBins())
    {
        // Reallocate memory for the new size
        delete[] m_data;
        try
        {
            m_data = new double[xBinsNew * yBinsNew];
        }
        catch (const std::bad_alloc &)
        {
            m_data = nullptr;
            throw;
        }
    }

    // Always update the number of bins on both axes, even if the total number
    // stayed the same, because 10bit:10bit might become 9bit:11bit.
    m_axisBinnings[Qt::XAxis].setBins(xBinsNew);
    m_axisBinnings[Qt::YAxis].setBins(yBinsNew);
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
    }
}

double Histo2D::getValue(double x, double y,
                         const ResolutionReductionFactors &rrf) const
{
    s64 binX = m_axisBinnings[Qt::XAxis].getBin(x, rrf.x);
    s64 binY = m_axisBinnings[Qt::YAxis].getBin(y, rrf.y);

    if (binX < 0 || binY < 0) // under-/overflow
        return 0.0;

    return getBinContent(binX, binY, rrf);
}

double Histo2D::getBinContent(u32 xInputBin, u32 yInputBin,
                              const ResolutionReductionFactors &rrf) const
{
    /* Summation of the rectangle formed by the x and y bins specified by the individual
     * axis res reductions.
     *
     * Implementation: sum up the rows of the rectangles.
     */
    u32 xBinCount = m_axisBinnings[Qt::XAxis].getBinCount();
    u32 yBinCount = m_axisBinnings[Qt::YAxis].getBinCount();

    u32 ix1 = xInputBin * rrf.getXFactor();
    u32 ix2 = ix1 + rrf.getXFactor();

    u32 iy1 = yInputBin * rrf.getYFactor();
    u32 iy2 = iy1 + rrf.getYFactor();

    ix1 = qBound(0u, ix1, xBinCount - 1);
    ix2 = qBound(0u, ix2, xBinCount);

    iy1 = qBound(0u, iy1, yBinCount - 1);
    iy2 = qBound(0u, iy2, yBinCount);

    double result = 0.0;
    int nBins  = 0;

    for (s64 iy = iy1; iy < iy2; iy++)
    {
        for (s64 ix = ix1; ix < ix2; ix++)
        {
            result += m_data[iy * xBinCount + ix];
            nBins++;
        }
    }

#if 0
    qDebug() << __PRETTY_FUNCTION__ << this
        << endl
        << "  xInputBin=" << xInputBin << ", yInputBin=" << yInputBin << ", rrf=" << rrf
        << endl
        << "  xBinCount=" << xBinCount << ", yBinCount=" << yBinCount
        << endl
        << "  ix1=" << ix1 << ", ix2=" << ix2
        << endl
        << "  iy1=" << iy1 << ", iy2=" << iy2
        << "summed bins =" << nBins << ", final value =" << result;
        ;
#endif
        return result;
}

void Histo2D::clear()
{
    size_t binCount = m_axisBinnings[Qt::XAxis].getBins() * m_axisBinnings[Qt::YAxis].getBins();
    std::fill(m_data, m_data + binCount, 0.0);

    m_underflow = 0.0;
    m_overflow = 0.0;
}

void Histo2D::debugDump() const
{
    qDebug() << "Histo2D" << this << objectName();
    auto xAxis = m_axisBinnings[Qt::XAxis];
    qDebug() << "X-Axis: bins =" << xAxis.getBins()
        << ", min =" << xAxis.getMin()
        << ", max =" << xAxis.getMax();
    qDebug() << "  underflow =" << m_underflow << ", overflow =" << m_overflow;
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
        InvalidCodePath;
    }

    return result;
}

Histo2DStatistics Histo2D::calcGlobalStatistics(const ResolutionReductionFactors &rrf) const
{
    return calcStatistics(getInterval(Qt::XAxis),
                          getInterval(Qt::YAxis),
                          rrf);
}

Histo2DStatistics Histo2D::calcStatistics(AxisInterval xInterval,
                                          AxisInterval yInterval,
                                          const ResolutionReductionFactors &rrf) const
{
    // TODO: rrf
    //qDebug() << __PRETTY_FUNCTION__
    //    << "xInterval =" << xInterval.minValue << xInterval.maxValue
    //    << "yInterval =" << yInterval.minValue << yInterval.maxValue;

    Histo2DStatistics result;

    result.rrf = rrf;

    // x
    s64 xMinBin = m_axisBinnings[Qt::XAxis].getBin(xInterval.minValue, rrf.x);
    s64 xMaxBin = m_axisBinnings[Qt::XAxis].getBin(xInterval.maxValue, rrf.x);

    if (xMinBin < 0)
        xMinBin = 0;

    if (xMaxBin < 0)
        xMaxBin = m_axisBinnings[Qt::XAxis].getBinCount(rrf.x) - 1;

    // y
    s64 yMinBin = m_axisBinnings[Qt::YAxis].getBin(yInterval.minValue, rrf.y);
    s64 yMaxBin = m_axisBinnings[Qt::YAxis].getBin(yInterval.maxValue, rrf.y);

    if (yMinBin < 0)
        yMinBin = 0;

    if (yMaxBin < 0)
        yMaxBin = m_axisBinnings[Qt::YAxis].getBinCount(rrf.y) - 1;

    for (s64 yBin = yMinBin;
         yBin <= yMaxBin;
         ++yBin)
    {
        for (s64 xBin = xMinBin;
             xBin <= xMaxBin;
             ++xBin)
        {
            //s64 linearBin = yBin * m_axisBinnings[Qt::XAxis].getBins() + xBin;
            //double v = m_data[linearBin];

            double v = getBinContent(xBin, yBin, rrf);

            if (!std::isnan(v))
            {
                if (v > result.maxZ)
                {
                    result.maxZ = v;
                    result.maxBinX  = xBin;
                    result.maxBinY  = yBin;
                }
                result.entryCount += v;
            }
        }
    }

    result.maxX = m_axisBinnings[Qt::XAxis].getBinLowEdge(result.maxBinX, rrf.x);
    result.maxY = m_axisBinnings[Qt::YAxis].getBinLowEdge(result.maxBinY, rrf.y);

    result.intervals[Qt::XAxis] = xInterval;
    result.intervals[Qt::YAxis] = yInterval;
    result.intervals[Qt::ZAxis] = { 0.0, result.maxZ };

    return result;
}
