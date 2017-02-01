#ifndef __HISTOGRAM_WIDGETS_H__
#define __HISTOGRAM_WIDGETS_H__

#include <QWidget>

class QwtPlot;
class QwtPlotCurve;

namespace analysis
{

class Histo1D;

class Hist1DWidget: public QWidget
{
    Q_OBJECT
    public:
        Hist1DWidget(Histo1D *histo, QWidget *parent = 0);

    private:
        Histo1D *m_histo;
        QwtPlot *m_plot;
        QwtPlotCurve *m_plotCurve;
};

}

#endif /* __HISTOGRAM_WIDGETS_H__ */
