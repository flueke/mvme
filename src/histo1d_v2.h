#ifndef __MVME_HISTO1D_V2_H__
#define __MVME_HISTO1D_V2_H__

#include "histo_util.h"

class AbstractHisto
{
    public:
        virtual ~AbstractHisto() {};

        virtual size_t getStorageSize() const = 0;
        virtual size_t getAxisCount() const = 0;
        virtual size_t getNumberOfBins(Qt::Axis = Qt::XAxis) const = 0;
        //virtual double getBinContent(size_t bin) const = 0;
        //virtual void setBinConent(size_t bin, double value);
};

class AbstractHisto1D: public AbstractHisto
{
    public:
        size_t getAxisCount() const override
        {
            return 1;
        }
};

#endif /* __MVME_HISTO1D_V2_H__ */
