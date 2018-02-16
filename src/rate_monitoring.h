#ifndef __RATE_MONITORING_H__
#define __RATE_MONITORING_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include <QWidget>
#include "typedefs.h"
#include "globals.h"
#include "mvme_context.h"


class QwtPlot;
class QwtPlotCurve;
struct RateMonitorPlotWidgetPrivate;

using RateHistoryBuffer = boost::circular_buffer<double>;
using RateHistoryBufferPtr = std::shared_ptr<RateHistoryBuffer>;

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

struct RateMonitorInfo
{
};

class RateMonitorRegistry
{
    public:
        using Handle = s32;
        static const Handle InvalidHandle = -1;

        Handle addRate(const QString &path, const RateMonitorInfo &rateInfo);
        Handle getHandle(const QString &path);

        struct Entry
        {
            enum class Type
            {
                Invalid,
                Rate,
                Directory,
                Array,
            };

            Type type;

            RateMonitorInfo info;
            RateHistoryBufferPtr history;
            double lastValue;

            QVector<Entry> children;

            operator bool() const { return type != Type::Invalid; }
        };

        Entry getEntry(Handle handle) const;
        Entry &getEntry(Handle handle);
        Entry getEntry(const QString &path) const;
        Entry getParent(const QString &path) const;
        Entry getParent(const Entry &entry) const;
        QString getPath(const Entry &entry) const;
        QString getName(const Entry &entry) const;
};

struct StreamProcRateMonitorHandles
{
    using Handle = RateMonitorRegistry::Handle;

    Handle bytesProcessed;
    Handle buffersProcessed;
    Handle buffersWithErrors;
    Handle eventSections;
    Handle invalidEventIndices;

    using ModuleHandles = std::array<Handle, MaxVMEModules>;
    std::array<ModuleHandles, MaxVMEEvents> moduleHandles;
    std::array<Handle, MaxVMEEvents> eventHandles;
};

struct RateMonitorWidgetPrivate;

class RateMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        RateMonitorWidget(RateMonitorRegistry *reg, MVMEContext *context, QWidget *parent = nullptr);
        ~RateMonitorWidget();

    private slots:
        void sample();

    private:
        std::unique_ptr<RateMonitorWidgetPrivate> m_d;
};

// ModuleHandles path:
// - Directory streamproc
// - Array     moduleHandles[MaxVMEEvents]
// Each modulesHandles entry has a child of the following form:
// - Array     Rate[MaxVMEModules]

#endif /* __RATE_MONITORING_H__ */
