#include "rate_monitoring.h"

#include <QDebug>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_legenditem.h>
#include <QBoxLayout>

#include "analysis/a2/util/nan.h"
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
    RateMonitorPlotData(const RateHistoryBufferPtr &buffer)
        : QwtSeriesData<QPointF>()
        , buffer(buffer)
    { }

    size_t size() const override
    {
        return buffer->capacity();
    }

    virtual QPointF sample(size_t i) const override
    {
        size_t offset = buffer->capacity() - buffer->size();
        ssize_t bufferIndex = i - offset;

        double y = 0.0;

        if (0 <= bufferIndex && bufferIndex < static_cast<ssize_t>(buffer->size()))
        {
            y = (*buffer)[bufferIndex];
        }

        QPointF result(i, y);

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
        auto max_it = std::max_element(buffer->begin(), buffer->end());
        double max_value = (max_it == buffer->end()) ? 0.0 : *max_it;

        auto result = QRectF(0.0, 0.0,
                             buffer->capacity(), max_value);

        return result;
    }

    RateHistoryBufferPtr buffer;
};


struct RateMonitorPlotWidgetPrivate
{
    RateHistoryBufferPtr m_buffer;

    QwtPlot *m_plot;
    ScrollZoomer *m_zoomer;
    QwtPlotCurve m_plotCurve;
    QwtPlotLegendItem m_plotLegendItem;
};

RateMonitorPlotWidget::RateMonitorPlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorPlotWidgetPrivate>())
{
    // plot and curve
    m_d->m_plot = new QwtPlot(this);
    m_d->m_plot->canvas()->setMouseTracking(true);
    m_d->m_plotCurve.attach(m_d->m_plot);
    m_d->m_plotLegendItem.attach(m_d->m_plot);

    // zoomer
    m_d->m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());
    m_d->m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_d->m_zoomer->zoomBase();
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
}

RateMonitorPlotWidget::~RateMonitorPlotWidget()
{
}

void RateMonitorPlotWidget::setRateHistoryBuffer(const RateHistoryBufferPtr &buffer)
{
    m_d->m_buffer = buffer;

    //m_d->m_plotCurve.setRenderHint(QwtPlotItem::RenderAntialiased, true);
#if 0
    m_d->m_plotCurve.setStyle(QwtPlotCurve::Lines);
    m_d->m_plotCurve.setCurveAttribute(QwtPlotCurve::Fitted);
#else
    m_d->m_plotCurve.setStyle(QwtPlotCurve::Steps);
    //m_d->m_plotCurve.setCurveAttribute(QwtPlotCurve::Inverted);
#endif

#if 0
    auto pen = m_d->m_plotCurve.pen();
    pen.setWidth(2);
    m_d->m_plotCurve.setPen(pen);
#endif

    m_d->m_plotCurve.setData(new RateMonitorPlotData(buffer));
    m_d->m_plotCurve.setTitle("Rate 1");

    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex pre setZoomBase =" << m_d->m_zoomer->zoomRectIndex();
    m_d->m_zoomer->setZoomBase(true); // doReplot=true
    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex post setZoomBase =" << m_d->m_zoomer->zoomRectIndex();
}

RateHistoryBufferPtr RateMonitorPlotWidget::getRateHistoryBuffer() const
{
    return m_d->m_buffer;
}

void RateMonitorPlotWidget::setXScaling(AxisScaling scaling)
{
}

void RateMonitorPlotWidget::replot()
{
    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex pre replot =" << m_d->m_zoomer->zoomRectIndex();
    m_d->m_plot->replot();
    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex post replot =" << m_d->m_zoomer->zoomRectIndex();

    if (m_d->m_zoomer->zoomRectIndex() == 0)
    {
        auto dataRect = m_d->m_plotCurve.dataRect();
        dataRect.setHeight(dataRect.height() * 1.05);
        m_d->m_zoomer->setZoomBase(dataRect);
        m_d->m_zoomer->zoomBase();
        qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex post updating zoomRect =" << m_d->m_zoomer->zoomRectIndex();
    }
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
