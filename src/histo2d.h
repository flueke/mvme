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
#ifndef __HISTO2D_H__
#define __HISTO2D_H__

#include "histo_util.h"

#include <QObject>
#include <QDebugStateSaver>
#include <array>

#include "libmvme_export.h"

struct ResolutionReductionFactors
{
    u32 x = AxisBinning::NoResolutionReduction;
    u32 y = AxisBinning::NoResolutionReduction;

    inline bool isNoReduction() const
    {
        return (x == AxisBinning::NoResolutionReduction
                && y == AxisBinning::NoResolutionReduction);
    }

    inline u32 getXFactor() const
    {
        return x == AxisBinning::NoResolutionReduction ? 1u : x;
    }

    inline u32 getYFactor() const
    {
        return y == AxisBinning::NoResolutionReduction ? 1u : y;
    }
};

struct Histo2DStatistics
{
    using Intervals = std::array<AxisInterval, 3>;

    u32 maxBinX = 0;        // x bin of max value
    u32 maxBinY = 0;        // y bin of max value
    double maxX = 0.0;      // low edge of maxBinX
    double maxY = 0.0;      // low edge of maxBinY
    double maxZ = 0.0;
    double entryCount = 0;

    /* The resolution reduction that was in effect when the stats where calculated.
     * bin numbers are given in terms of these factors. */
    ResolutionReductionFactors rrf = {};

    /* The x- and y-axis intervals for which the stats where calculated and the resulting
     * z-interval. */
    Intervals intervals;
};

#ifndef QT_NO_DEBUG
inline QDebug &operator<<(QDebug &dbg, const ResolutionReductionFactors &rrf)
{
    QDebugStateSaver ss(dbg);
    dbg.nospace().noquote() << "RRF(" << rrf.x << ", " << rrf.y << ")";
    return dbg;
}
#endif

class LIBMVME_EXPORT Histo2D: public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString title READ getTitle WRITE setTitle)
    Q_PROPERTY(QString footer READ getFooter WRITE setFooter)

    signals:
        void axisBinningChanged(Qt::Axis axis);

    public:
        static const u32 NoRR = AxisBinning::NoResolutionReduction;

        Histo2D(u32 xBins, double xMin, double xMax,
                u32 yBins, double yMin, double yMax,
                QObject *parent = 0);
        ~Histo2D();

        void resize(s32 xBins, s32 yBins);

        void fill(double x, double y, double weight = 1.0);

        double getValue(double x, double y,
                        const ResolutionReductionFactors &rrf = {}) const;

        double getBinContent(u32 xBin, u32 yBin,
                             const ResolutionReductionFactors &rrf = {}) const;

        void clear();
        inline double *data() { return m_data; }

        void debugDump() const;
        inline size_t getStorageSize() const
        {
            return getAxisBinning(Qt::XAxis).getBins()
                * getAxisBinning(Qt::YAxis).getBins()
                * sizeof(double);
        }

        AxisBinning getAxisBinning(Qt::Axis axis) const
        {
            if (axis < m_axisBinnings.size())
            {
                return m_axisBinnings[axis];
            }

            return AxisBinning();
        }

        void setAxisBinning(Qt::Axis axis, AxisBinning binning)
        {
            if (axis < m_axisBinnings.size())
            {
                if (binning != m_axisBinnings[axis])
                {
                    m_axisBinnings[axis] = binning;
                    emit axisBinningChanged(axis);
                }
            }
        }

        AxisInfo getAxisInfo(Qt::Axis axis) const
        {
            if (axis < m_axisInfos.size())
            {
                return m_axisInfos[axis];
            }
            return AxisInfo();
        }

        void setAxisInfo(Qt::Axis axis, AxisInfo info)
        {
            if (axis < m_axisInfos.size())
            {
                m_axisInfos[axis] = info;
            }
        }

        AxisInterval getInterval(Qt::Axis axis) const;

        Histo2DStatistics calcStatistics(
            AxisInterval xInterval,
            AxisInterval yInterval,
            const ResolutionReductionFactors &rrf = {}) const;

        Histo2DStatistics calcGlobalStatistics(
            const ResolutionReductionFactors &rrf = {}) const;

        double getUnderflow() const { return m_underflow; }
        void setUnderflow(double value) { m_underflow = value; }

        double getOverflow() const { return m_overflow; }
        void setOverflow(double value) { m_overflow = value; }

        void setTitle(const QString &title)
        {
            m_title = title;
        }

        QString getTitle() const
        {
            return m_title;
        }

        void setFooter(const QString &footer)
        {
            m_footer = footer;
        }

        QString getFooter() const
        {
            return m_footer;
        }

        using AxisBinnings = std::array<AxisBinning, 2>;
        using AxisInfos = std::array<AxisInfo, 2>;

        AxisBinnings getAxisBinnings() const { return m_axisBinnings; }
        AxisInfos getAxisInfos() const { return m_axisInfos; }

        inline u32 getNumberOfXBins(u32 rrf = NoRR) const
        {
            return m_axisBinnings[Qt::XAxis].getBins(rrf);
        }

        inline u32 getNumberOfYBins(u32 rrf = NoRR) const
        {
            return m_axisBinnings[Qt::YAxis].getBins(rrf);
        }

        inline u32 getNumberOfBins(Qt::Axis axis, u32 rrf = NoRR)
        {
            if (axis < m_axisBinnings.size())
                return m_axisBinnings[axis].getBinCount(rrf);
            return 0;
        }

        inline double getXMin() const { return m_axisBinnings[Qt::XAxis].getMin(); }
        inline double getXMax() const { return m_axisBinnings[Qt::XAxis].getMax(); }
        inline double getYMin() const { return m_axisBinnings[Qt::YAxis].getMin(); }
        inline double getYMax() const { return m_axisBinnings[Qt::YAxis].getMax(); }

    private:
        AxisBinnings m_axisBinnings;
        AxisInfos m_axisInfos;

        double *m_data = nullptr;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        QString m_title;
        QString m_footer;
};

using Histo2DPtr = std::shared_ptr<Histo2D>;

#endif /* __HISTO2D_H__ */
