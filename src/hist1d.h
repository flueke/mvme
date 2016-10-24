#ifndef __HIST1D_H__
#define __HIST1D_H__

#include "util.h"

/* TODO: implement Hist1D and Hist1DWidget. The Widget should use QwtPlotHistogram to display the histogram. */

class Hist1D: public QObject
{
    public:
        Hist1D(u32 bits, QObject *parent = 0);

        u32 getResolution() const { return 1 << m_bits; }
        u32 getBits() const { return m_bits; }

        double value(u32 x) const;

        void fill(u32 x, u32 weight=1);
        void inc(u32 x);

        void clear();

    private:
        u32 m_bits;
};

class Hist1DWidget: public MVMEWidget
{
    Q_OBJECT
    public:
        Hist1DWidget(Hist1D *histo, QWidget *parent = 0);
        ~Hist1DWidget();

    private:
        Hist1D *m_histo;
};

#endif /* __HIST1D_H__ */
