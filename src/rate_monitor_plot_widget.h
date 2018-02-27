#ifndef __RATE_MONITOR_PLOT_WIDGET_H__
#define __RATE_MONITOR_PLOT_WIDGET_H__

#include <QWidget>

#include "rate_monitor_base.h"
#include "util/plot.h"

class QwtPlot;
class QwtPlotCurve;

struct RateMonitorPlotWidgetPrivate;

class RateMonitorPlotWidget: public QWidget
{
    Q_OBJECT

    public:
        RateMonitorPlotWidget(QWidget *parent = nullptr);
        ~RateMonitorPlotWidget();

        void addRate(const RateHistoryBufferPtr &rateHistory, const QString &title = QString(),
                     const QColor &color = Qt::black);
        void removeRate(const RateHistoryBufferPtr &rateHistory);
        void removeRate(int index);
        int rateCount() const;
        QVector<RateHistoryBufferPtr> getRates() const;
        RateHistoryBufferPtr getRate(int index) const;

        /* Log or lin scaling for the Y-Axis. */
        AxisScale getYAxisScale() const;
        void setYAxisScale(AxisScale scaling);

        /* If true the x-axis runs from [-bufferCapacity, 0).
         * If false the x-axis runs from [0, bufferCapacity). */
        bool isXAxisReversed() const;
        void setXAxisReversed(bool b);

        bool isInternalLegendVisible() const;
        void setInternalLegendVisible(bool b);

        // internal qwt objects
        QwtPlot *getPlot();

        QwtPlotCurve *getPlotCurve(const RateHistoryBufferPtr &rate);
        QwtPlotCurve *getPlotCurve(int index);
        QVector<QwtPlotCurve *> getPlotCurves();

    public slots:

        void replot();

    private slots:
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();

    private:
        std::unique_ptr<RateMonitorPlotWidgetPrivate> m_d;
};


#endif /* __RATE_MONITOR_PLOT_WIDGET_H__ */
