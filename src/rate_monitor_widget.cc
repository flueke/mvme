#include "rate_monitor_widget.h"

#include "rate_monitor_plot_widget.h"

struct RateMonitorWidgetPrivate
{
    RateMonitorPlotWidget *m_plotWidget;
};

RateMonitorWidget::RateMonitorWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorWidgetPrivate>())
{
    m_d->m_plotWidget = new RateMonitorPlotWidget;
}

RateMonitorWidget::~RateMonitorWidget()
{
}

void RateMonitorWidget::replot()
{
}

void RateMonitorWidget::exportPlot()
{
}

void RateMonitorWidget::zoomerZoomed(const QRectF &)
{
}

void RateMonitorWidget::mouseCursorMovedToPlotCoord(QPointF)
{
}

void RateMonitorWidget::mouseCursorLeftPlot()
{
}

void RateMonitorWidget::updateStatistics()
{
}

void RateMonitorWidget::yAxisScalingChanged()
{
}
