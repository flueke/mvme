#include "rate_monitoring.h"

#include <QBoxLayout>
#include <QDebug>
#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_engine.h>

#include "analysis/a2/util/nan.h"
#include "histo_util.h"
#include "scrollzoomer.h"
#include "util/assert.h"

// Write a Plot Widget and a qwt raster data implementation using the circular buffer
// Make the plot fill from first to last but "right aligned"
// -> The plot widget should always display the last N entries for a buffer of capacity N.
// If the buffer has not reached full capacity yet the "missing" entries should
// be set to zero (use NaN?) and not counted at all.
// TODO: Make a base widget that allows adding multiple history buffers


struct RateMonitorPlotData: public QwtSeriesData<QPointF>
{
    RateMonitorPlotData(const RateHistoryBufferPtr &buffer, RateMonitorPlotWidget *plotWidget)
        : QwtSeriesData<QPointF>()
        , buffer(buffer)
        , plotWidget(plotWidget)
    { }

    size_t size() const override
    {
        return buffer->capacity();
    }

    virtual QPointF sample(size_t i) const override
    {
        size_t offset = buffer->capacity() - buffer->size();
        ssize_t bufferIndex = i - offset;

        double x = 0.0;
        double y = 0.0;

        if (plotWidget->isXAxisReversed())
            x = -(static_cast<double>(buffer->capacity()) - i);
        else
            x = i;

        if (0 <= bufferIndex && bufferIndex < static_cast<ssize_t>(buffer->size()))
            y = (*buffer)[bufferIndex];

        QPointF result(x, y);

#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "sample =" << i
            << ", offset =" << offset
            << ", bufferIndex =" << bufferIndex
            << ", buffer->size =" << buffer->size()
            << ", buffer->cap =" << buffer->capacity()
            << ", result =" << result;
#endif


        return result;
    }

    virtual QRectF boundingRect() const override
    {
        return get_bounding_rect(*buffer);
    }

    RateHistoryBufferPtr buffer;
    RateMonitorPlotWidget *plotWidget;
};


struct RateMonitorPlotWidgetPrivate
{
    RateHistoryBufferPtr m_buffer;
    bool m_xAxisReversed = false;

    QwtPlot *m_plot;
    ScrollZoomer *m_zoomer;
    QwtPlotCurve m_plotCurve;
    QwtPlotLegendItem m_plotLegendItem;

    RateMonitorPlotData *data()
    {
        return reinterpret_cast<RateMonitorPlotData *>(m_plotCurve.data());
    }
};

RateMonitorPlotWidget::RateMonitorPlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorPlotWidgetPrivate>())
{
    // plot and curve
    m_d->m_plot = new QwtPlot(this);
    m_d->m_plot->canvas()->setMouseTracking(true);
    m_d->m_plotCurve.attach(m_d->m_plot);
    //m_d->m_plotLegendItem.attach(m_d->m_plot);

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
}

void RateMonitorPlotWidget::setRateHistoryBuffer(const RateHistoryBufferPtr &buffer)
{
    m_d->m_buffer = buffer;

    m_d->m_plotCurve.setData(new RateMonitorPlotData(buffer, this));
    m_d->m_plotCurve.setTitle("Rate 1");
}

RateHistoryBufferPtr RateMonitorPlotWidget::getRateHistoryBuffer() const
{
    return m_d->m_buffer;
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

void RateMonitorPlotWidget::replot()
{
    // updateAxisScales
    if (m_d->m_buffer)
    {
        static const double ScaleFactor = 1.05;
        double maxValue = get_max_value(*m_d->m_buffer);
        double base = 0.0;

        switch (getYAxisScale())
        {
            case AxisScale::Linear:
                base = 0.0;
                maxValue = maxValue * ScaleFactor;
                break;

            case AxisScale::Logarithmic:
                base = 1.0;
                maxValue = std::pow(maxValue, ScaleFactor);
                break;
        }

        // This sets a fixed y axis scale effectively overriding any changes made
        // by the scrollzoomer.
        m_d->m_plot->setAxisScale(QwtPlot::yLeft, base, maxValue);

        // If fully zoomed out set the x-axis to full resolution
        if (m_d->m_zoomer->zoomRectIndex() == 0)
        {
            if (isXAxisReversed())
                m_d->m_plot->setAxisScale(QwtPlot::xBottom, -1.0 * m_d->m_buffer->capacity(), 0);
            else
                m_d->m_plot->setAxisScale(QwtPlot::xBottom, 0, m_d->m_buffer->capacity());
            m_d->m_zoomer->setZoomBase();
        }
        m_d->m_plot->updateAxes();
    }

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
            scaleEngine->setTransformation(new MinBoundLogTransform);
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

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve()
{
    return &m_d->m_plotCurve;
}
