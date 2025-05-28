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
#ifndef __HISTO_UTIL_H__
#define __HISTO_UTIL_H__

#include "typedefs.h"

#include <cmath>
#include <memory>
#include <qwt_scale_draw.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_map.h>
#include <qwt_text.h>

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QGroupBox;
class QRadioButton;

// Adapted from: http://stackoverflow.com/a/18593942

// This uses QwtScaleMap to perform the coordinate transformations:
// scaleInterval is the raw histogram resolution
// paintInterval is the unit interval

class UnitConversionAxisScaleDraw: public QwtScaleDraw
{
    public:
        explicit UnitConversionAxisScaleDraw(const QwtScaleMap &conversionMap)
            : m_conversionMap(conversionMap)
        {}

        virtual QwtText label(double value) const override
        {
            double labelValue = m_conversionMap.transform(value);
            auto text = QString::number(labelValue);
            return QwtText(text);
        };

    private:
        QwtScaleMap m_conversionMap;
};

class UnitConversionLinearScaleEngine: public QwtLinearScaleEngine
{
    public:
        UnitConversionLinearScaleEngine(const QwtScaleMap &conversionMap, u32 base=10)
            : QwtLinearScaleEngine(base)
            , m_conversionMap(conversionMap)
        {
        }


        virtual void autoScale(int maxNumSteps, double &x1, double &x2, double &stepSize) const override
        {
            x1 = m_conversionMap.transform(x1);
            x2 = m_conversionMap.transform(x2);
            stepSize = m_conversionMap.transform(stepSize);

            QwtLinearScaleEngine::autoScale(maxNumSteps, x1, x2, stepSize);

            x1 = m_conversionMap.invTransform(x1);
            x2 = m_conversionMap.invTransform(x1);
            stepSize = m_conversionMap.invTransform(stepSize);
        }

        virtual QwtScaleDiv divideScale(double x1, double x2, int maxMajorSteps, int maxMinorSteps, double stepSize=0.0) const override
        {
            x1 = m_conversionMap.transform(x1);
            x2 = m_conversionMap.transform(x2);
            //qDebug() << "stepSize pre" << stepSize;
            //stepSize = m_conversionMap.transform(stepSize);
            //qDebug() << "stepSize post" << stepSize;

            auto scaleDiv = QwtLinearScaleEngine::divideScale(x1, x2, maxMajorSteps, maxMinorSteps, stepSize);

            x1 = m_conversionMap.invTransform(x1);
            x2 = m_conversionMap.invTransform(x2);

            QwtScaleDiv result(x1, x2);

            for (int tickType = 0; tickType < QwtScaleDiv::NTickTypes; ++tickType)
            {
                auto ticks = scaleDiv.ticks(tickType);

                for (int i = 0; i < ticks.size(); ++i)
                    ticks[i] = m_conversionMap.invTransform(ticks[i]);

                result.setTicks(tickType, ticks);
            }

            return result;
        }

    private:
        QwtScaleMap m_conversionMap;
};

// Bounds values to 0.1 to make QwtLogScaleEngine happy
class MinBoundLogTransform: public QwtLogTransform
{
    public:
        explicit MinBoundLogTransform(double minBound = 0.1)
            : m_minBound(minBound)
        {
        }

        virtual double bounded(double value) const
        {
            double result = qBound(m_minBound, value, QwtLogTransform::LogMax);
            return result;
        }

        virtual double transform(double value) const
        {
            double result = QwtLogTransform::transform(bounded(value));
            return result;
        }

        virtual double invTransform(double value) const
        {
            double result = QwtLogTransform::invTransform(value);
            return result;
        }

        virtual QwtTransform *copy() const
        {
            return new MinBoundLogTransform(m_minBound);
        }

    private:
        double m_minBound;
};

QString makeAxisTitle(const QString &title, const QString &unit);

class AxisBinning
{
    public:
        static const s64 Underflow = -1;
        static const s64 Overflow = -2;

        /* Default value for most getters which means to use the number of bins directly
         * and to not perform any resolution reduction. */
        static const u32 NoResolutionReduction = 0;


        AxisBinning()
            : m_nBins(0)
            , m_min(0.0)
            , m_max(0.0)
        {}

        AxisBinning(u32 nBins, double Min, double Max)
            : m_nBins(nBins)
            , m_min(Min)
            , m_max(Max)
        {}

        inline double getMin() const { return m_min; }
        inline double getMax() const { return m_max; }
        inline double getWidth() const { return std::abs(getMax() - getMin()); }

        inline void setMin(double min) { m_min = min; }
        inline void setMax(double max) { m_max = max; }

        inline u32 getBins(u32 rrf = NoResolutionReduction) const
        {
            return rrf == NoResolutionReduction ? m_nBins : m_nBins / rrf;
        }

        inline u32 getBinCount(u32 rrf = NoResolutionReduction) const
        {
            return getBins(rrf);
        }

        inline void setBins(u32 bins) { m_nBins = bins; }
        inline void setBinCount(u32 bins) { setBins(bins); }

