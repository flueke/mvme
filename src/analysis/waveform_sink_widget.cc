#include "analysis/waveform_sink_widget.h"

#include <cmath>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
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
    // these have to be pointers as qwt takees ownership
    waveforms::WaveformPlotData *rawPlotData_ = nullptr;
    waveforms::WaveformPlotData *interpolatedPlotData_ = nullptr;
    waveforms::Trace *currentTrace_ = nullptr;
    waveforms::Trace interpolatedTrace_;
    QwtPlotZoomer *zoomer_ = nullptr;
    QTimer replotTimer_;
    mesytec::mvlc::util::Stopwatch frameTimer_;

    QVariantAnimation *curveFader_ = nullptr;

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
    curveFader->setLoopCount(-1); // loop forever until stopped
    curveFader->setDuration(1000);
    curveFader->start();
    d->curveFader_ = curveFader;

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
        });
    }

    tb->addSeparator();

    d->pb_printInfo_ = new QPushButton("Print Info");
    tb->addWidget(d->pb_printInfo_);

    d->spin_curveAlpha_ = new QSpinBox;
    d->spin_curveAlpha_->setMinimum(0);
    d->spin_curveAlpha_->setMaximum(255);
    d->spin_curveAlpha_->setValue(255);
    tb->addWidget(d->spin_curveAlpha_);

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
        std::transform(std::begin(trace->xs), std::end(trace->xs), std::begin(trace->xs),
            [dtSample](double x) { return x * dtSample; });

        // and interpolate
        auto emitter = [this] (double x, double y)
        {
            d->interpolatedTrace_.push_back(x, y);
        };

        d->interpolatedTrace_.clear();
        waveforms::interpolate(trace->xs, trace->ys, interpolationFactor, emitter);
    }

    d->rawPlotData_->setTrace(trace);
    d->interpolatedPlotData_->setTrace(&d->interpolatedTrace_);

    auto boundingRect = d->interpolatedPlotData_->boundingRect();
    auto newBoundingRect = d->maxBoundingRect_.united(boundingRect);
    waveforms::update_plot_axes(getPlot(), d->zoomer_, newBoundingRect, d->maxBoundingRect_);
    d->maxBoundingRect_ = newBoundingRect;
    d->currentTrace_ = trace;

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
    size_t currentTraceSize = currentTrace_ ? currentTrace_->size() : 0u;
    size_t interpolatedSize = interpolatedTrace_.size();
    auto selectedChannel = channelSelect_->value();

    out << "Trace History Info:\n";
    out << fmt::format("  Total memory used: {:} B / {:.2f} MiB\n", totalMemory, static_cast<double>(totalMemory) / Megabytes(1));
    out << fmt::format("  Number of channels: {}\n", numChannels);
    out << fmt::format("  History depth: {}\n", historyDepth);
    out << fmt::format("  Selected channel: {}\n", selectedChannel);
    out << "\n";

    if (currentTrace_)
    {
        out << fmt::format("begin current trace ({} samples):\n", currentTraceSize);
        mesytec::mvme::waveforms::print_trace(out, *currentTrace_);
        out << fmt::format("end current trace\n");

        out << fmt::format("begin interpolated trace ({} samples):\n", interpolatedSize);
        mesytec::mvme::waveforms::print_trace(out, interpolatedTrace_);
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
    double uiTotalMemory = get_used_memory(traceHistories_) + get_used_memory(interpolatedTrace_);
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

struct WaveformSinkVerticalWidget::Private
{
    WaveformSinkVerticalWidget *q = nullptr;
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
    waveforms::WaveformCollectionVerticalRasterData *plotData_ = nullptr;
    QwtPlotSpectrogram *plotItem_ = nullptr;
    std::vector<const waveforms::Trace *> currentCollection_;
    std::vector<waveforms::Trace> postProcessedTraces_;
    waveforms::Trace traceWorkBuffer_;
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
    curveFader->setLoopCount(-1); // loop forever until stopped
    curveFader->setDuration(1000);
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

    d->pb_printInfo_ = new QPushButton("Print Info");
    tb->addWidget(d->pb_printInfo_);

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

    d->currentCollection_.clear();

    auto selectedTraceIndex = d->traceSelect_->value();

    // pick a trace from the same column of each row in the trace history
    auto accu = [selectedTraceIndex] (std::vector<const waveforms::Trace *> &acc, const waveforms::TraceHistory &history)
    {
        if (0 <= selectedTraceIndex && static_cast<size_t>(selectedTraceIndex) < history.size())
            acc.push_back(&history[selectedTraceIndex]);
        return acc;
    };

    d->currentCollection_ = std::accumulate(
        std::begin(d->traceHistories_), std::end(d->traceHistories_), d->currentCollection_, accu);

    {
        auto traces = &d->currentCollection_;

        // do interpolation
        if (!traces->empty())
        {
            auto dtSample = d->spin_dtSample_->value();
            auto interpolationFactor = 1+ d->spin_interpolationFactor_->value();

            // grow/shrink if needed
            d->postProcessedTraces_.resize(traces->size());

            // clean remaining traces, keeping allocations intact
            std::for_each(std::begin(d->postProcessedTraces_), std::end(d->postProcessedTraces_),
                [] (waveforms::Trace &trace) { trace.clear(); });

            // interpolate
            mesytec::mvlc::util::for_each(std::begin(*traces), std::end(*traces), std::begin(d->postProcessedTraces_),
                [interpolationFactor] (const waveforms::Trace *trace, waveforms::Trace &interpolatedTrace)
                {
                    auto emitter = [&interpolatedTrace] (double x, double y)
                    {
                        interpolatedTrace.push_back(x, y);
                    };
                    waveforms::interpolate(trace->xs, trace->ys, interpolationFactor, emitter);
                });


            // TODO: somehow do the x scaling efficiently without having to keep another copy of the data
            // TODO: x scaling has to happen before interpolation
            #if 0
            // scale x values by dtsample
            std::for_each(std::begin(d->postProcessedTraces_), std::end(d->postProcessedTraces_),
                [dtSample] (waveforms::Trace &trace)
                {
                    // We now have raw trace data from the analysis. First scale the x
                    // coordinates, which currently are just array indexes, by dtSample.
                    std::transform(std::begin(trace.xs), std::end(trace.xs), std::begin(trace.xs),
                        [dtSample](double x) { return x * dtSample; });
                });
            #endif
        }
    }

    auto traces = &d->postProcessedTraces_;
    std::vector<const waveforms::Trace *> tracePointers(traces->size());
    std::transform(std::begin(*traces), std::end(*traces), std::begin(tracePointers),
        [] (const waveforms::Trace &trace) { return &trace; });
    // TODO: improve this interface
    d->plotData_->setTraceCollection(tracePointers);

    // determine axis scale ranges, update the zoomer, set plot axis scales
    if (!traces->empty())
    {
        // max sample count over all traces
        double xMax = std::max_element(std::begin(*traces), std::end(*traces),
            [] (const auto &lhs, const auto &rhs) { return lhs.size() < rhs.size(); })->size();

        // number of traces == row count
        double yMax = traces->size();

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

        if (d->zoomer_->zoomRectIndex() == 0)
        {
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

        d->plotData_->setInterval(Qt::XAxis, QwtInterval(0.0, xMax));
        d->plotData_->setInterval(Qt::YAxis, QwtInterval(0.0, yMax));
        d->plotData_->setInterval(Qt::ZAxis, QwtInterval(zMin, zMax));

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

    {
        auto curveAlpha = d->curveFader_->currentValue().value<int>();
        d->plotItem_->setAlpha(curveAlpha);
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

}
