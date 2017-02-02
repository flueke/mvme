#ifndef __HISTOGRAMS_H__
#define __HISTOGRAMS_H__

#include "typedefs.h"
#include <cmath>
#include <QString>

namespace analysis
{

class BinnedAxis
{
    public:
        static const s64 Underflow = -1;
        static const s64 Overflow = -2;


        BinnedAxis(u32 nBins, double Min, double Max)
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

//
// Histo1D
//

class Histo1D
{
    public:
        Histo1D(u32 nBins, double xMin, double xMax);
        ~Histo1D();

        // Returns the bin number or -1 in case of under/overflow.
        s32 fill(double x, double weight = 1.0);
        double getValue(double x) const;
        void clear();

        u32 getNumberOfBins() const { return m_nBins; }

        // Returns the bin number for the value x or -1 if x is out of range.
        s64 getBin(double x) const;

        inline double getBinWidth() const { return getWidth() / getNumberOfBins(); }
        double getBinLowEdge(u32 bin) const;
        double getBinCenter(u32 bin) const;
        double getBinContent(u32 bin) const;

        inline double getXMin() const { return m_xMin; }
        inline double getXMax() const { return m_xMax; }
        inline double getWidth() const { return std::abs(m_xMax - m_xMin); }

        double getMaxValue() const { return m_maxValue; }
        u32 getMaxBin() const { return m_maxBin; }

        void debugDump() const;

        QString m_name;

    private:
        u32 m_nBins = 0;
        double m_xMin = 0.0;
        double m_xMax = 0.0;
        double *m_data = nullptr;
        double m_underflow = 0.0;
        double m_overflow = 0.0;

        double m_count = 0.0;
        double m_maxValue = 0.0;
        u32 m_maxBin = 0;
};

//
// Histo2D
//

struct Hist2DStatistics
{
    u32 maxX = 0;
    u32 maxY = 0;
    u32 maxValue = 0;
    s64 entryCount = 0;
};

class Histo2D
{
    public:
        Histo2D(u32 xBins, double xMin, double xMax,
               u32 yBins, double yMin, double yMax);
        ~Histo2D();

        void fill(double x, double y, double weight = 1.0);
        double getValue(double x, double y) const;
        void clear();

        void debugDump() const;

        QString m_name;

    private:
        BinnedAxis m_xAxis;
        BinnedAxis m_yAxis;

        double *m_data = nullptr;

        double m_underflow = 0.0;
        double m_overflow = 0.0;

        Hist2DStatistics m_stats;
};

}

#endif /* __HISTOGRAMS_H__ */
