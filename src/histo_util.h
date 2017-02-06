#ifndef __HISTO_UTIL_H__
#define __HISTO_UTIL_H__

#include "typedefs.h"

#include <qwt_scale_draw.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_map.h>

// Adapted from: http://stackoverflow.com/a/18593942

// This uses QwtScaleMap to perform the coordinate transformations:
// scaleInterval is the raw histogram resolution
// paintInterval is the unit interval

class UnitConversionAxisScaleDraw: public QwtScaleDraw
{
    public:
        UnitConversionAxisScaleDraw(const QwtScaleMap &conversionMap)
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
        virtual double bounded(double value) const
        {
            double result = qBound(0.1, value, QwtLogTransform::LogMax);
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
            return new MinBoundLogTransform;
        }
};

QString makeAxisTitle(const QString &title, const QString &unit);

class AxisBinning
{
    public:
        static const s64 Underflow = -1;
        static const s64 Overflow = -2;


        AxisBinning(u32 nBins, double Min, double Max)
            : m_nBins(nBins)
            , m_min(Min)
            , m_max(Max)
        {}

        inline double getMin() const { return m_min; }
        inline double getMax() const { return m_max; }
        inline double getWidth() const { return std::abs(getMax() - getMin()); }

        inline u32 getBins() const { return m_nBins; }
        inline double getBinWidth() const { return getWidth() / getBins(); }
        inline double getBinLowEdge(u32 bin) const { return getMin() + bin * getBinWidth(); }
        inline double getBinCenter(u32 bin) const { return getBinLowEdge(bin) + getBinWidth() * 0.5; }

        inline s64 getBin(double x) const
        {
            if (x < getMin())
                return Underflow;

            if (x >= getMax())
                return Overflow;

            double binWidth = getBinWidth();
            u32 bin = static_cast<u32>(std::floor(x / binWidth));

            return bin;
        }

    private:
        u32 m_nBins;
        double m_min;
        double m_max;
};

#endif /* __HISTO_UTIL_H__ */
