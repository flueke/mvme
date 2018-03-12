#ifndef __RATE_MONITOR_WIDGET_H__
#define __RATE_MONITOR_WIDGET_H__

#include <memory>
#include <QDir>
#include <QWidget>
#include "analysis/analysis.h"

/* Similar design to Histo1DWidget:
 * Must be able to display a plain RateHistoryBuffer, a RateSampler and a list
 * of RateSamplers. The list part could be moved into an extra
 * RateMonitorListWidget.
 *
 * Note: limited to displaying a list of RateSamplerPtr, a single
 * RateSamplerPtr or a single raw RateSampler *. The reason is that RateSampler
 * should carry all required information for the widget (except an
 * objectName()).
 *
 * Note: most functionality is only available if in addition to the rates a
 * sink has also been set using setSink(). Not the best design right now but
 * I'm still figuring out what is needed and what isn't.
 * */

struct RateMonitorWidgetPrivate;

class RateMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        using SinkPtr = std::shared_ptr<analysis::RateMonitorSink>;
        using SinkModifiedCallback = std::function<void (const SinkPtr &)>;

        RateMonitorWidget(const a2::RateSamplerPtr &sampler, QWidget *parent = nullptr);
        RateMonitorWidget(const QVector<a2::RateSamplerPtr> &samplers, QWidget *parent = nullptr);
        virtual ~RateMonitorWidget();

        void setSink(const SinkPtr &sink, SinkModifiedCallback sinkModifiedCallback);
        void setPlotExportDirectory(const QDir &dir);

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

        friend struct RateMonitorWidgetPrivate;
        std::unique_ptr<RateMonitorWidgetPrivate> m_d;
};

#endif /* __RATE_MONITOR_WIDGET_H__ */
