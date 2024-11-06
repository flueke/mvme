#include "waveform_plotting.h"
#include <qwt_symbol.h>

namespace mesytec::mvme::waveforms
{

QRectF update_plot_axes(QwtPlot *plot, QwtPlotZoomer *zoomer, const QRectF &newBoundingRect, const QRectF &prevBoundingRect)
{
    if (!prevBoundingRect.isValid() || zoomer->zoomRectIndex() == 0)
    {
        auto xMin = newBoundingRect.left();
        auto xMax = newBoundingRect.right();
        auto yMax = newBoundingRect.bottom();
        auto yMin = newBoundingRect.top();

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

}
