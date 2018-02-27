#include "rate_monitor_plot_widget.h"

#include <QBoxLayout>
#include <QDebug>

#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>

#include "analysis/a2/util/nan.h"
#include "histo_util.h"
#include "scrollzoomer.h"
#include "util/assert.h"
#include "mvme_stream_processor.h"
#include "util/counters.h"
#include "sis3153_readout_worker.h"
#include "util/bihash.h"

//
// RateMonitorPlotWidget
//

struct RateMonitorPlotData: public QwtSeriesData<QPointF>
{
    RateMonitorPlotData(const RateHistoryBufferPtr &rateHistory, RateMonitorPlotWidget *plotWidget)
        : QwtSeriesData<QPointF>()
        , rateHistory(rateHistory)
        , plotWidget(plotWidget)
    { }

    size_t size() const override
    {
        return rateHistory->capacity();
    }

    virtual QPointF sample(size_t i) const override
    {
        size_t offset = rateHistory->capacity() - rateHistory->size();
        ssize_t bufferIndex = i - offset;

        double x = 0.0;
        double y = 0.0;

        if (plotWidget->isXAxisReversed())
            x = -(static_cast<double>(rateHistory->capacity()) - i);
        else
            x = i;

        if (0 <= bufferIndex && bufferIndex < static_cast<ssize_t>(rateHistory->size()))
            y = (*rateHistory)[bufferIndex];

        QPointF result(x, y);

#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "sample =" << i
            << ", offset =" << offset
            << ", bufferIndex =" << bufferIndex
            << ", buffer->size =" << rateHistory->size()
            << ", buffer->cap =" << rateHistory->capacity()
            << ", result =" << result;
#endif

        return result;
    }

    virtual QRectF boundingRect() const override
    {
        return get_qwt_bounding_rect(*rateHistory);
    }

    RateHistoryBufferPtr rateHistory;
    RateMonitorPlotWidget *plotWidget;
};

struct RateMonitorPlotWidgetPrivate
{
    QVector<RateHistoryBufferPtr> m_rates;
    bool m_xAxisReversed = false;

    QwtPlot *m_plot;
    ScrollZoomer *m_zoomer;
    QVector<QwtPlotCurve *> m_curves;
    QwtPlotLegendItem m_plotLegendItem;
};

RateMonitorPlotWidget::RateMonitorPlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorPlotWidgetPrivate>())
{
    // plot and curve
    m_d->m_plot = new QwtPlot(this);
    m_d->m_plot->canvas()->setMouseTracking(true);
    m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle("Rate");
    m_d->m_plotLegendItem.attach(m_d->m_plot);

    // zoomer
    m_d->m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());
    m_d->m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);

    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex =" << m_d->m_zoomer->zoomRectIndex();

    TRY_ASSERT(connect(m_d->m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));
    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &RateMonitorPlotWidget::mouseCursorMovedToPlotCoord));
    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorLeftPlot,
                       this, &RateMonitorPlotWidget::mouseCursorLeftPlot));

    // layout
    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->setSpacing(0);
    widgetLayout->addWidget(m_d->m_plot);

    setYAxisScale(AxisScale::Linear);
}

RateMonitorPlotWidget::~RateMonitorPlotWidget()
{
    qDeleteAll(m_d->m_curves);
}

void RateMonitorPlotWidget::addRate(const RateHistoryBufferPtr &rateHistory,
                                    const QString &title,
                                    const QColor &color)
{
    assert(rateHistory);
    assert(m_d->m_rates.size() == m_d->m_curves.size());

    auto curve = std::make_unique<QwtPlotCurve>(title);
    curve->setData(new RateMonitorPlotData(rateHistory, this));
    curve->setPen(color);
    curve->attach(m_d->m_plot);

    m_d->m_curves.push_back(curve.release());
    m_d->m_rates.push_back(rateHistory);

    qDebug() << "added rate. title =" << title << ", color =" << color
        << ", capacity =" << rateHistory->capacity()
        << ", new plot count =" << m_d->m_curves.size();

    assert(m_d->m_rates.size() == m_d->m_curves.size());
}

void RateMonitorPlotWidget::removeRate(const RateHistoryBufferPtr &rateHistory)
{
    removeRate(m_d->m_rates.indexOf(rateHistory));
}

void RateMonitorPlotWidget::removeRate(int index)
{
    assert(m_d->m_rates.size() == m_d->m_curves.size());

    if (0 <= index && index < m_d->m_rates.size())
    {
        assert(index < m_d->m_curves.size());

        qDebug() << __PRETTY_FUNCTION__ << "removing plot with index" << index;
        auto curve = std::unique_ptr<QwtPlotCurve>(m_d->m_curves.at(index));
        curve->detach();
        m_d->m_curves.remove(index);
        m_d->m_rates.remove(index);
    }

    assert(m_d->m_rates.size() == m_d->m_curves.size());
}

