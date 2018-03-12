#include "rate_monitor_plot_widget.h"

#include <QBoxLayout>
#include <QDebug>

#include <qwt_curve_fitter.h>
#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>
#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>

#include "analysis/a2/util/nan.h"
#include "histo_util.h"
#include "qt_util.h"
#include "scrollzoomer.h"
#include "util/assert.h"
#include "util/counters.h"

//
// RateMonitorPlotWidget
//

static inline QRectF make_bounding_rect(const RateSampler *sampler)
{
    QRectF result;

    if (!sampler->rateHistory.empty())
    {
        double xMin = sampler->getFirstSampleTime();
        double xMax = sampler->getLastSampleTime();
        double yMax = get_max_value(sampler->rateHistory);

        result = QRectF(xMin * 1000.0, 0.0,
                        xMax * 1000.0, yMax);
    }

    return result;
}

struct RateMonitorPlotData: public QwtSeriesData<QPointF>
{
    RateMonitorPlotData(const RateSamplerPtr &sampler)
        : QwtSeriesData<QPointF>()
        , sampler(sampler)
    { }

    size_t size() const override
    {
        return sampler->historySize();
    }

    virtual QPointF sample(size_t i) const override
    {
        double x = sampler->getSampleTime(i) * 1000.0;
        double y = sampler->getSample(i);

        QPointF result(x, y);
#if 0
        auto rateHistory = &sampler->rateHistory;

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
        auto result = make_bounding_rect(sampler.get());
        qDebug() << __PRETTY_FUNCTION__ << result;
        return result;
    }

    RateSamplerPtr sampler;
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

    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex =" << m_d->m_zoomer->zoomRectIndex();

    /* NOTE: using connect with the c++ pointer-to-member syntax with these qwt signals does
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
    curve->setData(new RateMonitorPlotData(sampler));
    curve->setPen(color);
    curve->setStyle(QwtPlotCurve::Lines);
    //curve->setCurveAttribute(QwtPlotCurve::Fitted);
    //curve->setCurveFitter(new QwtSplineCurveFitter);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);
    curve->attach(m_d->m_plot);

    m_d->m_curves.push_back(curve.release());
    m_d->m_samplers.push_back(sampler);

    qDebug() << "added rate. title =" << title << ", color =" << color
        << ", capacity =" << sampler->historyCapacity()
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
    /* Things that have to happen:
     * - calculate stats for the visible area. use this to scale y if y auto scaling is desired
     * - update info display
     * - update cursor info
     * - update axis titles
     * - update window title
     * - update projections
     */


    // updateAxisScales
    static const double ScaleFactor = 1.05;

    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    bool haveSamples = false;

    for (auto &sampler: m_d->m_samplers)
    {
        if (!sampler->rateHistory.empty())
        {
            haveSamples = true;

            xMin = std::min(xMin, sampler->getFirstSampleTime());
            xMax = std::max(xMax, sampler->getLastSampleTime());
#if 0
            //yMax = std::max(yMax, get_max_value(sampler->rateHistory));

            auto stats = calc_rate_sampler_stats(*sampler, visibleXInterval_s);

            auto yInterval = stats.intervals[Qt::YAxis];

            if (!std::isnan(yInterval.minValue) && !std::isnan(yInterval.maxValue))
            {
                yMin = std::min(yMin, yInterval.minValue);
                yMax = std::max(yMax, yInterval.maxValue);
                haveYMinMax = true;
            }
#endif
        }
    }

    if (haveSamples)
    {
        // scale x-values to milliseconds
        double xMin_ms = xMin * 1000.0;
        double xMax_ms = xMax * 1000.0;

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        // If fully zoomed out set the x-axis to full resolution and the y-axis to
        // the min/max values in the visible area.
        if (m_d->m_zoomer->zoomRectIndex() == 0)
        {
            qDebug() << __PRETTY_FUNCTION__ << "fully zoomed out -> setting x scale to:" << xMin << xMax;
            m_d->m_plot->setAxisScale(QwtPlot::xBottom, xMin_ms, xMax_ms);
            AxisInterval visibleXInterval_s = { xMin, xMax };

            bool haveYMinMax = false;

            for (auto &sampler: m_d->m_samplers)
            {
                if (!sampler->rateHistory.empty())
                {
                    auto stats = calc_rate_sampler_stats(*sampler, visibleXInterval_s);
                    auto yInterval = stats.intervals[Qt::YAxis];

                    if (!std::isnan(yInterval.minValue) && !std::isnan(yInterval.maxValue))
                    {
                        yMin = std::min(yMin, yInterval.minValue);
                        yMax = std::max(yMax, yInterval.maxValue);
                        haveYMinMax = true;
                    }
                }
            }

            if (haveYMinMax)
            {
                qDebug() << __PRETTY_FUNCTION__
                    << "found y minmax for visible x range. auto scaling y and setting zoomBase";
                switch (getYAxisScale())
                {
                    case AxisScale::Linear:
                        yMax *= ScaleFactor;
                        break;

                    case AxisScale::Logarithmic:
                        yMax = std::pow(yMax, ScaleFactor);
                        break;
                }

                m_d->m_plot->setAxisScale(QwtPlot::yLeft,   yMin, yMax);
                m_d->m_zoomer->setZoomBase();
            }
        }
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
            static const double LogScaleMinBound = 0.1;
            auto scaleEngine = new QwtLogScaleEngine;
            scaleEngine->setTransformation(new MinBoundLogTransform(LogScaleMinBound));
            m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
            m_d->m_plot->setAxisAutoScale(QwtPlot::yLeft, true);
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
