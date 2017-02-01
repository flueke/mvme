#include "histogram_widgets.h"
#include "histograms.h"
#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <QHBoxLayout>
#include <QDebug>

namespace analysis
{

class Hist1DPointData: public QwtSeriesData<QPointF>
{
    public:
        Hist1DPointData(Histo1D *histo)
            : m_histo(histo)
        {}

        virtual size_t size() const override
        {
            return m_histo->getNumberOfBins();
        }

        virtual QPointF sample(size_t i) const override
        {
            auto result = QPointF(
                m_histo->getBinLowEdge(i),
                m_histo->getBinContent(i));

            qDebug() << __PRETTY_FUNCTION__
                << "i =" << i
                << "result =" << result
                ;
            return result;
        }

        virtual QRectF boundingRect() const override
        {
            return QRectF(0, 0, m_histo->getWidth(), m_histo->getMaxValue());
        }

    private:
        Histo1D *m_histo;
};

Hist1DWidget::Hist1DWidget(Histo1D *histo, QWidget *parent)
    : QWidget(parent)
    , m_histo(histo)
    , m_plot(new QwtPlot)
    , m_plotCurve(new QwtPlotCurve)
{
    auto layout = new QHBoxLayout(this);
    layout->addWidget(m_plot);

    m_plot->setAxisAutoScale(QwtPlot::xBottom, true);
    m_plot->setAxisAutoScale(QwtPlot::yLeft, true);

    m_plotCurve->setStyle(QwtPlotCurve::Steps);
    m_plotCurve->setCurveAttribute(QwtPlotCurve::Inverted);
    m_plotCurve->attach(m_plot);
    m_plotCurve->setData(new Hist1DPointData(m_histo));
}

}
