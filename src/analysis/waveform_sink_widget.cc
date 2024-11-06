#include "analysis/waveform_sink_widget.h"

#include <cmath>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStack>
#include <QTimer>
#include <QVariantAnimation>
#include <qwt_plot_spectrogram.h>
#include <qwt_symbol.h>
#include <set>

#include <mesytec-mvlc/util/algo.h>
#include <mesytec-mvlc/util/stopwatch.h>

#include "analysis_service_provider.h"
#include "mdpp-sampling/mdpp_sampling_p.h"
#include "mdpp-sampling/mdpp_sampling.h"
#include "util/qt_logview.h"

using namespace mesytec::mvme;
using namespace mesytec::mvme::mdpp_sampling;

namespace analysis
{

namespace
{
    QSpinBox *add_channel_select(QToolBar *toolbar)
    {
        auto result = new QSpinBox;
        result->setMinimum(0);
        result->setMaximum(0);
        auto boxStruct = make_vbox_container(QSL("Channel"), result, 0, -2);
        toolbar->addWidget(boxStruct.container.release());
        return result;
    }

    QSpinBox *add_trace_select(QToolBar *toolbar)
    {
        auto result = new QSpinBox;
        result->setMinimum(0);
        result->setMaximum(1);
        result->setValue(0);
        result->setSpecialValueText("latest");
        auto boxStruct = make_vbox_container(QSL("Trace#"), result, 0, -2);
        toolbar->addWidget(boxStruct.container.release());
        return result;
    }

    QDoubleSpinBox *add_dt_sample_setter(QToolBar *toolbar)
    {
        auto result = new QDoubleSpinBox;
        result->setMinimum(1.0);
        result->setMaximum(1e9);
        result->setSingleStep(0.1);
        result->setSuffix(" ns");
        result->setValue(mdpp_sampling::MdppDefaultSamplePeriod);

        auto pb_useDefaultSampleInterval = new QPushButton(QIcon(":/reset_to_default.png"), {});

        QObject::connect(pb_useDefaultSampleInterval, &QPushButton::clicked, result, [result] {
            result->setValue(mdpp_sampling::MdppDefaultSamplePeriod);
        });

        auto [w0, l0] = make_widget_with_layout<QWidget, QHBoxLayout>();
        l0->addWidget(result);
        l0->addWidget(pb_useDefaultSampleInterval);

        auto boxStruct = make_vbox_container(QSL("Sample Interval"), w0, 0, -2);
        toolbar->addWidget(boxStruct.container.release());

        return result;
    }

    QSpinBox *add_interpolation_factor_setter(QToolBar *toolbar)
    {
        auto result = new QSpinBox;
        result->setSpecialValueText("off");
        result->setMinimum(0);
        result->setMaximum(100);
        result->setValue(5);
        auto boxStruct = make_vbox_container(QSL("Interpolation Factor"), result, 0, -2);
        toolbar->addWidget(boxStruct.container.release());
        return result;
    }
}

static const int ReplotInterval_ms = 33;

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

    QVariantAnimation *curveFader_ = nullptr;
    bool curveFaderFinished_ = false; // The finished() signal is only emitted if the animation stopped naturally.

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

    QSpinBox *spin_curveAlpha_ = nullptr;

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

    auto curveFader = new QVariantAnimation(this);
    curveFader->setStartValue(1.0);
    curveFader->setEndValue(0.0);
    curveFader->setDuration(1000);
    d->curveFader_ = curveFader;
    connect(d->curveFader_, &QAbstractAnimation::finished, this, [this] { d->curveFaderFinished_ = true; qDebug() << "finished"; });

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

