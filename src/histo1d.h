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

        // Returns the bin number or -1 in case of under/overflow.
        s32 fill(double x, double weight = 1.0);
        double getValue(double x) const;
        void clear();

        inline u32 getNumberOfBins() const { return m_xAxis.getBins(); }

        inline double getBinContent(u32 bin) const { return (bin < getNumberOfBins()) ? m_data[bin] : 0.0; }
        bool setBinContent(u32 bin, double value);

        inline double getXMin() const { return m_xAxis.getMin(); }
        inline double getXMax() const { return m_xAxis.getMax(); }
        inline double getWidth() const { return m_xAxis.getWidth(); }

        inline double getBinWidth() const { return m_xAxis.getBinWidth(); }
        inline double getBinLowEdge(u32 bin) const { return m_xAxis.getBinLowEdge(bin); }
        inline double getBinCenter(u32 bin) const { return m_xAxis.getBinCenter(bin); }

        //inline const AxisBinning &getAxisBinning() const { return m_xAxis; }

        inline double getEntryCount() const { return m_count; }
        double getMaxValue() const { return m_maxValue; }
        u32 getMaxBin() const { return m_maxBin; }

        void debugDump(bool dumpEmptyBins = true) const;

        double getUnderflow() const { return m_underflow; }
        void setUnderflow(double value) { m_underflow = value; }

        double getOverflow() const { return m_overflow; }
        void setOverflow(double value) { m_overflow = value; }

        Histo1DStatistics calcStatistics(double minX, double maxX) const;
        Histo1DStatistics calcBinStatistics(u32 startChannel, u32 onePastEndChannel) const;

    private:
        AxisBinning m_xAxis;

        double *m_data = nullptr;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        double m_count = 0.0;
        double m_maxValue = 0.0;
        u32 m_maxBin = 0;
};

QTextStream &writeHisto1D(QTextStream &out, Histo1D *histo);
Histo1D *readHisto1D(QTextStream &in);


#endif /* __HISTO1D_H__ */
