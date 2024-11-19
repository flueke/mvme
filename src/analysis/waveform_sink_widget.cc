#include "analysis/waveform_sink_widget.h"
#include "analysis/waveform_sink_widget_common.h"

#include <cmath>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStack>
#include <QTimer>
#include <qwt_plot_spectrogram.h>
#include <qwt_symbol.h>
#include <set>

#include <mesytec-mvlc/util/stopwatch.h>

#include "analysis_service_provider.h"
#include "mdpp-sampling/mdpp_sampling_p.h"
#include "mdpp-sampling/mdpp_sampling.h"
#include "util/qt_logview.h"

using namespace mesytec::mvme;
using namespace mesytec::mvme::mdpp_sampling;

namespace analysis
{

static const int ReplotInterval_ms = 100;

struct WaveformSinkWidget::Private
{
    WaveformSinkWidget *q = nullptr;
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
    waveforms::WaveformPlotCurveHelper curveHelper_;
    waveforms::WaveformPlotCurveHelper::Handle waveformHandle_;
    // these have to be pointers as qwt takes ownership
    waveforms::WaveformPlotData *rawPlotData_ = nullptr;
    waveforms::WaveformPlotData *interpolatedPlotData_ = nullptr;

    waveforms::Trace currentRawTrace_;
    waveforms::Trace currentInterpolatedTrace_;

    QwtPlotZoomer *zoomer_ = nullptr;
    QTimer replotTimer_;
    mesytec::mvlc::util::Stopwatch frameTimer_;

    QSpinBox *channelSelect_ = nullptr;
    QSpinBox *traceSelect_ = nullptr;
    waveforms::TraceHistories traceHistories_;
    QPushButton *pb_printInfo_ = nullptr;
    QDoubleSpinBox *spin_dtSample_ = nullptr;
    QSpinBox *spin_interpolationFactor_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QCheckBox *cb_sampleSymbols_ = nullptr;
    QCheckBox *cb_interpolatedSymbols_ = nullptr;
    WidgetGeometrySaver *geoSaver_ = nullptr;
    QRectF maxBoundingRect_;
    QPushButton *pb_resetBoundingRect = nullptr;

    void updateUi();
    void makeInfoText(std::ostringstream &oss);
    void makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame);
    void printInfo();
    void exportPlotToPdf();
    void exportPlotToClipboard();
};