int RateMonitorPlotWidget::rateCount() const
{
    assert(m_d->m_rates.size() == m_d->m_curves.size());
    return m_d->m_rates.size();
}

QVector<RateHistoryBufferPtr> RateMonitorPlotWidget::getRates() const
{
    assert(m_d->m_rates.size() == m_d->m_curves.size());
    return m_d->m_rates;
}

RateHistoryBufferPtr RateMonitorPlotWidget::getRate(int index) const
{
    return m_d->m_rates.value(index);
}

bool RateMonitorPlotWidget::isXAxisReversed() const
{
    return m_d->m_xAxisReversed;
}

void RateMonitorPlotWidget::setXAxisReversed(bool b)
{
    m_d->m_xAxisReversed = b;
    m_d->m_zoomer->setZoomBase(false);
    replot();
}

bool RateMonitorPlotWidget::isInternalLegendVisible() const
{
    return m_d->m_plotLegendItem.plot();
}

void RateMonitorPlotWidget::setInternalLegendVisible(bool visible)
{
    if (visible && !m_d->m_plotLegendItem.plot())
        m_d->m_plotLegendItem.attach(m_d->m_plot);
    else if (!visible && m_d->m_plotLegendItem.plot())
        m_d->m_plotLegendItem.detach();
}

void RateMonitorPlotWidget::replot()
{
    // updateAxisScales
    static const double ScaleFactor = 1.05;

    double xMax = 0.0;
    double yMax = 0.0;

    for (auto &rate: m_d->m_rates)
    {
        xMax = std::max(xMax, static_cast<double>(rate->capacity()));
        yMax = std::max(yMax, get_max_value(*rate));
    }

    double base = 0.0;

    switch (getYAxisScale())
    {
        case AxisScale::Linear:
            base = 0.0;
            yMax *= ScaleFactor;
            break;

        case AxisScale::Logarithmic:
            base = 0.1;
            yMax = std::pow(yMax, ScaleFactor);
            break;
    }

    // This sets a fixed y axis scale effectively overriding any changes made
    // by the scrollzoomer.
    m_d->m_plot->setAxisScale(QwtPlot::yLeft, base, yMax);


    // If fully zoomed out set the x-axis to full resolution
    if (m_d->m_zoomer->zoomRectIndex() == 0)
    {
        if (isXAxisReversed())
            m_d->m_plot->setAxisScale(QwtPlot::xBottom, -xMax, 0.0);
        else
            m_d->m_plot->setAxisScale(QwtPlot::xBottom, 0.0, xMax);

        m_d->m_zoomer->setZoomBase();
    }

    m_d->m_plot->updateAxes();
    m_d->m_plot->replot();
}

static bool axis_is_lin(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLinearScaleEngine *>(plot->axisScaleEngine(axis));
}

static bool axis_is_log(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLogScaleEngine *>(plot->axisScaleEngine(axis));
}

void RateMonitorPlotWidget::setYAxisScale(AxisScale scaling)
{
    switch (scaling)
    {
        case AxisScale::Linear:
            m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
            m_d->m_plot->setAxisAutoScale(QwtPlot::yLeft, true);
            break;

        case AxisScale::Logarithmic:
            auto scaleEngine = new QwtLogScaleEngine;
            //scaleEngine->setTransformation(new MinBoundLogTransform); // TODO enable
            m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
            break;
    }

    replot();
}

AxisScale RateMonitorPlotWidget::getYAxisScale() const
{
    if (axis_is_lin(m_d->m_plot, QwtPlot::yLeft))
        return AxisScale::Linear;

    assert(axis_is_log(m_d->m_plot, QwtPlot::yLeft));

    return AxisScale::Logarithmic;
}

void RateMonitorPlotWidget::zoomerZoomed(const QRectF &)
{
    qDebug() << __PRETTY_FUNCTION__ << m_d->m_zoomer->zoomRectIndex();
    replot();
}

void RateMonitorPlotWidget::mouseCursorMovedToPlotCoord(QPointF)
{
}

void RateMonitorPlotWidget::mouseCursorLeftPlot()
{
}

QwtPlot *RateMonitorPlotWidget::getPlot()
{
    return m_d->m_plot;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(const RateHistoryBufferPtr &rate)
{
    int index = m_d->m_rates.indexOf(rate);

    if (0 < index && index < m_d->m_rates.size())
    {
        assert(index < m_d->m_curves.size());
        return m_d->m_curves.at(index);
    }

    return nullptr;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(int index)
{
    if (0 < index && index < m_d->m_rates.size())
    {
        assert(index < m_d->m_curves.size());
        return m_d->m_curves.at(index);
    }

    return nullptr;
}

QVector<QwtPlotCurve *> RateMonitorPlotWidget::getPlotCurves()
{
    return m_d->m_curves;
}
