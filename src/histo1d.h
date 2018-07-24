/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __HISTO1D_H__
#define __HISTO1D_H__

#include "analysis/a2/memory.h"
#include "histo_util.h"
#include "libmvme_export.h"
#include <memory>
#include <QObject>

struct Histo1DStatistics
{
    u32 maxBin = 0;
    double maxValue = 0.0;
    double mean = 0.0;
    double sigma = 0.0;
    double entryCount = 0;
    double fwhm = 0.0;
    // X coordinate of the center between the fwhm edges
    double fwhmCenter = 0.0;
};

struct HistoLogicError: public std::runtime_error
{
    HistoLogicError(const char *s): std::runtime_error(s) {}
};

struct SharedHistoMem
{
    // Shared pointer to arena to keep the memory alive.
    std::shared_ptr<memory::Arena> arena;

    // Pointer into the arena where this Histograms data starts.
    double *data = nullptr;
};

class LIBMVME_EXPORT Histo1D: public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString title READ getTitle WRITE setTitle)
    Q_PROPERTY(QString footer READ getFooter WRITE setFooter)

    signals:
        void axisBinningChanged(Qt::Axis axis);

    public:
        /* This constructor will make the histo allocate memory internally.
         * resize() will be available. */
        Histo1D(u32 nBins, double xMin, double xMax, QObject *parent = 0);

        /* Uses the memory passed in with the data pointer. resize() will not
         * be available. */
        Histo1D(AxisBinning binning, const SharedHistoMem &mem, QObject *parent = 0);

        ~Histo1D();

        bool ownsMemory() const { return !m_externalMemory.arena; }
        bool canResize() const { return !m_externalMemory.arena; }

        /* Throws HistoLogicError if external memory is used. */
        void resize(u32 nBins);

        /* Throws HistoLogicError if internal memory is used. It could
         * deallocate and switch to being a histo with external memory but it
         * doesn't right now. */
        void setData(const SharedHistoMem &mem, AxisBinning newBinning);

        // Returns the bin number or -1 in case of under/overflow.
        s32 fill(double x, double weight = 1.0);

        /* Returns the counts of the bin containing the given x value. */
        double getValue(double x) const;

        /* Returns a pair of (x_bin_low_edge, y_counts) for the given x value. */
        std::pair<double, double> getValueAndBinLowEdge(double x) const;

        void clear();
        inline double *data() { return m_data; }

        inline u32 getNumberOfBins() const { return m_xAxisBinning.getBins(); }
        inline size_t getStorageSize() const { return getNumberOfBins() * sizeof(double); }

        inline double getBinContent(u32 bin) const
        {
            return (bin < getNumberOfBins()) ? m_data[bin] : 0.0;
        }
        bool setBinContent(u32 bin, double value);

        inline double getXMin() const { return m_xAxisBinning.getMin(); }
        inline double getXMax() const { return m_xAxisBinning.getMax(); }
        inline double getWidth() const { return m_xAxisBinning.getWidth(); }

        inline double getBinWidth() const { return m_xAxisBinning.getBinWidth(); }
        inline double getBinLowEdge(u32 bin) const { return m_xAxisBinning.getBinLowEdge(bin); }
        inline double getBinCenter(u32 bin) const { return m_xAxisBinning.getBinCenter(bin); }

        AxisBinning getAxisBinning(Qt::Axis axis) const
        {
            switch (axis)
            {
                case Qt::XAxis:
                    return m_xAxisBinning;
                default:
                    return AxisBinning();
            }
        }

        void setAxisBinning(Qt::Axis axis, AxisBinning binning)
        {
            if (axis == Qt::XAxis && binning != m_xAxisBinning)
            {
                m_xAxisBinning = binning;
                clear();
                emit axisBinningChanged(axis);
            }
        }

        AxisInfo getAxisInfo(Qt::Axis axis) const
        {
            switch (axis)
            {
                case Qt::XAxis:
                    return m_xAxisInfo;
                default:
                    return AxisInfo();
            }
        }

        void setAxisInfo(Qt::Axis axis, AxisInfo info)
        {
            if (axis == Qt::XAxis)
            {
                m_xAxisInfo = info;
            }
        }

        struct ValueAndBin
        {
            double value;
            u32 bin;
        };

        ValueAndBin getMaxValueAndBin() const;
        double getMaxValue() const { return getMaxValueAndBin().value; }
        u32 getMaxBin() const { return getMaxValueAndBin().bin; }

        void debugDump(bool dumpEmptyBins = true) const;

        double getUnderflow() const { return m_underflow; }
        void setUnderflow(double value) { m_underflow = value; }

        double getOverflow() const { return m_overflow; }
        void setOverflow(double value) { m_overflow = value; }

        Histo1DStatistics calcStatistics(double minX, double maxX) const;
        Histo1DStatistics calcBinStatistics(u32 startBin, u32 onePastEndBin) const;

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

    private:
        AxisBinning m_xAxisBinning;
        AxisInfo m_xAxisInfo;

        double *m_data = nullptr;
        SharedHistoMem m_externalMemory;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        double m_count = 0.0;
        double m_maxValue = 0.0;
        u32 m_maxBin = 0;

        QString m_title;
        QString m_footer;
};

typedef std::shared_ptr<Histo1D> Histo1DPtr;

QTextStream &writeHisto1D(QTextStream &out, Histo1D *histo);
Histo1D *readHisto1D(QTextStream &in);


#endif /* __HISTO1D_H__ */

