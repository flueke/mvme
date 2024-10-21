#ifndef F66C9539_DA00_4A40_802A_F2101420A636
#define F66C9539_DA00_4A40_802A_F2101420A636

#include <mesytec-mvlc/cpp_compat.h>

#include "histo_ui.h"
#include "mvme_qwt.h"
#include "mdpp-sampling/waveform_interpolation.h"

namespace mesytec::mvme::waveforms
{

// Does not take ownership of any of the underlying data containers!
// Only works for increasing and non-negative x values for now.

// It must behave like an iterator over Sample instances.
template<typename It>
QRectF calculate_bounding_rect(const It &begin, const It &end)
{
    auto minMax = std::minmax_element(begin, end,
        [](const auto &a, const auto &b) { return a.second < b.second; });

    if (begin < end && minMax.first != end && minMax.second != end)
    {
        double minY = minMax.first->second;
        double maxY = minMax.second->second;
        double maxX = (end - 1)->first; // last sample must contain the largest x coordinate

        QPointF topLeft(0, maxY);
        QPointF bottomRight(maxX, minY);
        return QRectF(topLeft, bottomRight);
    }

    return {};
}

template<typename Waveform>
QRectF calculate_bounding_rect(const Waveform &waveform)
{
    return calculate_bounding_rect(std::begin(waveform), std::end(waveform));
}

template<typename T>
QRectF calculate_trace_bounding_rect(const T &xs, const T &ys)
{
    if (xs.empty() || ys.empty())
        return {};

    auto xminmax = std::minmax_element(std::begin(xs), std::end(xs));
    auto yminmax = std::minmax_element(std::begin(ys), std::end(ys));

    auto [xmin, xmax] = std::make_pair(*xminmax.first, *xminmax.second);
    auto [ymin, ymax] = std::make_pair(*yminmax.first, *yminmax.second);

    QPointF topLeft(xmin, ymax);
    QPointF bottomRight(xmax, ymin);

    return QRectF(topLeft, bottomRight);
}

inline QRectF calculate_trace_bounding_rect(const Trace &trace)
{
    return calculate_trace_bounding_rect(trace.xs, trace.ys);
}

// It must iterate over objects with a QRectF Foo::boundingRect() method.
template<typename It>
QRectF unite_bounding_rects(const It begin, const It &end)
{
    return std::accumulate(begin, end, QRectF{},
        [](QRectF &acc, const auto &obj)
        {
            return acc.united(obj.boundingRect());
        });
}

QRectF update_plot_axes(QwtPlot *plot, QwtPlotZoomer *zoomer, const QRectF &newBoundingRect, const QRectF &prevBoundingRect = {});

// The std::vector<double> xs / std::vector<double> ys based approach.
// Does not take ownership of any underlying trace data.
class WaveformPlotData: public QwtSeriesData<QPointF>
{
    public:
        void setTrace(Trace *trace)
        {
            trace_ = trace;
            boundingRectCache_ = {};
        }

        Trace *getTrace() const { return trace_; }

        QRectF boundingRect() const override
        {
            if (!boundingRectCache_.isValid())
                boundingRectCache_ = calculateBoundingRect();

            return boundingRectCache_;
        }

        size_t size() const override
        {
            return trace_ ? trace_->size() : 0u;
        }

        QPointF sample(size_t i) const override
        {
            return i < size() ? QPointF(trace_->xs[i], trace_->ys[i]) : QPointF{};
        }

        QRectF calculateBoundingRect() const
        {
            return trace_ ? calculate_trace_bounding_rect(*trace_) : QRectF{};
        }

    private:
        Trace *trace_ = nullptr;
        mutable QRectF boundingRectCache_;
};

// y axis is the trace index, x axis is trace x, z color is trace y
class WaveformCollectionVerticalRasterData: public QwtMatrixRasterData
{
    public:
        void setTraceCollection(const std::vector<const Trace *> &traces)
        {
            traces_ = traces;
        }

        std::vector<const Trace *> getTraceCollection() const
        {
            return traces_;
        }

        double value(double x, double y) const override
        {
            const ssize_t traceIndex = y;

            if (0 > traceIndex || traceIndex >= static_cast<ssize_t>(traces_.size()))
                return mesytec::mvme::util::make_quiet_nan();

            auto trace = traces_[traceIndex];
            assert(trace);

            const ssize_t sampleIndex = x;

            if (0 > sampleIndex || sampleIndex >= static_cast<ssize_t>(trace->size()))
                return mesytec::mvme::util::make_quiet_nan();

            return trace->ys[sampleIndex];
        }

        QRectF pixelHint(const QRectF &) const override
        {
            QRectF result
            {
                0.0, 0.0,
                1.0, 1.0
            };
            return result;
        }

    private:
        std::vector<const Trace *> traces_;

};

struct WaveformCurves
{
    std::unique_ptr<QwtPlotCurve> rawCurve;
    std::unique_ptr<QwtPlotCurve> interpolatedCurve;
};

struct RawWaveformCurves
{
    QwtPlotCurve *rawCurve = nullptr;
    QwtPlotCurve *interpolatedCurve = nullptr;
};

class IWaveformPlotter
{
    public:
        using Handle = size_t;

        virtual ~IWaveformPlotter();
        virtual Handle addWaveform(WaveformCurves &&data) = 0;
        virtual WaveformCurves takeWaveform(Handle handle) = 0;
        virtual RawWaveformCurves getWaveform(Handle handle) const = 0;
        virtual QwtPlotCurve *getRawCurve(Handle handle) const = 0;
        virtual QwtPlotCurve *getInterpolatedCurve(Handle handle) const = 0;

        bool detachWaveform(Handle handle) { return takeWaveform(handle).rawCurve != nullptr; };
};

class WaveformPlotCurveHelper: public IWaveformPlotter
{
    public:
        using Handle = size_t;

        explicit WaveformPlotCurveHelper(QwtPlot *plot = nullptr);

        QwtPlot *getPlot() const;

        Handle addWaveform(WaveformCurves &&data) override;
        WaveformCurves takeWaveform(Handle handle) override;
        RawWaveformCurves getWaveform(Handle handle) const override;
        QwtPlotCurve *getRawCurve(Handle handle) const override;
        QwtPlotCurve *getInterpolatedCurve(Handle handle) const override;

        void setRawSymbolsVisible(Handle handle, bool visible);
        void setInterpolatedSymbolsVisible(Handle handle, bool visible);

    private:

        struct WaveformData: public RawWaveformCurves
        {
            mvme_qwt::QwtSymbolCache rawSymbolCache;
            mvme_qwt::QwtSymbolCache interpolatedSymbolCache;
        };

        WaveformData *getWaveformData(Handle handle);
        const WaveformData *getWaveformData(Handle handle) const;

        QwtPlot *plot_ = nullptr;
        std::vector<WaveformData> waveforms_;
};

WaveformCurves make_curves(QColor curvePenColor = Qt::black);

#if 0
class WaveformPlotWidget: public histo_ui::PlotWidget, public IWaveformPlotter
{
    Q_OBJECT
    public:
        using Handle = size_t;

        WaveformPlotWidget(QWidget *parent = nullptr);
        ~WaveformPlotWidget() override;

        Handle addWaveform(WaveformCurves &&data) override;
        WaveformCurves takeWaveform(Handle handle) override;
        RawWaveformCurves getWaveform(Handle handle) const override;
        bool detachWaveform(Handle handle);
        QwtPlotCurve *getRawCurve(Handle handle) override;
        QwtPlotCurve *getInterpolatedCurve(Handle handle) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
#endif

}

#endif /* F66C9539_DA00_4A40_802A_F2101420A636 */
