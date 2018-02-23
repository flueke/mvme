#ifndef __RATE_MONITOR_WIDGET_H__
#define __RATE_MONITOR_WIDGET_H__

#include <QWidget>
#include <memory>

struct RateMonitorWidgetPrivate;

class RateMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        RateMonitorWidget(QWidget *parent = nullptr);
        virtual ~RateMonitorWidget();

    private slots:
        void replot();
        void exportPlot();
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void updateStatistics();
        void yAxisScalingChanged();

    private:
        std::unique_ptr<RateMonitorWidgetPrivate> m_d;
};

#endif /* __RATE_MONITOR_WIDGET_H__ */