WaveformSinkWidget::WaveformSinkWidget(
    const std::shared_ptr<analysis::WaveformSink> &sink,
    AnalysisServiceProvider *asp,
    QWidget *parent)
    : histo_ui::PlotWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->sink_ = sink;
    d->asp_ = asp;
    d->curveHelper_ = waveforms::WaveformPlotCurveHelper(getPlot());
    d->replotTimer_.setInterval(ReplotInterval_ms);

    auto curves = waveforms::make_curves();

    d->rawPlotData_ = new waveforms::WaveformPlotData;
    curves.rawCurve->setData(d->rawPlotData_);
    d->interpolatedPlotData_ = new waveforms::WaveformPlotData;
    curves.interpolatedCurve->setData(d->interpolatedPlotData_);

    d->waveformHandle_ = d->curveHelper_.addWaveform(std::move(curves));

    getPlot()->axisWidget(QwtPlot::xBottom)->setTitle("Time [ns]");
    histo_ui::setup_axis_scale_changer(this, QwtPlot::yLeft, "Y-Scale");
    d->zoomer_ = histo_ui::install_scrollzoomer(this);
    histo_ui::install_tracker_picker(this);

    DO_AND_ASSERT(connect(histo_ui::get_zoomer(this), SIGNAL(zoomed(const QRectF &)), this, SLOT(replot())));

    // enable both the zoomer and mouse cursor tracker by default

    if (auto zoomAction = findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);

    if (auto trackerPickerAction = findChild<QAction *>("trackerPickerAction"))
        trackerPickerAction->setChecked(true);

    setWindowTitle("WaveformSinkWidget");

    auto tb = getToolBar();

    tb->addSeparator();
    d->channelSelect_ = add_channel_select(tb);

    tb->addSeparator();
    d->traceSelect_ = add_trace_select(tb);

    tb->addSeparator();
    d->spin_dtSample_ = add_dt_sample_setter(tb);

    tb->addSeparator();
    d->spin_interpolationFactor_ = add_interpolation_factor_setter(tb);

    tb->addSeparator();

    {
        d->cb_sampleSymbols_ = new QCheckBox("Sample Symbols");
        d->cb_sampleSymbols_->setChecked(true);
        d->cb_interpolatedSymbols_ = new QCheckBox("Interpolated Symbols");
        d->pb_resetBoundingRect = new QPushButton("Reset Axis Scales");
        auto [widget, layout] = make_widget_with_layout<QWidget, QVBoxLayout>();
        layout->addWidget(d->cb_sampleSymbols_);
        layout->addWidget(d->cb_interpolatedSymbols_);
        layout->addWidget(d->pb_resetBoundingRect);
        tb->addWidget(widget);

        connect(d->pb_resetBoundingRect, &QPushButton::clicked, this, [this] {
            d->maxBoundingRect_ = {};
            d->zoomer_->setZoomStack(QStack<QRectF>(), -1);
            d->zoomer_->zoom(0);
        });
    }

    tb->addSeparator();

    d->pb_printInfo_ = new QPushButton("Show Info");
    tb->addWidget(d->pb_printInfo_);

    tb->addSeparator();

    // export plot to file / clipboard
    {
        auto menu = new QMenu(this);
        menu->addAction(QSL("to file"), this, [this] { d->exportPlotToPdf(); });
        menu->addAction(QSL("to clipboard"), this, [this] { d->exportPlotToClipboard(); });

        auto button = make_toolbutton(QSL(":/document-pdf.png"), QSL("Export"));
        button->setStatusTip(QSL("Export plot to file or clipboard"));
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);

        tb->addWidget(button);
    }

    connect(d->pb_printInfo_, &QPushButton::clicked, this, [this] { d->printInfo(); });

    connect(d->channelSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &WaveformSinkWidget::replot);
    connect(d->traceSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &WaveformSinkWidget::replot);

    connect(&d->replotTimer_, &QTimer::timeout, this, &WaveformSinkWidget::replot);
    d->replotTimer_.start();

    d->geoSaver_ = new WidgetGeometrySaver(this);
    d->geoSaver_->addAndRestore(this, "WindowGeometries/WaveformSinkWidget");
}

WaveformSinkWidget::~WaveformSinkWidget()
{
}

void WaveformSinkWidget::Private::updateUi()
{
    // selector 1: Update the channel number spinbox
    const auto maxChannel = static_cast<signed>(traceHistories_.size()) - 1;
    channelSelect_->setMaximum(std::max(maxChannel, channelSelect_->maximum()));
    auto selectedChannel = channelSelect_->value();

    // selector 2: trace number in the trace history. index 0 is the latest trace.
    if (0 <= selectedChannel && selectedChannel <= maxChannel)
    {
        auto &tracebuffer = traceHistories_[selectedChannel];
        traceSelect_->setMaximum(std::max(0, static_cast<int>(tracebuffer.size())-1));
    }
    else
    {
        traceSelect_->setMaximum(0);
    }

    curveHelper_.setRawSymbolsVisible(waveformHandle_, cb_sampleSymbols_->isChecked());
    curveHelper_.setInterpolatedSymbolsVisible(waveformHandle_, cb_interpolatedSymbols_->isChecked());
}

