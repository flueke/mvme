#include "waveform_plotting.h"

namespace mesytec::mvme::waveforms
{

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

RawWaveformCurves WaveformPlotCurveHelper::getWaveform(Handle handle) const
{
    return handle < waveforms_.size() ? waveforms_[handle] : RawWaveformCurves{};
}

QwtPlotCurve *WaveformPlotCurveHelper::getRawCurve(Handle handle)
{
    return getWaveform(handle).rawCurve;
}

QwtPlotCurve *WaveformPlotCurveHelper::getInterpolatedCurve(Handle handle)
{
    return getWaveform(handle).interpolatedCurve;
}

}
