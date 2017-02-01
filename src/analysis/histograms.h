#ifndef __HISTOGRAMS_H__
#define __HISTOGRAMS_H__

#include "typedefs.h"
#include <cmath>

namespace analysis
{

//
// Histo1D
//

class Histo1D
{
    public:
        Histo1D(u32 nBins, double xMin, double xMax);
        ~Histo1D();

        void fill(double x, double weight = 1.0);
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

        void fill(double x, double y, double weight = 1.0) {}
        double value(double x, double y) { return 3.14159;}
};

}

#endif /* __HISTOGRAMS_H__ */
