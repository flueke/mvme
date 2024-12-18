#include "waveform_plotting.h"
#include <qwt_symbol.h>

#include "mdpp-sampling/mdpp_decode.h"

namespace mesytec::mvme::waveforms
{

QRectF update_plot_axes(QwtPlot *plot, QwtPlotZoomer *zoomer, const QRectF &newBoundingRect, const QRectF &prevBoundingRect)
{
    if (!prevBoundingRect.isValid() || zoomer->zoomRectIndex() == 0)
    {
        auto xMin = newBoundingRect.left();
        auto xMax = newBoundingRect.right();
        auto yMin = newBoundingRect.top();
        auto yMax = newBoundingRect.bottom();

        if (xMin > xMax) std::swap(xMin, xMax); // just in case the limits are
        if (yMin > yMax) std::swap(yMin, yMax); // still swapped for some reason

        spdlog::trace("forcing axis scales to: xMin={}, xMax={}, yMin={}, yMax={}", xMin, xMax, yMin, yMax);

        plot->setAxisScale(QwtPlot::xBottom, xMin, xMax);
        histo_ui::adjust_y_axis_scale(plot, yMin, yMax);
        plot->updateAxes();

        if (zoomer->zoomRectIndex() == 0)
        {
            spdlog::trace("updatePlotAxisScales(): zoomer fully zoomed out -> setZoomBase()");
            zoomer->setZoomBase();
        }

        return newBoundingRect;
    }

    return prevBoundingRect;
}

IWaveformPlotter::~IWaveformPlotter() {}

WaveformPlotCurveHelper::WaveformPlotCurveHelper(QwtPlot *plot)
    : plot_(plot)
{
}

QwtPlot *WaveformPlotCurveHelper::getPlot() const
{
    return plot_;
}

WaveformPlotCurveHelper::Handle WaveformPlotCurveHelper::addWaveform(WaveformCurves &&data)
{
    auto dest = std::find_if(std::begin(waveforms_), std::end(waveforms_),
        [](const auto &curves) { return !curves.rawCurve && !curves.interpolatedCurve; });

    if (dest == std::end(waveforms_))
    {
        waveforms_.emplace_back();
        dest = std::end(waveforms_) - 1;
    }

    *dest = {};

    if (data.rawCurve)
    {
        data.rawCurve->attach(getPlot());
        dest->rawCurve = data.rawCurve.release();
    }

    if (data.interpolatedCurve)
    {
        data.interpolatedCurve->attach(getPlot());
        dest->interpolatedCurve = data.interpolatedCurve.release();
    }

    if (auto symbol = dest->rawCurve->symbol())
        dest->rawSymbolCache = mvme_qwt::make_cache_from_symbol(dest->rawCurve->symbol());

    if (auto symbol = dest->interpolatedCurve->symbol())
        dest->interpolatedSymbolCache = mvme_qwt::make_cache_from_symbol(dest->interpolatedCurve->symbol());

    return std::distance(std::begin(waveforms_), dest);
}

WaveformCurves WaveformPlotCurveHelper::takeWaveform(Handle handle)
{
    if (handle >= waveforms_.size())
        return {};

    auto &curves = waveforms_[handle];

    if (curves.rawCurve)
        curves.rawCurve->detach();

    if (curves.interpolatedCurve)
        curves.interpolatedCurve->detach();

    WaveformCurves result;
    result.rawCurve.reset(curves.rawCurve);
    result.interpolatedCurve.reset(curves.interpolatedCurve);

    curves = {};

    return result;
}

WaveformPlotCurveHelper::WaveformData *WaveformPlotCurveHelper::getWaveformData(Handle handle)
{
    return handle < waveforms_.size() ? &waveforms_[handle] : nullptr;
}

const WaveformPlotCurveHelper::WaveformData *WaveformPlotCurveHelper::getWaveformData(Handle handle) const
{
    return handle < waveforms_.size() ? &waveforms_[handle] : nullptr;
}

RawWaveformCurves WaveformPlotCurveHelper::getWaveform(Handle handle) const
{
    RawWaveformCurves result;

    if (auto data = getWaveformData(handle))
        result = RawWaveformCurves{ data->rawCurve, data->interpolatedCurve };

    return result;
}

QwtPlotCurve *WaveformPlotCurveHelper::getRawCurve(Handle handle) const
{
    return getWaveform(handle).rawCurve;
}

QwtPlotCurve *WaveformPlotCurveHelper::getInterpolatedCurve(Handle handle) const
{
    return getWaveform(handle).interpolatedCurve;
}

void WaveformPlotCurveHelper::setRawSymbolsVisible(Handle handle, bool visible)
{
    if (auto data = getWaveformData(handle))
    {
        auto curve = data->rawCurve; assert(curve);

        if (!visible)
            curve->setSymbol(nullptr);
        else
            curve->setSymbol(mvme_qwt::make_symbol_from_cache(data->rawSymbolCache).release());
    }
}

void WaveformPlotCurveHelper::setInterpolatedSymbolsVisible(Handle handle, bool visible)
{
    if (auto data = getWaveformData(handle))
    {
        auto curve = data->interpolatedCurve; assert(curve);

        if (!visible)
            curve->setSymbol(nullptr);
        else
            curve->setSymbol(mvme_qwt::make_symbol_from_cache(data->interpolatedSymbolCache).release());
    }
}

mvme_qwt::QwtSymbolCache WaveformPlotCurveHelper::getRawSymbolCache(Handle handle) const
{
    if (auto data = getWaveformData(handle))
        return data->rawSymbolCache;
    return {};
}

mvme_qwt::QwtSymbolCache WaveformPlotCurveHelper::getInterpolatedSymbolCache(Handle handle) const
{
    if (auto data = getWaveformData(handle))
        return data->interpolatedSymbolCache;
    return {};
}

size_t WaveformPlotCurveHelper::size() const
{
    return std::count_if(std::begin(waveforms_), std::end(waveforms_),
        [](const auto &curves) { return curves.rawCurve && curves.interpolatedCurve; });
}

std::unique_ptr<QwtSymbol> make_symbol(QwtSymbol::Style style = QwtSymbol::Diamond, QColor color = Qt::red)
{
    auto result = std::make_unique<QwtSymbol>(style);
    result->setSize(QSize(5, 5));
    result->setColor(color);
    return result;
}

std::unique_ptr<QwtPlotCurve> make_curve(QColor penColor = Qt::black)
{
    auto result = std::make_unique<QwtPlotCurve>();
    result->setPen(penColor);
    result->setRenderHint(QwtPlotItem::RenderAntialiased);
    return result;
}

WaveformCurves make_curves(QColor curvePenColor)
{
    WaveformCurves result;

    result.rawCurve = make_curve(curvePenColor);
    result.rawCurve->setStyle(QwtPlotCurve::NoCurve);
    result.rawCurve->setSymbol(make_symbol(QwtSymbol::Diamond, Qt::red).release());

    result.interpolatedCurve = make_curve(curvePenColor);
    result.interpolatedCurve->setStyle(QwtPlotCurve::Lines);
    result.interpolatedCurve->setSymbol(make_symbol(QwtSymbol::Triangle, Qt::blue).release());

    return result;
}

inline void maybe_shrink_trace_history(waveforms::TraceHistory &traces, size_t maxDepth)
{
    while (traces.size() > maxDepth)
        traces.pop_back();
}

inline waveforms::Trace maybe_recycle_trace(waveforms::TraceHistory &traceBuffer, size_t maxDepth)
{
    assert(maxDepth > 0);

    waveforms::Trace result;

    if (maxDepth > 0 && traceBuffer.size() >= maxDepth)
    {
        result = std::move(traceBuffer.back());
        traceBuffer.pop_back();
    }

    result.clear();
    return result;
}

size_t post_process_waveforms(
    const waveforms::TraceHistories &analysisTraceData,
    waveforms::TraceHistories &rawDisplayTraces,
    waveforms::TraceHistories &interpolatedDisplayTraces,
    double dtSample,
    int interpolationFactor,
    size_t maxDepth,
    PhaseCorrectionMode phaseCorrection)
{
    rawDisplayTraces.resize(analysisTraceData.size());
    interpolatedDisplayTraces.resize(analysisTraceData.size());

    std::for_each(std::begin(rawDisplayTraces), std::end(rawDisplayTraces),
        [maxDepth] (auto &traces) { maybe_shrink_trace_history(traces, maxDepth); });

    std::for_each(std::begin(interpolatedDisplayTraces), std::end(interpolatedDisplayTraces),
        [maxDepth] (auto &traces) { maybe_shrink_trace_history(traces, maxDepth); });

    size_t tracesProcessed = 0;

    for (size_t chan=0; chan<analysisTraceData.size(); ++chan)
    {
        const auto &inputTraces = analysisTraceData[chan];

        auto &rawDestTraces = rawDisplayTraces[chan];
        auto &ipolDestTraces = interpolatedDisplayTraces[chan];

        assert(rawDestTraces.size() <= maxDepth);
        assert(ipolDestTraces.size() <= maxDepth);
        assert(rawDestTraces.size() == ipolDestTraces.size());

        auto pred = [] (const waveforms::Trace &trace) { return !trace.empty(); };

        if (auto it = std::find_if(std::begin(inputTraces), std::end(inputTraces), pred);
            it != std::end(inputTraces))
        {
            auto &inputTrace = *it;

            auto rawDestTrace = maybe_recycle_trace(rawDestTraces, maxDepth);
            auto ipolDestTrace = maybe_recycle_trace(ipolDestTraces, maxDepth);

            rawDestTrace.clear();
            ipolDestTrace.clear();

            double phase = 1.0;
            u32 traceConfig = 0;

            if (phaseCorrection != PhaseCorrection_Off)
            {
                if (auto it = inputTrace.meta.find("config"); it != std::end(inputTrace.meta))
                    traceConfig = std::get<u32>(it->second);

                if (auto it = inputTrace.meta.find("phase"); it != std::end(inputTrace.meta))
                    phase = std::get<double>(it->second);
            }

            if (phaseCorrection == PhaseCorrection_Auto)
            {
                if (!(traceConfig & mdpp_sampling::SamplingSettings::NoResampling))
                {
                    phase = 1.0; // was done in the FPGA, turn it off here
                }
            }

            rawDestTrace.meta = inputTrace.meta;
            ipolDestTrace.meta = inputTrace.meta;
            waveforms::scale_x_values(inputTrace, rawDestTrace, dtSample, phase);
            waveforms::interpolate(rawDestTrace, ipolDestTrace, interpolationFactor);

            rawDestTraces.push_front(std::move(rawDestTrace));
            ipolDestTraces.push_front(std::move(ipolDestTrace));
            ++tracesProcessed;
        }

        assert(rawDestTraces.size() <= maxDepth);
        assert(ipolDestTraces.size() <= maxDepth);
        assert(rawDestTraces.size() == ipolDestTraces.size());
    }

    return tracesProcessed;
}

size_t post_process_waveform_snapshot(
    const waveforms::TraceHistories &analysisTraceData,
    waveforms::TraceHistories &rawDisplayTraces,
    waveforms::TraceHistories &interpolatedDisplayTraces,
    double dtSample,
    int interpolationFactor,
    size_t startingTraceIndex,  // <- first "column"
    size_t maxTraceCount,       // <- amount of "columns" to process and store in the display traces
    PhaseCorrectionMode phaseCorrection)
{
    rawDisplayTraces.resize(analysisTraceData.size());
    interpolatedDisplayTraces.resize(analysisTraceData.size());
    size_t tracesProcessed = 0;

    for (size_t chan=0; chan<analysisTraceData.size(); ++chan)
    {
        const auto &inputTraces = analysisTraceData[chan];

        auto &rawDestTraces = rawDisplayTraces[chan];
        auto &ipolDestTraces = interpolatedDisplayTraces[chan];

        rawDestTraces.resize(std::min(maxTraceCount, inputTraces.size()));
        ipolDestTraces.resize(std::min(maxTraceCount, inputTraces.size()));

        const size_t traceIndexMin = std::min(startingTraceIndex, inputTraces.size());
        const size_t traceIndexMax = std::min(startingTraceIndex + maxTraceCount, inputTraces.size());
        size_t traceOutputIndex = 0;

        for (size_t traceIndex = traceIndexMin; traceIndex < traceIndexMax; ++traceIndex, ++traceOutputIndex)
        {
            auto &inputTrace = inputTraces[traceIndex];
            auto &rawDestTrace = rawDestTraces[traceOutputIndex];
            auto &ipolDestTrace = ipolDestTraces[traceOutputIndex];

            rawDestTrace.clear();
            ipolDestTrace.clear();

            double phase = 1.0;
            u32 traceConfig = 0;

            if (phaseCorrection != PhaseCorrection_Off)
            {
                if (auto it = inputTrace.meta.find("config"); it != std::end(inputTrace.meta))
                    traceConfig = std::get<u32>(it->second);

                if (auto it = inputTrace.meta.find("phase"); it != std::end(inputTrace.meta))
                    phase = std::get<double>(it->second);
            }

            if (phaseCorrection == PhaseCorrection_Auto)
            {
                if (!(traceConfig & mdpp_sampling::SamplingSettings::NoResampling))
                {
                    phase = 1.0; // was done in the FPGA, turn it off here
                }
            }

            rawDestTrace.meta = inputTrace.meta;
            ipolDestTrace.meta = inputTrace.meta;
            waveforms::scale_x_values(inputTrace, rawDestTrace, dtSample, phase);
            waveforms::interpolate(rawDestTrace, ipolDestTrace, interpolationFactor);
            ++tracesProcessed;
        }
    }

    spdlog::trace("post_process_waveform_snapshot(): processed {} traces", tracesProcessed);
    return tracesProcessed;
}

size_t reprocess_waveforms(
    waveforms::TraceHistories &rawDisplayTraces,
    waveforms::TraceHistories &interpolatedDisplayTraces,
    double dtSample,
    int interpolationFactor,
    PhaseCorrectionMode phaseCorrection)
{
    interpolatedDisplayTraces.resize(rawDisplayTraces.size());
    size_t tracesProcessed = 0;

    for (size_t chan=0; chan<rawDisplayTraces.size(); ++chan)
    {
        auto &rawTraces = rawDisplayTraces[chan];
        auto &ipolTraces = interpolatedDisplayTraces[chan];
        ipolTraces.resize(rawTraces.size());

        for (size_t traceIndex=0; traceIndex<rawTraces.size(); ++traceIndex)
        {
            auto &rawTrace = rawTraces[traceIndex];
            auto &ipolTrace = ipolTraces[traceIndex];

            double phase = 1.0;
            u32 traceConfig = 0;

            if (phaseCorrection != PhaseCorrection_Off)
            {
                if (auto it = rawTrace.meta.find("config"); it != std::end(rawTrace.meta))
                    traceConfig = std::get<u32>(it->second);

                if (auto it = rawTrace.meta.find("phase"); it != std::end(rawTrace.meta))
                    phase = std::get<double>(it->second);
            }

            if (phaseCorrection == PhaseCorrection_Auto)
            {
                if (!(traceConfig & mdpp_sampling::SamplingSettings::NoResampling))
                {
                    phase = 1.0; // was done in the FPGA, turn it off here
                }
            }

            waveforms::rescale_x_values(rawTrace, dtSample, phase);
            waveforms::interpolate(rawTrace, ipolTrace, interpolationFactor);
            ++tracesProcessed;
        }
    }

    return tracesProcessed;
}

}
