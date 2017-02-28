#ifndef __HISTO2D_H__
#define __HISTO2D_H__

#include "histo_util.h"
#include <QObject>
#include <memory>

struct Histo2DStatistics
{
    double maxX = 0.0;
    double maxY = 0.0;
    double maxValue = 0.0;
    double entryCount = 0;
};

class Histo2D: public QObject
{
    Q_OBJECT
    public:
        Histo2D(u32 xBins, double xMin, double xMax,
                u32 yBins, double yMin, double yMax,
                QObject *parent = 0);
        ~Histo2D();

        void fill(double x, double y, double weight = 1.0);
        double getValue(double x, double y) const;
        void clear();

        void debugDump() const;

        AxisBinning getAxis(Qt::Axis axis) const
        {
            switch (axis)
            {
                case Qt::XAxis:
                    return m_xAxis;
                case Qt::YAxis:
                    return m_yAxis;
                default:
                    return AxisBinning();
            }
        }

        AxisInterval getInterval(Qt::Axis axis) const;

        //Hist2DStatistics calcStatistics(QwtInterval xInterval, QwtInterval yInterval) const;

    private:
        AxisBinning m_xAxis;
        AxisBinning m_yAxis;

        double *m_data = nullptr;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        Histo2DStatistics m_stats;
};

typedef std::shared_ptr<Histo2D> Histo2DPtr;

#endif /* __HISTO2D_H__ */
