#ifndef __MVME_HISTO1D_V2_H__
#define __MVME_HISTO1D_V2_H__

#include "histo_util.h"

class Histo
{
    public:
        virtual ~Histo() {};

        virtual size_t getAxisCount() const = 0;
        virtual size_t getNumberOfBins(Qt::Axis axis = Qt::XAxis) const = 0;
        virtual size_t getBinContent(size_t binIndex, Qt::Axis axis = Qt::XAxis) const = 0;
        virtual bool setBinContent(size_t binIndex, double value,  Qt::Axis = Qt::XAxis) = 0;
        virtual size_t getStorageSize() const = 0;

        virtual AxisBinning getBinning(Qt::Axis axis = Qt::XAxis) const = 0;

};

class Histo1D: public Histo
{
    public:
        size_t getAxisCount() const override
        {
            return 1;
        }
};

class HistoResolutionReducer: public Histo
{
    public:
        HistoResolutionReducer(Histo *base, unsigned rrf = 0);

    private:
        Histo *m_histo;
};

#endif /* __MVME_HISTO1D_V2_H__ */
