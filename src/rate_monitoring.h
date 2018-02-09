#ifndef __RATE_MONITORING_H__
#define __RATE_MONITORING_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include <QWidget>

class QwtPlot;
class QwtPlotCurve;
struct RateMonitorPlotWidgetPrivate;

using RateHistoryBuffer = boost::circular_buffer<double>;
using RateHistoryBufferPtr = std::shared_ptr<RateHistoryBuffer>;

enum class AxisScaling
{
    Linear,
    Logarithmic
};

class RateMonitorPlotWidget: public QWidget
{
    Q_OBJECT

    public:
        RateMonitorPlotWidget(QWidget *parent = nullptr);
        ~RateMonitorPlotWidget();

        void setRateHistoryBuffer(const RateHistoryBufferPtr &buffer);
        RateHistoryBufferPtr getRateHistoryBuffer() const;

        // internal stuff
        QwtPlot *getPlot();
        QwtPlotCurve *getPlotCurve();

    public slots:
        void setXScaling(AxisScaling scaling);

        void replot();

    private slots:
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();

    private:
        std::unique_ptr<RateMonitorPlotWidgetPrivate> m_d;
};

#endif /* __RATE_MONITORING_H__ */
