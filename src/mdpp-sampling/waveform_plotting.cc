#include "waveform_plotting.h"

namespace mesytec::mvme::waveforms
{

struct WaveformCurvesInternal
{
    QwtPlotCurve *rawCurve_ = nullptr;
    QwtPlotCurve *interpolatedCurve_ = nullptr;
};

struct WaveformPlotWidget::Private
{
    std::vector<WaveformCurvesInternal> waveforms;
};

WaveformPlotWidget::WaveformPlotWidget(QWidget *parent)
    : PlotWidget(parent)
    , d(std::make_unique<Private>())
{
}

WaveformPlotWidget::~WaveformPlotWidget()
{
}

WaveformPlotWidget::Handle WaveformPlotWidget::addWaveform(WaveformCurves &&data)
{
}

void WaveformPlotWidget::replot()
{
}

}
