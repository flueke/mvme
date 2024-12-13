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

    QPointF p0(xmin, ymin);
    QPointF p1(xmax, ymax);

    return QRectF(p0, p1);
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

// y axis is the trace index, x axis is trace x, z color is trace y.
// FIXME: (not true right now) assumes uniform x step size for all traces.
class WaveformCollectionVerticalRasterData: public QwtMatrixRasterData
{
    public:
#ifndef QT_NO_DEBUG
    /* Counts the number of samples obtained by qwt when doing a replot. Has to be atomic
     * as QwtPlotSpectrogram::renderImage() uses threaded rendering internally.
     * The number of samples heavily depends on the result of the pixelHint() method and
     * is performance critical. */
    mutable std::atomic<u64> m_sampledValuesForLastReplot;
#endif

        WaveformCollectionVerticalRasterData()
    #ifndef QT_NO_DEBUG
            : m_sampledValuesForLastReplot(0u)
    #endif
        {}

        virtual void initRaster(const QRectF &area, const QSize &raster) override
        {
            #ifndef QT_NO_DEBUG
            m_sampledValuesForLastReplot = 0u;
            #endif
            QwtRasterData::initRaster(area, raster);
        }

        virtual void discardRaster() override
        {
            //qDebug() << __PRETTY_FUNCTION__ << this << "sampled values for last replot: " << m_sampledValuesForLastReplot;
            #ifndef QT_NO_DEBUG
            m_sampledValuesForLastReplot = 0u;
            #endif
            QwtRasterData::discardRaster();
        }

        void setTraceCollection(const std::vector<const Trace *> &traces, double xStep)
        {
            traces_ = traces;
            xStep_ = xStep;
            //qDebug() << fmt::format("WaveformCollectionVerticalRasterData::setTraceCollection(): traces.size()={}, xStep={}", traces.size(), xStep).c_str();
        }

        std::vector<const Trace *> getTraceCollection() const { return traces_; }

        double value(double x, double y) const override
        {
#ifndef QT_NO_DEBUG
            m_sampledValuesForLastReplot++;
#endif

            if (auto trace = getTraceForY(y); trace && !trace->empty())
            {
                #if 0 // TODO: use this once interpolation yields equidistant x values
                const ssize_t sampleIndex = x / xStep_;

                qDebug() << fmt::format("WaveformCollectionVerticalRasterData::value(): x={}, y={}, xstep={}, sampleIndex(x)={}, trace.size()={}",
                    x, y, xStep_, sampleIndex, trace->size()).c_str();

                if (0 <= sampleIndex && sampleIndex < static_cast<ssize_t>(trace->size()))
                {
                    qDebug() << fmt::format("WaveformCollectionVerticalRasterData::value(): input x={}, sampleIndex={}, trace x={}",
                        x, sampleIndex, trace->xs[sampleIndex]).c_str();
                    return trace->ys[sampleIndex];
                }
                else
                {
                    qDebug() << fmt::format("WaveformCollectionVerticalRasterData::value(): sampleIndex out of bounds: x={}, sampleIndex={}", x, sampleIndex).c_str();
                }
                #else
                // FIXME: such a hack because of non uniform x distances...
                if (auto it = std::lower_bound(std::begin(trace->xs), std::end(trace->xs), x); it != std::end(trace->xs))
                {
                    const auto index = std::distance(std::begin(trace->xs), it);
                    //qDebug() << fmt::format("WaveformCollectionVerticalRasterData::value(): x={}, y={}, xstep={}, sampleIndex(x)={}, trace.size()={}",
                    //    x, y, xStep_, index, trace->size()).c_str();
                    return trace->ys[index];
                }
                else
                {
                    //qDebug() << fmt::format("WaveformCollectionVerticalRasterData::value(): no sample for x={}, returning trace.back()=({}, {})",
                    //    x, trace->xs.back(), trace->ys.back()).c_str();
                    return trace->ys.back();
                }
                #endif
            }
            else
            {
                //qDebug() << fmt::format("WaveformCollectionVerticalRasterData::value(): no trace for y={}", y).c_str();
            }

            return mesytec::mvme::util::make_quiet_nan();
        }

        QRectF pixelHint(const QRectF &) const override
        {
            QRectF result{0.0, 0.0, xStep_ , 1.0};
            //qDebug() << "returning WaveformCollectionVerticalRasterData::pixelHint():" << result;
            return result;
        }

    private:
        const Trace *getTraceForY(double y) const
        {
            const ssize_t traceIndex = y;

            if (0 > traceIndex || traceIndex >= static_cast<ssize_t>(traces_.size()))
                return nullptr;

            return traces_[traceIndex];
        }

        std::vector<const Trace *> traces_;
        double xStep_ = 1.0;
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

        mvme_qwt::QwtSymbolCache getRawSymbolCache(Handle handle) const;
        mvme_qwt::QwtSymbolCache getInterpolatedSymbolCache(Handle handle) const;

        size_t size() const;
        size_t capacity() const { return waveforms_.size(); };

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

struct WaveformProcessingData
{
    waveforms::TraceHistories &analysisTraceData;
    waveforms::TraceHistories &rawDisplayTraces;
    waveforms::TraceHistories &interpolatedDisplayTraces;
};

// For each channel in analysisTraceData:
// - take the latest trace from the front of the channels trace history,
// - scale x by dtSample then interpolate while limiting each channels history to maxDepth.
// Trace memory is reused once maxDepth is reached.
void post_process_waveforms(
    const waveforms::TraceHistories &analysisTraceData,
    waveforms::TraceHistories &rawDisplayTraces,
    waveforms::TraceHistories &interpolatedDisplayTraces,
    double dtSample,
    int interpolationFactor,
    size_t maxDepth,
    bool doPhaseCorrection);

// Like post_process_waveforms() but processes the analysisTraceData snapshot as
// is. This means the result contains exactly the traces contained in the input
// snapshot, it's not built up using only the latest trace per input channel.
// => Traces from different channels but with the same index in the channels
// trace history are from the same analysis event.
// => Way more expensive than post_process_waveforms() as it processes all traces.
// TODO: add a starting traceIndex and a maxTraceCount parameter or in the UI
// crate a copy of the trace data containing only the traces to be displayed,
// then call post_process_waveform_snapshot() on that copy.
void post_process_waveform_snapshot(
    const waveforms::TraceHistories &analysisTraceData,
    waveforms::TraceHistories &rawDisplayTraces,
    waveforms::TraceHistories &interpolatedDisplayTraces,
    double dtSample,
    int interpolationFactor,
    bool doPhaseCorrection);

// Reprocess waveforms to account for changed dtSample and interpolationFactor
// values.
// The traces in rawDisplayTraces are rescaled by dtSample, then interpolated
// data is written to interpolatedDisplayTraces.
void reprocess_waveforms(
    waveforms::TraceHistories &rawDisplayTraces,
    waveforms::TraceHistories &interpolatedDisplayTraces,
    double dtSample,
    int interpolationFactor,
    bool doPhaseCorrection);

}

#endif /* F66C9539_DA00_4A40_802A_F2101420A636 */