        inline double getBinWidth(u32 rrf = NoResolutionReduction) const
        {
            return getWidth() / getBins(rrf);
        }

        inline double getBinLowEdge(u32 bin, u32 rrf = NoResolutionReduction) const
        {
            return getMin() + bin * getBinWidth(rrf);
        }

        inline double getBinCenter(u32 bin, u32 rrf = NoResolutionReduction) const
        {
            return getBinLowEdge(bin, rrf) + getBinWidth(rrf) * 0.5;
        }

        inline double getBinsToUnitsRatio(u32 rrf = NoResolutionReduction) const
        {
            return getBins(rrf) / getWidth();
        }

        // Allows passing a fractional bin number
        inline double getBinLowEdgeFractional(double binFraction,
                                              u32 rrf = NoResolutionReduction) const
        {
            return getMin() + binFraction * getBinWidth(rrf);
        }

        /* Returns the bin number for the value x. Returns Underflow/Overflow
         * if x is out of range. */
        // FIXME: I think this returns 0 for NaNs!
        inline s64 getBin(double x, u32 rrf = NoResolutionReduction) const
        {
            double bin = getBinUnchecked(x, rrf);

            if (bin < 0.0)
                return Underflow;

            if (bin >= getBins(rrf))
                return Overflow;

            return static_cast<s64>(bin);
        }

        inline s64 getBinBounded(double x, u32 rrf = NoResolutionReduction) const
        {
            s64 bin = getBin(x, rrf);

            if (bin == Underflow)
                bin = 0;

            if (bin == Overflow)
                bin = getBinCount(rrf) - 1;

            return bin;
        }

        /* Returns the bin number for the value x. No check is performed if x
         * is in range of the axis. */
        inline double getBinUnchecked(double x, u32 rrf = NoResolutionReduction) const
        {
            double bin = getBinCount(rrf) * (x - m_min) / (m_max - m_min);
            return bin;
        }

        inline bool operator==(const AxisBinning &other)
        {
            return (m_nBins == other.m_nBins
                    && m_min == other.m_min
                    && m_max == other.m_max);
        }

        inline bool operator!=(const AxisBinning &other)
        {
            return !(*this == other);
        }

    private:
        u32 m_nBins;
        double m_min;
        double m_max;
};

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

struct AxisInterval
{
    double minValue;
    double maxValue;
};

inline bool operator==(const AxisInterval &a, const AxisInterval &b)
{
    return (a.minValue == b.minValue && a.maxValue == b.maxValue);
}

struct AxisInfo
{
    QString title;
    QString unit;
};

inline
QString make_title_string(const AxisInfo &axisInfo)
{
    QString result;

    if (!axisInfo.title.isEmpty())
    {
        result = axisInfo.title;
        if (!axisInfo.unit.isEmpty())
        {
            result = QString("%1 <small>[%2]</small>").arg(axisInfo.title).arg(axisInfo.unit);
        }
    }

    return result;
}

static const s32 Histo1DMinBits = 1;
static const s32 Histo1DMaxBits = 20;
static const s32 Histo1DDefBits = 13;

static const s32 Histo2DMinBits = 1;
static const s32 Histo2DMaxBits = 13;
static const s32 Histo2DDefBits = 10;

QComboBox *make_resolution_combo(s32 minBits, s32 maxBits, s32 selectedBits);
// Assumes that selectedRes is a power of 2!
void select_by_resolution(QComboBox *combo, s32 selectedRes);

struct HistoAxisLimitsUI
{
    QFrame *outerFrame;
    QFrame *limitFrame;
    QDoubleSpinBox *spin_min;
    QDoubleSpinBox *spin_max;
    QRadioButton *rb_limited;
    QRadioButton *rb_fullRange;
};

HistoAxisLimitsUI make_axis_limits_ui(const QString &groupBoxTitle,
                                      double inputMin, double inputMax,
                                      double limitMin, double limitMax,
                                      bool isLimited);

class Histo2D;
class Histo1D;

using Histo1DPtr = std::shared_ptr<Histo1D>;
using Histo1DList = QVector<Histo1DPtr>;

Histo1DPtr make_x_projection(Histo2D *histo);
Histo1DPtr make_x_projection(Histo2D *histo,
                             double startX, double endX,
                             double startY, double endY);

Histo1DPtr make_y_projection(Histo2D *histo);
Histo1DPtr make_y_projection(Histo2D *histo,
                             double startX, double endX,
                             double startY, double endY);

Histo1DPtr make_projection(Histo2D *histo, Qt::Axis axis);
Histo1DPtr make_projection(Histo2D *histo, Qt::Axis axis,
                           double startX, double endX,
                           double startY, double endY);

Histo1DPtr make_projection(const Histo1DList &histos, Qt::Axis axis,
                           double startX, double endX,
                           double startY, double endY);

Histo1DList slice(Histo2D *histo, Qt::Axis axis,
    double startX, double endX, double startY, double endY,
    ResolutionReductionFactors rrfs = {});

Histo1DPtr add(const Histo1D &a, const Histo1D &b);
Histo1DPtr add(const Histo1DList &histos);

#endif /* __HISTO_UTIL_H__ */