void WaveformSinkWidget::replot()
{
    spdlog::trace("begin WaveformSinkWidget::replot()");

    // Thread-safe copy of the trace history shared with the analysis runtime.
    // Might be very expensive depending on the size of the trace history.
    d->traceHistories_ = d->sink_->getTraceHistories();

    d->updateUi(); // update, selection boxes, buttons, etc.

    waveforms::Trace *trace = nullptr;

    const auto channelCount = d->traceHistories_.size();
    const auto selectedChannel = d->channelSelect_->value();

    if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) < channelCount)
    {
        auto &tracebuffer = d->traceHistories_[selectedChannel];

        // selector 3: trace number in the trace history. 0 is the latest trace.
        auto selectedTraceIndex = d->traceSelect_->value();

        spdlog::trace("selectedTraceIndex={}, traceSelect->min()={}, traceSelect->max()={}",
            selectedTraceIndex, d->traceSelect_->minimum(), d->traceSelect_->maximum());

        if (0 <= selectedTraceIndex && static_cast<size_t>(selectedTraceIndex) < tracebuffer.size())
            trace = &tracebuffer[selectedTraceIndex];
    }

    if (trace)
    {
        auto dtSample = d->spin_dtSample_->value();
        auto interpolationFactor = 1+ d->spin_interpolationFactor_->value();

        // We now have raw trace data from the analysis. First scale the x
        // coordinates, which currently are just array indexes, by dtSample.
        waveforms::scale_x_values(*trace, d->currentRawTrace_, dtSample);
        // and interpolate
        waveforms::interpolate(d->currentRawTrace_, d->currentInterpolatedTrace_, interpolationFactor);
    }

    d->rawPlotData_->setTrace(&d->currentRawTrace_);
    d->interpolatedPlotData_->setTrace(&d->currentInterpolatedTrace_);

    auto boundingRect = d->interpolatedPlotData_->boundingRect();
    auto newBoundingRect = d->maxBoundingRect_.united(boundingRect);
    waveforms::update_plot_axes(getPlot(), d->zoomer_, newBoundingRect, d->maxBoundingRect_);
    d->maxBoundingRect_ = newBoundingRect;

    histo_ui::PlotWidget::replot();

    std::ostringstream oss;
    d->makeStatusText(oss, d->frameTimer_.interval());

    getStatusBar()->clearMessage();
    getStatusBar()->showMessage(oss.str().c_str());

    spdlog::trace("end WaveformSinkWidget::replot()");
}

void WaveformSinkWidget::Private::makeInfoText(std::ostringstream &out)
{
    double totalMemory = mesytec::mvme::waveforms::get_used_memory(traceHistories_);
    size_t numChannels = traceHistories_.size();
    size_t historyDepth = !traceHistories_.empty() ? traceHistories_[0].size() : 0u;
    size_t currentTraceSize = currentRawTrace_.size();
    size_t interpolatedSize = currentInterpolatedTrace_.size();
    auto selectedChannel = channelSelect_->value();

    out << "Trace History Info:\n";
    out << fmt::format("  Total memory used: {:} B / {:.2f} MiB\n", totalMemory, static_cast<double>(totalMemory) / Megabytes(1));
    out << fmt::format("  Number of channels: {}\n", numChannels);
    out << fmt::format("  History depth: {}\n", historyDepth);
    out << fmt::format("  Selected channel: {}\n", selectedChannel);
    out << "\n";

    {
        out << fmt::format("begin current trace ({} samples):\n", currentTraceSize);
        mesytec::mvme::waveforms::print_trace(out, currentRawTrace_);
        out << fmt::format("end current trace\n");

        out << fmt::format("begin interpolated trace ({} samples):\n", interpolatedSize);
        mesytec::mvme::waveforms::print_trace(out, currentInterpolatedTrace_);
        out << fmt::format("end interpolated trace\n");
    }
}

