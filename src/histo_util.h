#ifndef __HISTO_UTIL_H__
#define __HISTO_UTIL_H__

#include <qwt_scale_draw.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_map.h>

#include "typedefs.h"

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

#endif /* __HISTO_UTIL_H__ */
