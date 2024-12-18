#include "analysis/waveform_sink_2d_widget.h"

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
#include "analysis/waveform_sink_widget_common.h"
#include "mdpp-sampling/waveform_plotting.h"
#include "util/qt_logview.h"

using namespace mesytec::mvme;
using namespace mesytec::mvme::mdpp_sampling;

namespace analysis
{

static const int ReplotInterval_ms = 100;

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

inline void post_process_one_trace(TraceCollectionProcessingState &state,
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

inline void post_process_traces(TraceCollectionProcessingState &state,
    double dtSample, int interpolationFactor)
{
    state.outputTraces.resize(state.inputTraces.size());
    state.results.resize(state.inputTraces.size());

    for (size_t i = 0; i < state.inputTraces.size(); ++i)
    {
        post_process_one_trace(state, i, dtSample, interpolationFactor);
    }
}

struct WaveformSink2DWidget::Private
{
    WaveformSink2DWidget *q = nullptr;
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
    waveforms::WaveformCollectionVerticalRasterData *plotData_ = nullptr;
    QwtPlotSpectrogram *plotItem_ = nullptr;
    TraceCollectionProcessingState processingState_;

    QwtPlotZoomer *zoomer_ = nullptr;
    QTimer replotTimer_;
    mesytec::mvlc::util::Stopwatch frameTimer_;

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

WaveformSink2DWidget::WaveformSink2DWidget(
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

    setWindowTitle("WaveformSink2DWidget");

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

    connect(d->traceSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &WaveformSink2DWidget::replot);

    connect(&d->replotTimer_, &QTimer::timeout, this, &WaveformSink2DWidget::replot);
    d->replotTimer_.start();

    d->geoSaver_ = new WidgetGeometrySaver(this);
    d->geoSaver_->addAndRestore(this, "WindowGeometries/WaveformSink2DWidget");
}

WaveformSink2DWidget::~WaveformSink2DWidget()
{
}

void WaveformSink2DWidget::Private::updateUi()
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

void WaveformSink2DWidget::replot()
{
    spdlog::trace("begin WaveformSink2DWidget::replot()");

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
            //qDebug() << "WaveformSink2DWidget::replot(): setting xBottom xMax =" << xMax;
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

    spdlog::trace("end WaveformSink2DWidget::replot()");
}

void WaveformSink2DWidget::Private::makeInfoText(std::ostringstream &out)
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

void WaveformSink2DWidget::Private::makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame)
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

void WaveformSink2DWidget::Private::printInfo()
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

void WaveformSink2DWidget::Private::exportPlotToPdf()
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

void WaveformSink2DWidget::Private::exportPlotToClipboard()
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
