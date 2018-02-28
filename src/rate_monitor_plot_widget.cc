#include "rate_monitor_plot_widget.h"

#include <QBoxLayout>
#include <QDebug>

#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>

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

// FIXME: use sampler->interval in here to get this correct for samplers that use a different interval than 1.0s
static inline QRectF make_bounding_rect(const RateSampler *sampler)
{
    double xMin = 0.0;
    double xMax = sampler->rateHistory.size();

    if (sampler->totalSamples > sampler->rateHistory.capacity())
    {
        xMin = sampler->totalSamples - sampler->rateHistory.capacity();
        xMax = xMin + sampler->rateHistory.size();
    }

    QRectF result(xMin * 1000.0, 0.0,
                  xMax * 1000.0, get_max_value(sampler->rateHistory));

    return result;
}

struct RateMonitorPlotData: public QwtSeriesData<QPointF>
{
    RateMonitorPlotData(const RateSamplerPtr &sampler, RateMonitorPlotWidget *plotWidget)
        : QwtSeriesData<QPointF>()
        , sampler(sampler)
        , plotWidget(plotWidget)
    { }

    size_t size() const override
    {
        return sampler->rateHistory.size();
    }

    virtual QPointF sample(size_t i) const override
    {
        auto rateHistory = &sampler->rateHistory;

        double x = (sampler->totalSamples - rateHistory->size() + i) * 1000.0;
        double y = rateHistory->at(i);

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
        //return get_qwt_bounding_rect_size(sampler->rateHistory);
        auto result = make_bounding_rect(sampler.get());
        qDebug() << __PRETTY_FUNCTION__ << result;
        return result;
    }

    RateSamplerPtr sampler;
    RateMonitorPlotWidget *plotWidget;
};

struct RateMonitorPlotWidgetPrivate
{
    QVector<RateSamplerPtr> m_samplers;

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

    // XXX: playing with QwtDateScaleEngine and QwtDateScaleDraw
    {
        auto engine = new QwtDateScaleEngine(Qt::UTC);
        m_d->m_plot->setAxisScaleEngine(QwtPlot::xBottom, engine);

        auto draw = new QwtDateScaleDraw(Qt::UTC);
        draw->setDateFormat(QwtDate::Millisecond,   QSL("H'h' m'm' s's' zzz'ms'"));
        draw->setDateFormat(QwtDate::Second,        QSL("H'h' m'm' s's'"));
        draw->setDateFormat(QwtDate::Minute,        QSL("H'h' m'm'"));
        draw->setDateFormat(QwtDate::Hour,          QSL("H'h' m'm'"));
        draw->setDateFormat(QwtDate::Day,           QSL("d 'd'"));

        m_d->m_plot->setAxisScaleDraw(QwtPlot::xBottom, draw);
    }

    m_d->m_plotLegendItem.attach(m_d->m_plot);

    // zoomer
    m_d->m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());
    m_d->m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);

    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex =" << m_d->m_zoomer->zoomRectIndex();

    /* NOTE: using connect with the c++ pointer to member syntax with these qwt signals does
     * not work for some reason. */
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

void RateMonitorPlotWidget::addRateSampler(const RateSamplerPtr &sampler,
                                           const QString &title,
                                           const QColor &color)
{
    assert(sampler);
    assert(m_d->m_samplers.size() == m_d->m_curves.size());

    auto curve = std::make_unique<QwtPlotCurve>(title);
    curve->setData(new RateMonitorPlotData(sampler, this));
    curve->setPen(color);
    //curve->setStyle(QwtPlotCurve::Lines);
    curve->attach(m_d->m_plot);

    m_d->m_curves.push_back(curve.release());
    m_d->m_samplers.push_back(sampler);

    qDebug() << "added rate. title =" << title << ", color =" << color
        << ", capacity =" << sampler->rateHistory.capacity()
        << ", new plot count =" << m_d->m_curves.size();

    assert(m_d->m_samplers.size() == m_d->m_curves.size());
}

void RateMonitorPlotWidget::removeRateSampler(const RateSamplerPtr &sampler)
{
    removeRateSampler(m_d->m_samplers.indexOf(sampler));
}

void RateMonitorPlotWidget::removeRateSampler(int index)
{
    assert(m_d->m_samplers.size() == m_d->m_curves.size());

    if (0 <= index && index < m_d->m_samplers.size())
    {
        assert(index < m_d->m_curves.size());

        qDebug() << __PRETTY_FUNCTION__ << "removing plot with index" << index;
        auto curve = std::unique_ptr<QwtPlotCurve>(m_d->m_curves.at(index));
        curve->detach();
        m_d->m_curves.remove(index);
        m_d->m_samplers.remove(index);
    }

    assert(m_d->m_samplers.size() == m_d->m_curves.size());
}

int RateMonitorPlotWidget::rateCount() const
{
    assert(m_d->m_samplers.size() == m_d->m_curves.size());
    return m_d->m_samplers.size();
}

QVector<RateSamplerPtr> RateMonitorPlotWidget::getRateSamplers() const
{
    assert(m_d->m_samplers.size() == m_d->m_curves.size());
    return m_d->m_samplers;
}

RateSamplerPtr RateMonitorPlotWidget::getRateSampler(int index) const
{
    return m_d->m_samplers.value(index);
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

    // FIXME: use sampler->interval in here to get this correct for samplers that use a different interval than 1.0s
    double xMin = 0.0;
    double xMax = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;

    for (auto &sampler: m_d->m_samplers)
    {
        xMin = std::max(xMin, (sampler->totalSamples - sampler->rateHistory.size()));
        xMax = std::max(xMax, xMin + static_cast<double>(sampler->rateHistory.size()));
        yMax = std::max(yMax, get_max_value(sampler->rateHistory));
    }

    // scale x-value to milliseconds
    xMin *= 1000.0;
    xMax *= 1000.0;

    switch (getYAxisScale())
    {
        case AxisScale::Linear:
            yMin = 0.0;
            yMax *= ScaleFactor;
            break;

        case AxisScale::Logarithmic:
            yMin = 0.001;
            yMax = std::pow(yMax, ScaleFactor);
            break;
    }

    // This sets a fixed y axis scale effectively overriding any changes made
    // by the scrollzoomer.
    m_d->m_plot->setAxisScale(QwtPlot::yLeft, yMin, yMax);


    // If fully zoomed out set the x-axis to full resolution
    if (m_d->m_zoomer->zoomRectIndex() == 0)
    {
        m_d->m_plot->setAxisScale(QwtPlot::xBottom, xMin, xMax);
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
            //scaleEngine->setTransformation(new MinBoundLogTransform);
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

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(const RateSamplerPtr &sampler)
{
    int index = m_d->m_samplers.indexOf(sampler);

    if (0 < index && index < m_d->m_samplers.size())
    {
        assert(index < m_d->m_curves.size());
        return m_d->m_curves.at(index);
    }

    return nullptr;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(int index)
{
    if (0 < index && index < m_d->m_samplers.size())
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
