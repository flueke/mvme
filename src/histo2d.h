#ifndef __HISTO2D_H__
#define __HISTO2D_H__

#include "histo_util.h"
#include <QObject>
#include <array>
#include <memory>

struct Histo2DStatistics
{
    u32 maxBinX = 0;        // x bin of max value
    u32 maxBinY = 0;        // y bin of max value
    double maxX = 0.0;      // low edge of maxBinX
    double maxY = 0.0;      // low edge of maxBinY
    double maxValue = 0.0;
    double entryCount = 0;
};

class Histo2D: public QObject
{
    Q_OBJECT
    signals:
        void axisBinningChanged(Qt::Axis axis);

    public:
        Histo2D(u32 xBins, double xMin, double xMax,
                u32 yBins, double yMin, double yMax,
                QObject *parent = 0);
        ~Histo2D();

        void resize(s32 xBins, s32 yBins);

        void fill(double x, double y, double weight = 1.0);
        double getValue(double x, double y) const;
        void clear();

        void debugDump() const;

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

        Histo2DStatistics calcStatistics(AxisInterval xInterval, AxisInterval yInterval) const;

        double getUnderflow() const { return m_underflow; }
        void setUnderflow(double value) { m_underflow = value; }

        double getOverflow() const { return m_overflow; }
        void setOverflow(double value) { m_overflow = value; }

    private:
        std::array<AxisBinning, 2> m_axisBinnings;
        std::array<AxisInfo, 2> m_axisInfos;

        double *m_data = nullptr;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        Histo2DStatistics m_stats;
};

typedef std::shared_ptr<Histo2D> Histo2DPtr;

#endif /* __HISTO2D_H__ */
