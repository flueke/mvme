#ifndef __HISTO2D_H__
#define __HISTO2D_H__

#include "histo_util.h"
#include <QObject>

struct Histo2DStatistics
{
    u32 maxX = 0;
    u32 maxY = 0;
    u32 maxValue = 0;
    s64 entryCount = 0;
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

        //Hist2DStatistics calcStatistics(QwtInterval xInterval, QwtInterval yInterval) const;

    private:
        AxisBinning m_xAxis;
        AxisBinning m_yAxis;

        double *m_data = nullptr;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        Histo2DStatistics m_stats;
};

#endif /* __HISTO2D_H__ */
