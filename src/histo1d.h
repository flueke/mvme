#ifndef __HISTO1D_H__
#define __HISTO1D_H__

#include "histo_util.h"
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
};

class Histo1D: public QObject
{
    Q_OBJECT
    public:
        Histo1D(u32 nBins, double xMin, double xMax, QObject *parent = 0);
        ~Histo1D();

        void resize(u32 nBins);

        // Returns the bin number or -1 in case of under/overflow.
        s32 fill(double x, double weight = 1.0);
        double getValue(double x) const;
        void clear();

        inline u32 getNumberOfBins() const { return m_xAxisBinning.getBins(); }

        inline double getBinContent(u32 bin) const { return (bin < getNumberOfBins()) ? m_data[bin] : 0.0; }
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
            if (axis == Qt::XAxis)
            {
                m_xAxisBinning = binning;
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

        inline double getEntryCount() const { return m_count; }
        double getMaxValue() const { return m_maxValue; }
        u32 getMaxBin() const { return m_maxBin; }

        void debugDump(bool dumpEmptyBins = true) const;

        double getUnderflow() const { return m_underflow; }
        void setUnderflow(double value) { m_underflow = value; }

        double getOverflow() const { return m_overflow; }
        void setOverflow(double value) { m_overflow = value; }

        Histo1DStatistics calcStatistics(double minX, double maxX) const;
        Histo1DStatistics calcBinStatistics(u32 startBin, u32 onePastEndBin) const;

    private:
        AxisBinning m_xAxisBinning;
        AxisInfo m_xAxisInfo;

        double *m_data = nullptr;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        double m_count = 0.0;
        double m_maxValue = 0.0;
        u32 m_maxBin = 0;
};

typedef std::shared_ptr<Histo1D> Histo1DPtr;

QTextStream &writeHisto1D(QTextStream &out, Histo1D *histo);
Histo1D *readHisto1D(QTextStream &in);


#endif /* __HISTO1D_H__ */
