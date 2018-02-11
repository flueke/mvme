#ifndef __RATE_MONITORING_H__
#define __RATE_MONITORING_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include <QWidget>
#include <cpp11-on-multicore/common/rwlock.h>

class QwtPlot;
class QwtPlotCurve;
struct RateMonitorPlotWidgetPrivate;

using RateHistoryBuffer = boost::circular_buffer<double>;
using RateHistoryBufferPtr = std::shared_ptr<RateHistoryBuffer>;

struct RateHistory
{
    RateHistoryBuffer buffer;
    NonRecursiveRWLock lock;
};

inline double get_max_value(const RateHistoryBuffer &rh)
{
    auto max_it = std::max_element(rh.begin(), rh.end());
    double max_value = (max_it == rh.end()) ? 0.0 : *max_it;
    return max_value;
}

inline QRectF get_bounding_rect(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.capacity(), max_value);
    return result;
}

enum class AxisScale
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

        // internal qwt objects
        QwtPlot *getPlot();
        QwtPlotCurve *getPlotCurve();

    public slots:
        void setYAxisScale(AxisScale scaling);
        AxisScale getYAxisScale() const;

        void replot();

    private slots:
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();

    private:
        std::unique_ptr<RateMonitorPlotWidgetPrivate> m_d;
};

#endif /* __RATE_MONITORING_H__ */
