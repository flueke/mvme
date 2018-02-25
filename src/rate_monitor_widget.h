#ifndef __RATE_MONITOR_WIDGET_H__
#define __RATE_MONITOR_WIDGET_H__

#include <QWidget>
#include <memory>
#include "analysis/analysis.h"

/* Similar design to Histo1DWidget:
 * Must be able to display a plain RateHistoryBuffer, a RateSampler and a list
 * of RateSamplers. The list part could be moved into an extra
 * RateMonitorListWidget. */

struct RateMonitorWidgetPrivate;

class RateMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        using SinkPtr = std::shared_ptr<analysis::RateMonitorSink>;
        using SinkModifiedCallback = std::function<void (const SinkPtr &)>;

        RateMonitorWidget(const a2::RateSamplerPtr &sampler, QWidget *parent = nullptr);
        RateMonitorWidget(a2::RateSampler *sampler, QWidget *parent = nullptr);
        RateMonitorWidget(const QVector<a2::RateSamplerPtr> &samplers, QWidget *parent = nullptr);
        virtual ~RateMonitorWidget();

        void setSink(const SinkPtr &sink, SinkModifiedCallback sinkModifiedCallback);

        /* Returns the shared rate samplers set on this widget. In case the
         * constructor taking a raw RateSampler * was used and empty vector is
         * returned .*/
        QVector<a2::RateSamplerPtr> getRateSamplers() const;

    private slots:
        void replot();
        void exportPlot();
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void updateStatistics();
        void yAxisScalingChanged();

    private:
        RateMonitorWidget(QWidget *parent = nullptr);

        std::unique_ptr<RateMonitorWidgetPrivate> m_d;
};

#endif /* __RATE_MONITOR_WIDGET_H__ */