void WaveformSinkWidget::Private::makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame)
{
    // About memory: initially a trace history consists of an empty vector of
    // queues of empty vectors.
    //
    // The analysis sink will quickly fill each channels history buffer to the
    // max size, then reuse existing buffers.
    //
    // The UI calls getTraceHistories() on the sink which copies the entire
    // history. When the internal vectors and queues are copied, the copies may
    // have less memory reserved than the originals. This happens when traces
    // are empty due to the sink input data consisting only of invalid
    // parameters: the sinks reused trace buffers will have the full capacity,
    // the copies will be empty without any allocation done.
    //
    // Two effects follow:
    //
    //   1) The memory used by the copy of the trace history can be much lower
    //      than the memory used by the analysis sink.
    //
    //   2) The memory used by the copy of the trace history can fluctuate
    //      during a run.
    //
    // Both depend on the actual number of samples currently in the trace history.

    using mesytec::mvme::waveforms::get_used_memory;
    double uiTotalMemory = get_used_memory(traceHistories_) + get_used_memory(currentRawTrace_) + get_used_memory(currentInterpolatedTrace_);
    double sinkTotalMemory = sink_->getStorageSize();
    size_t numChannels = traceHistories_.size();
    size_t historyDepth = !traceHistories_.empty() ? traceHistories_[0].size() : 0u;
    //size_t currentTraceSize = currentTrace_ ? currentTrace_->size() : 0u;
    //size_t interpolatedSize = interpolatedTrace_.size();
    auto selectedChannel = channelSelect_->value();

    out << fmt::format("Mem: ui: {:.2f} MiB, analysis: {:.2f} MiB",
        uiTotalMemory / Megabytes(1), sinkTotalMemory / Megabytes(1));
    out << fmt::format(", #Channels: {}", numChannels);
    out << fmt::format(", History depth: {}", historyDepth);
    out << fmt::format(", Selected channel: {}", selectedChannel);
    out << fmt::format(", Frame time: {} ms", static_cast<int>(dtFrame.count()));
}

void WaveformSinkWidget::Private::printInfo()
{
    if (!logView_)
    {
        logView_ = make_logview().release();
        logView_->setWindowTitle("MDPP Sampling Mode: Trace Info");
        logView_->setAttribute(Qt::WA_DeleteOnClose);
        logView_->resize(1000, 600);
        connect(logView_, &QWidget::destroyed, q, [this] { logView_ = nullptr; });
        add_widget_close_action(logView_);
        geoSaver_->addAndRestore(logView_, "WindowGeometries/MdppSamplingUiLogView");
    }

    std::ostringstream oss;
    makeInfoText(oss);

    assert(logView_);
    logView_->setPlainText(oss.str().c_str());
    logView_->show();
    logView_->raise();
}

void WaveformSinkWidget::Private::exportPlotToPdf()
{
    // TODO: generate a filename
    #if 0
    QString fileName = getCurrentHisto()->objectName();
    fileName.replace("/", "_");
    fileName.replace("\\", "_");
    fileName += QSL(".pdf");
    #else
    QString filename = "waveform_export.pdf";
    #endif

    if (asp_)
        filename = QDir(asp_->getWorkspacePath(QSL("PlotsDirectory"))).filePath(filename);

    #if 0
    m_plot->setTitle(getCurrentHisto()->getTitle());

    QString footerString = getCurrentHisto()->getFooter();
    QwtText footerText(footerString);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_plot->setFooter(footerText);
    m_waterMarkLabel->show();
    #endif

    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.exportTo(q->getPlot(), filename);

    #if 0
    m_plot->setTitle(QString());
    m_plot->setFooter(QString());
    m_waterMarkLabel->hide();
    #endif
}

void WaveformSinkWidget::Private::exportPlotToClipboard()
{
    QSize size(1024, 768);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QwtPlotRenderer renderer;
#ifndef Q_OS_WIN
    // Enabling this leads to black pixels when pasting the image into windows
    // paint.
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground
                             | QwtPlotRenderer::DiscardCanvasBackground);
#endif
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.renderTo(q->getPlot(), image);

    auto clipboard = QApplication::clipboard();
    clipboard->clear();
    clipboard->setImage(image);
}


}
