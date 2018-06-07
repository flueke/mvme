#ifndef __RATE_MONITOR_PLOT_WIDGET_H__
#define __RATE_MONITOR_PLOT_WIDGET_H__

#include <QWidget>

#include "libmvme_export.h"
#include "rate_monitor_base.h"
#include "util/plot.h"

class QwtPlot;
class QwtPlotCurve;
class ScrollZoomer;

struct RateMonitorPlotWidgetPrivate;

class LIBMVME_EXPORT RateMonitorPlotWidget: public QWidget
{
    Q_OBJECT

    public:
        RateMonitorPlotWidget(QWidget *parent = nullptr);
        ~RateMonitorPlotWidget();

        void addRateSampler(const RateSamplerPtr &sampler, const QString &title = QString(),
                            const QColor &color = Qt::black);
        void removeRateSampler(const RateSamplerPtr &sampler);
        void removeRateSampler(int index);
        int rateCount() const;
        QVector<RateSamplerPtr> getRateSamplers() const;
        RateSamplerPtr getRateSampler(int index) const;

        /* Log or lin scaling for the Y-Axis. */
        AxisScale getYAxisScale() const;
        void setYAxisScale(AxisScale scaling);

        bool isInternalLegendVisible() const;
        void setInternalLegendVisible(bool b);

        // internal qwt objects
        QwtPlot *getPlot();

        QwtPlotCurve *getPlotCurve(const RateSamplerPtr &rate) const;
        QwtPlotCurve *getPlotCurve(int index) const;
        QVector<QwtPlotCurve *> getPlotCurves() const;
        ScrollZoomer *getZoomer() const;

    public slots:
        void replot();

    private slots:
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();

    private:
        std::unique_ptr<RateMonitorPlotWidgetPrivate> m_d;
};

/* Returns a bounding rect for the use with qwt components.
 * This version uses the capacity of the buffer for the x-axis max value. */
inline QRectF get_qwt_bounding_rect_capacity(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.capacity() * 1000.0, max_value);
    return result;
}

/* Returns a bounding rect for the use with qwt components.
 * This version uses the current size of buffer for the x-axis max value. */
inline QRectF get_qwt_bounding_rect_size(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.size() * 1000.0, max_value);
    return result;
}

#endif /* __RATE_MONITOR_PLOT_WIDGET_H__ */
