#include "analysis/waveform_sink_widget.h"

namespace analysis
{

struct WaveformSinkWidget::Private
{
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
};

WaveformSinkWidget::WaveformSinkWidget(
    const std::shared_ptr<analysis::WaveformSink> &sink,
    AnalysisServiceProvider *asp,
    QWidget *parent)
    : histo_ui::IPlotWidget(parent)
    , d(std::make_unique<Private>())
{
    d->sink_ = sink;
    d->asp_ = asp;
}

WaveformSinkWidget::~WaveformSinkWidget()
{
}

QwtPlot *WaveformSinkWidget::getPlot()
{
    return nullptr;
}

const QwtPlot *WaveformSinkWidget::getPlot() const
{
    return nullptr;
}


QToolBar *WaveformSinkWidget::getToolBar()
{
    return nullptr;
}

QStatusBar *WaveformSinkWidget::getStatusBar()
{
    return nullptr;
}

void WaveformSinkWidget::replot()
{
}

}