    /*
    if ((!trace || trace->empty()) && (d->curveFader_->state() != QAbstractAnimation::Running && !d->curveFaderFinished_))
    {
        d->curveFaderFinished_ = false; // set to true by the qt event loop
        d->curveFader_->start();
    }
    else */ if (trace /*&& !trace->empty() */)
    {
        //d->curveFader_->stop();
        //d->curveFader_->setCurrentTime(0);

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

    #if 0
    if (auto curves = d->curveHelper_.getWaveform(d->waveformHandle_);
        curves.rawCurve && curves.interpolatedCurve)
    {
        for (auto curve: { curves.rawCurve, curves.interpolatedCurve })
        {
            auto curveAlpha = d->curveFader_->currentValue().value<double>();

            // FIXME: ugly
            {
                auto pen = curve->pen();
                auto penColor = pen.color();
                penColor.setAlphaF(curveAlpha);
                pen.setColor(penColor);
                curve->setPen(pen);
            }

            if (auto symbol = curve->symbol())
            {
                // FIXME: ugly
                auto cache = mvme_qwt::make_cache_from_symbol(symbol);

                auto brushColor = cache.brush.color();
                brushColor.setAlphaF(curveAlpha);
                cache.brush.setColor(brushColor);

                auto penColor = cache.pen.color();
                penColor.setAlphaF(curveAlpha);
                cache.pen.setColor(penColor);

                auto newSymbol = mvme_qwt::make_symbol_from_cache(cache);
                curve->setSymbol(newSymbol.release());
            }
        }
    }
    #endif

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

struct TraceCollectionProcessingState
{
    enum Result
    {
        TraceUpdated,
        PreviousTraceKept,
    };

    // input traces from the analysis with x coordinates set to the samples index
    std::vector<const waveforms::Trace *> inputTraces;
    // output trace with x coordinates scaled by dtSample. reused for each trace during processing.
    waveforms::Trace traceWorkBuffer_;
    // interpolated output traces to be plotted
    std::vector<waveforms::Trace> outputTraces;
    std::vector<Result> results;
};

void post_process_one_trace(TraceCollectionProcessingState &state,
    size_t traceIndex, double dtSample, int interpolationFactor)
{
        const auto &inputTrace = *state.inputTraces[traceIndex];
        auto &outputTrace = state.outputTraces[traceIndex];

        #if 0
        if (inputTrace.empty() && !outputTrace.empty())
        {
            state.results[traceIndex] = TraceCollectionProcessingState::PreviousTraceKept;
            return;
        }
        #endif

        waveforms::scale_x_values(inputTrace, state.traceWorkBuffer_, dtSample);
        waveforms::interpolate(state.traceWorkBuffer_, outputTrace, interpolationFactor);
        state.results[traceIndex] = TraceCollectionProcessingState::TraceUpdated;
}

static void post_process_traces(TraceCollectionProcessingState &state,
    double dtSample, int interpolationFactor)
{
    state.outputTraces.resize(state.inputTraces.size());
    state.results.resize(state.inputTraces.size());

    for (size_t i = 0; i < state.inputTraces.size(); ++i)
    {
        post_process_one_trace(state, i, dtSample, interpolationFactor);
    }
}

struct WaveformSinkVerticalWidget::Private
{
    WaveformSinkVerticalWidget *q = nullptr;
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
    waveforms::WaveformCollectionVerticalRasterData *plotData_ = nullptr;
    QwtPlotSpectrogram *plotItem_ = nullptr;
    TraceCollectionProcessingState processingState_;

    QwtPlotZoomer *zoomer_ = nullptr;
    QTimer replotTimer_;
    mesytec::mvlc::util::Stopwatch frameTimer_;

    QVariantAnimation *curveFader_ = nullptr;

    QSpinBox *traceSelect_ = nullptr;
    waveforms::TraceHistories traceHistories_;
    QPushButton *pb_printInfo_ = nullptr;
    QDoubleSpinBox *spin_dtSample_ = nullptr;
    QSpinBox *spin_interpolationFactor_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    WidgetGeometrySaver *geoSaver_ = nullptr;
    QRectF maxBoundingRect_;
    QPushButton *pb_resetBoundingRect = nullptr;

    void updateUi();
    void makeInfoText(std::ostringstream &oss);
    void makeStatusText(std::ostringstream &oss, const std::chrono::duration<double, std::milli> &dtFrame);
    void printInfo();
    void exportPlotToPdf();
    void exportPlotToClipboard();
};

WaveformSinkVerticalWidget::WaveformSinkVerticalWidget(
    const std::shared_ptr<analysis::WaveformSink> &sink,
    AnalysisServiceProvider *asp,
    QWidget *parent)
    : histo_ui::PlotWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->sink_ = sink;
    d->asp_ = asp;
    d->replotTimer_.setInterval(ReplotInterval_ms);

    auto curveFader = new QVariantAnimation(this);
    curveFader->setStartValue(255);
    curveFader->setEndValue(0);
    curveFader->setDuration(1000);
    curveFader->setLoopCount(-1); // loop forever until stopped
    curveFader->start();
    d->curveFader_ = curveFader;

    d->plotData_ = new waveforms::WaveformCollectionVerticalRasterData();
    d->plotItem_ = new QwtPlotSpectrogram;
    d->plotItem_->setData(d->plotData_);
    d->plotItem_->setRenderThreadCount(0); // use system specific ideal thread count
    d->plotItem_->setColorMap(histo_ui::make_histo2d_color_map(histo_ui::AxisScaleType::Linear).release());
    d->plotItem_->attach(getPlot());

    auto rightAxis = getPlot()->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Sample Value");
    rightAxis->setColorBarEnabled(true);
    getPlot()->enableAxis(QwtPlot::yRight);


    getPlot()->axisWidget(QwtPlot::xBottom)->setTitle("Time [ns]");
    getPlot()->axisWidget(QwtPlot::yLeft)->setTitle("Channel");
    histo_ui::setup_axis_scale_changer(this, QwtPlot::yRight, "Z-Scale");
    d->zoomer_ = histo_ui::install_scrollzoomer(this);
    histo_ui::install_tracker_picker(this);

    DO_AND_ASSERT(connect(histo_ui::get_zoomer(this), SIGNAL(zoomed(const QRectF &)), this, SLOT(replot())));

    // enable both the zoomer and mouse cursor tracker by default

    if (auto zoomAction = findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);

    if (auto trackerPickerAction = findChild<QAction *>("trackerPickerAction"))
        trackerPickerAction->setChecked(true);

    setWindowTitle("WaveformSinkVerticalWidget");

    auto tb = getToolBar();

    tb->addSeparator();
    d->traceSelect_ = add_trace_select(tb);

    tb->addSeparator();
    d->spin_dtSample_ = add_dt_sample_setter(tb);

    tb->addSeparator();
    d->spin_interpolationFactor_ = add_interpolation_factor_setter(tb);

    tb->addSeparator();

    {
        d->pb_resetBoundingRect = new QPushButton("Reset Axis Scales");
        auto [widget, layout] = make_widget_with_layout<QWidget, QVBoxLayout>();
        layout->addWidget(d->pb_resetBoundingRect);
        tb->addWidget(widget);

        connect(d->pb_resetBoundingRect, &QPushButton::clicked, this, [this] {
            d->maxBoundingRect_ = {};
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

    connect(d->traceSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &WaveformSinkVerticalWidget::replot);

    connect(&d->replotTimer_, &QTimer::timeout, this, &WaveformSinkVerticalWidget::replot);
    d->replotTimer_.start();

    d->geoSaver_ = new WidgetGeometrySaver(this);
    d->geoSaver_->addAndRestore(this, "WindowGeometries/WaveformSinkVerticalWidget");
}

WaveformSinkVerticalWidget::~WaveformSinkVerticalWidget()
{
}

void WaveformSinkVerticalWidget::Private::updateUi()
{
    if (traceHistories_.empty())
    {
        traceSelect_->setMaximum(0);
        return;
    }

    // assumes all trace histories have the same size
    auto &tracebuffer = traceHistories_[0];
    traceSelect_->setMaximum(std::max(0, static_cast<int>(tracebuffer.size())-1));
}

void WaveformSinkVerticalWidget::replot()
{
    spdlog::trace("begin WaveformSinkVerticalWidget::replot()");

    // Thread-safe copy of the trace history shared with the analysis runtime.
    // Might be very expensive depending on the size of the trace history.
    d->traceHistories_ = d->sink_->getTraceHistories();

    d->updateUi(); // update, selection boxes, buttons, etc.

    // pick a trace from the same column of each row in the trace history == one event snapshot
    auto selectedTraceIndex = d->traceSelect_->value();
    d->processingState_.inputTraces = waveforms::get_trace_column(d->traceHistories_, selectedTraceIndex);

    // post process the raw traces
    const auto dtSample = d->spin_dtSample_->value();
    const auto interpolationFactor = 1 + d->spin_interpolationFactor_->value();

    post_process_traces(d->processingState_, dtSample, interpolationFactor);
    // Warning: if interpolationFactor is > 1.0 the x intervals in the output traces will not be equidistant!
    // TODO: change the interpolation to produce equidistant x values

    auto traces = &d->processingState_.outputTraces;

    double xStep = dtSample / interpolationFactor;
    #if 0
    // FIXME: this does not work. Is the interpolation bugged? I expect steps <
    // dtSample if interpolation is used, but it never happens.
    if (auto hasSamples = std::find_if(std::begin(*traces), std::end(*traces), [] (const auto &trace) { return trace.size() > 1; });
        hasSamples != std::end(*traces))
    {
        xStep = hasSamples->xs[1] - hasSamples->xs[0];
    }
    #endif

    // TODO: improve setTraceCollection() to avoid having to create the vector of pointers here.
    std::vector<const waveforms::Trace *> tracePointers(traces->size());
    std::transform(std::begin(*traces), std::end(*traces), std::begin(tracePointers),
        [] (const waveforms::Trace &trace) { return &trace; });

    d->plotData_->setTraceCollection(tracePointers, xStep);

    // determine axis scale ranges, update the zoomer, set plot axis scales
    if (!traces->empty())
    {
        const double xMax = 1.0 + std::accumulate(std::begin(*traces), std::end(*traces), 0.0,
            [] (double acc, const auto &trace)
            {
                return !trace.empty() ? std::max(acc, trace.xs.back()) : acc;
            });


        // number of traces == row count
        const double yMax = traces->size();

        // minmax y values over all traces
        double zMin = std::numeric_limits<double>::max();
        double zMax = std::numeric_limits<double>::lowest();

        auto update_z_minmax = [&zMin, &zMax] (const auto &trace)
        {
            auto [min_, max_] = waveforms::find_minmax_y(trace);
            zMin = std::min(zMin, min_);
            zMax = std::max(zMax, max_);
        };

        std::for_each(std::begin(*traces), std::end(*traces), update_z_minmax);

        // To avoid 'QwtLinearScaleEngine::divideScale: overflow' warnings when
        // all traces are empty and this zMin/Max remain unchanged.
        if (zMax < zMin || static_cast<long double>(zMax) - static_cast<long double>(zMin) >= std::numeric_limits<double>::max())
        {
            zMin = 0.0;
            zMax = 1.0;
        }

        d->plotData_->setInterval(Qt::XAxis, QwtInterval(0.0, xMax));
        d->plotData_->setInterval(Qt::YAxis, QwtInterval(0.0, yMax));
        d->plotData_->setInterval(Qt::ZAxis, QwtInterval(zMin, zMax));

        if (d->zoomer_->zoomRectIndex() == 0)
        {
            //qDebug() << "WaveformSinkVerticalWidget::replot(): setting xBottom xMax =" << xMax;
            //qDebug() << "rasterdata interval X:" << d->plotData_->interval(Qt::XAxis);
            getPlot()->setAxisScale(QwtPlot::xBottom, 0.0, xMax);
            getPlot()->setAxisScale(QwtPlot::yLeft, 0.0, yMax);
            d->zoomer_->setZoomBase();
        }

        if (histo_ui::is_logarithmic_axis_scale(getPlot(), QwtPlot::yRight))
        {
            zMin = 1.0;
            zMax = std::max(2.0, zMax);
        }

        getPlot()->setAxisScale(QwtPlot::yRight, zMin, zMax);
        getPlot()->updateAxes();

        // FIXME only do this when the scale type changes. maybe update PlotAxisScaleChanger to help with this
        auto colorMap = histo_ui::make_histo2d_color_map(
            histo_ui::get_axis_scale_type(getPlot(), QwtPlot::yRight));
        d->plotItem_->setColorMap(colorMap.release());

        // FIXME only do this when the scale type changes. maybe update PlotAxisScaleChanger to help with this
        colorMap = histo_ui::make_histo2d_color_map(
            histo_ui::get_axis_scale_type(getPlot(), QwtPlot::yRight));
        getPlot()->axisWidget(QwtPlot::yRight)
            ->setColorMap(QwtInterval{ zMin, zMax }, colorMap.release());
    }

    histo_ui::PlotWidget::replot();

    std::ostringstream oss;
    d->makeStatusText(oss, d->frameTimer_.interval());

    getStatusBar()->clearMessage();
    getStatusBar()->showMessage(oss.str().c_str());

    spdlog::trace("end WaveformSinkVerticalWidget::replot()");
}

void WaveformSinkVerticalWidget::Private::makeInfoText(std::ostringstream &out)
{
    double totalMemory = mesytec::mvme::waveforms::get_used_memory(traceHistories_);
    size_t numChannels = traceHistories_.size();
    size_t historyDepth = !traceHistories_.empty() ? traceHistories_[0].size() : 0u;

    out << "Trace History Info:\n";
    out << fmt::format("  Total memory used: {:} B / {:.2f} MiB\n", totalMemory, static_cast<double>(totalMemory) / Megabytes(1));
    out << fmt::format("  Number of channels: {}\n", numChannels);
    out << fmt::format("  History depth: {}\n", historyDepth);
    out << "\n";

    const size_t numTraces = std::min(processingState_.inputTraces.size(), processingState_.outputTraces.size());

    for (size_t traceIndex = 0; traceIndex < numTraces; ++traceIndex)
    {
        auto rawTrace = processingState_.inputTraces[traceIndex];
        auto interpolatedTrace = processingState_.outputTraces[traceIndex];

        if (rawTrace)
        {
            out << fmt::format("input trace#{}: ", traceIndex);
            mesytec::mvme::waveforms::print_trace_compact(out, *rawTrace);
        }

        out << fmt::format("interpolated trace#{}: ", traceIndex);
        mesytec::mvme::waveforms::print_trace_compact(out, interpolatedTrace);
    }

    // raw input traces: d->processingState_.inputTraces
    // intermediate scaled trace is not kept. it's only temp in the traceWorkBuffer
    // interpolated traces: d->processingState_.outputTraces
}

void WaveformSinkVerticalWidget::Private::makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame)
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
    double uiTotalMemory = get_used_memory(traceHistories_);
    double sinkTotalMemory = sink_->getStorageSize();
    size_t numChannels = traceHistories_.size();
    size_t historyDepth = !traceHistories_.empty() ? traceHistories_[0].size() : 0u;

    out << fmt::format("Mem: ui: {:.2f} MiB, analysis: {:.2f} MiB",
        uiTotalMemory / Megabytes(1), sinkTotalMemory / Megabytes(1));
    out << fmt::format(", #Channels: {}", numChannels);
    out << fmt::format(", History depth: {}", historyDepth);
    out << fmt::format(", Frame time: {} ms", static_cast<int>(dtFrame.count()));
}

void WaveformSinkVerticalWidget::Private::printInfo()
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

void WaveformSinkVerticalWidget::Private::exportPlotToPdf()
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

void WaveformSinkVerticalWidget::Private::exportPlotToClipboard()
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
