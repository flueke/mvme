#include "analysis/waveform_sink_test_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QMenu>
#include <QStack>
#include <QTimer>

//#include <mesytec-mvlc/util/algo.h>
#include <mesytec-mvlc/util/stopwatch.h>

#include "analysis_service_provider.h"
#include "analysis/waveform_sink_widget_common.h"
#include "mdpp-sampling/mdpp_sampling_p.h"
#include "mdpp-sampling/mdpp_sampling.h"
#include "util/qt_logview.h"

using namespace mesytec::mvme;
using namespace mesytec::mvme::mdpp_sampling;

inline QSpinBox *add_maxdepth_spin(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setMinimum(1);
    result->setMaximum(100);
    result->setValue(10);
    auto boxStruct = make_vbox_container(QSL("Max Traces"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
    return result;
}

inline waveforms::Trace maybe_recycle_trace(waveforms::TraceHistory &traceBuffer, size_t maxDepth)
{
    assert(maxDepth > 0);

    waveforms::Trace result;

    if (traceBuffer.size() >= maxDepth)
    {
        result = std::move(traceBuffer.back());
        traceBuffer.pop_back();
    }

    result.clear();
    return result;
}

struct CurvesWithData
{
    waveforms::WaveformCurves curves;
    waveforms::WaveformPlotData *rawData; // owned by curves.rawCurve
    waveforms::WaveformPlotData *interpolatedData; // owned by curves.interpolatedCurve
};

inline CurvesWithData make_curves_with_data()
{
    CurvesWithData result;
    result.curves = waveforms::make_curves();
    result.rawData = new waveforms::WaveformPlotData;
    result.curves.rawCurve->setData(result.rawData);
    result.interpolatedData = new waveforms::WaveformPlotData;
    result.curves.interpolatedCurve->setData(result.interpolatedData);
    return result;
}

namespace analysis
{

static const int ReplotInterval_ms = 100;
static const int CopyFromAnalysisInterval_ms = 100;

struct WaveformSinkDontKnowYetWidget::Private
{
    WaveformSinkDontKnowYetWidget *q = nullptr;
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
    waveforms::WaveformPlotCurveHelper curveHelper_;
    std::vector<waveforms::WaveformPlotCurveHelper::Handle> waveformHandles_;

    waveforms::TraceHistories analysisTraceData_;
    bool dataUpdated_ = false;

    // Post processed trace data. One history buffer per channel in the source
    // sink. A new trace is prepended to each history buffer every ReplotInterval_ms.
    waveforms::TraceHistories rawDisplayTraces_;            // x-scaling only, no interpolation
    waveforms::TraceHistories interpolatedDisplayTraces_;   // x-scaling and interpolation

    QwtPlotZoomer *zoomer_ = nullptr;
    QTimer replotTimer_;
    QTimer copyFromAnalysisTimer_;
    mesytec::mvlc::util::Stopwatch frameTimer_;

    QSpinBox *channelSelect_ = nullptr;
    QPushButton *pb_printInfo_ = nullptr;
    QDoubleSpinBox *spin_dtSample_ = nullptr;
    QSpinBox *spin_interpolationFactor_ = nullptr;
    // set both the max number of traces to keep per channel and the number of traces to show in the plot at the same time.
    QSpinBox *spin_maxDepth_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QCheckBox *cb_sampleSymbols_ = nullptr;
    QCheckBox *cb_interpolatedSymbols_ = nullptr;
    WidgetGeometrySaver *geoSaver_ = nullptr;
    QRectF maxBoundingRect_;
    QPushButton *pb_resetBoundingRect = nullptr;

    void updateDataFromAnalysis();
    void postProcessData();
    void updateUi();
    void makeInfoText(std::ostringstream &oss);
    void makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame);
    void printInfo();
    void exportPlotToPdf();
    void exportPlotToClipboard();
};

WaveformSinkDontKnowYetWidget::WaveformSinkDontKnowYetWidget(
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
    d->copyFromAnalysisTimer_.setInterval(CopyFromAnalysisInterval_ms);

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

    setWindowTitle("WaveformSinkDontKnowYetWidget");

    auto tb = getToolBar();

    tb->addSeparator();
    d->channelSelect_ = add_channel_select(tb);

    tb->addSeparator();
    d->spin_dtSample_ = add_dt_sample_setter(tb);

    tb->addSeparator();
    d->spin_interpolationFactor_ = add_interpolation_factor_setter(tb);

    tb->addSeparator();
    d->spin_maxDepth_ = add_maxdepth_spin(tb);

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

    connect(d->channelSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &WaveformSinkDontKnowYetWidget::replot);

    connect(&d->replotTimer_, &QTimer::timeout, this, &WaveformSinkDontKnowYetWidget::replot);
    d->replotTimer_.start();

    //connect(&d->copyFromAnalysisTimer_, &QTimer::timeout, this, [this] { d->updateDataFromAnalysis(); });
    //d->copyFromAnalysisTimer_.start();

    d->geoSaver_ = new WidgetGeometrySaver(this);
    d->geoSaver_->addAndRestore(this, "WindowGeometries/WaveformSinkDontKnowYetWidget");
}

WaveformSinkDontKnowYetWidget::~WaveformSinkDontKnowYetWidget()
{
}

void WaveformSinkDontKnowYetWidget::Private::updateDataFromAnalysis()
{
    auto newAnalysisTraceData = sink_->getTraceHistories();

    if (newAnalysisTraceData != analysisTraceData_)
    {
        analysisTraceData_ = newAnalysisTraceData;
        dataUpdated_ = true;
    }
}

void WaveformSinkDontKnowYetWidget::Private::postProcessData()
{
    // TODO/XXX: changing dtSample during a run will lead to funky results: the older
    // traces will have been interpolated with the previously set dtSample.
    // Could actually redo the sampling by just forcing x values to be
    // index*dtSample again, then reinterpolating each of the older traces... :)
    // TODO: when a run is finished this somehow must stop updating. I still
    // don't know how to really do this. Timestamp in the sink maybe?

    const auto dtSample = spin_dtSample_->value();
    const auto interpolationFactor = 1 + spin_interpolationFactor_->value();
    const size_t maxDepth = spin_maxDepth_->value();
    auto &analysisTraceData = analysisTraceData_;

    // This potentially removes Traces still referenced by underlying
    // QwtPlotCurves. Have to update/delete superfluous curves before calling
    // replot().
    rawDisplayTraces_.resize(analysisTraceData.size());
    interpolatedDisplayTraces_.resize(analysisTraceData.size());

    for (size_t chan=0; chan<analysisTraceData.size(); ++chan)
    {
        const auto &inputTraces = analysisTraceData[chan];

        auto &rawDestTraces = rawDisplayTraces_[chan];
        auto &ipolDestTraces = interpolatedDisplayTraces_[chan];

        while (rawDestTraces.size() > maxDepth)
            rawDestTraces.pop_back();

        while (ipolDestTraces.size() > maxDepth)
            ipolDestTraces.pop_back();

        if (!inputTraces.empty())
        {
            auto &inputTrace = inputTraces.front();

            auto rawDestTrace = maybe_recycle_trace(rawDestTraces, maxDepth);
            auto ipolDestTrace = maybe_recycle_trace(ipolDestTraces, maxDepth);

            rawDestTrace.clear();
            ipolDestTrace.clear();

            waveforms::scale_x_values(inputTrace, rawDestTrace, dtSample);
            waveforms::interpolate(rawDestTrace, ipolDestTrace, interpolationFactor);

            rawDestTraces.push_front(std::move(rawDestTrace));
            ipolDestTraces.push_front(std::move(ipolDestTrace));
        }
    }

    dataUpdated_ = false;
}

void WaveformSinkDontKnowYetWidget::Private::updateUi()
{
    // selector 1: Update the channel number spinbox
    const auto maxChannel = static_cast<signed>(rawDisplayTraces_.size()) - 1;
    channelSelect_->setMaximum(std::max(maxChannel, channelSelect_->maximum()));
}

void set_curve_alpha(QwtPlotCurve *curve, double alpha)
{
    auto pen = curve->pen();
    auto penColor = pen.color();
    penColor.setAlphaF(alpha);
    pen.setColor(penColor);
    curve->setPen(pen);
}

void WaveformSinkDontKnowYetWidget::replot()
{
    spdlog::trace("begin WaveformSinkDontKnowYetWidget::replot()");

    // TODO: use the timer again to decouple pulling the data and the post process and replot steps
    d->updateDataFromAnalysis();
    if (d->dataUpdated_)
        d->postProcessData();
    d->updateUi(); // update, selection boxes, buttons, etc.

    const auto channelCount = d->rawDisplayTraces_.size();
    const auto selectedChannel = d->channelSelect_->value();
    QRectF newBoundingRect = d->maxBoundingRect_;

    if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) < channelCount)
    {
        auto &rawTraces = d->rawDisplayTraces_[selectedChannel];
        auto &ipolTraces = d->interpolatedDisplayTraces_[selectedChannel];

        assert(rawTraces.size() == ipolTraces.size());

        while (d->waveformHandles_.size() < rawTraces.size())
        {
            auto cd = make_curves_with_data();
            d->waveformHandles_.push_back(d->curveHelper_.addWaveform(std::move(cd.curves)));
        }

        while (d->waveformHandles_.size() > rawTraces.size())
        {
            d->curveHelper_.takeWaveform(d->waveformHandles_.back());
            d->waveformHandles_.pop_back();
        }

        assert(d->waveformHandles_.size() == rawTraces.size());

        const auto traceCount = rawTraces.size();
        const double slope = (1.0 - 0.1) / traceCount; // want alpha from 1.0 to 0.1

        for (size_t traceIndex = 0; traceIndex < traceCount; ++traceIndex)
        {
            auto &rawTrace = rawTraces[traceIndex];
            auto &ipolTrace = ipolTraces[traceIndex];
            auto curves = d->curveHelper_.getWaveform(d->waveformHandles_[traceIndex]);
            auto rawData = reinterpret_cast<waveforms::WaveformPlotData *>(curves.rawCurve->data());
            auto ipolData = reinterpret_cast<waveforms::WaveformPlotData *>(curves.interpolatedCurve->data());
            assert(rawData && ipolData);
            rawData->setTrace(&rawTrace);
            ipolData->setTrace(&ipolTrace);

            d->curveHelper_.setRawSymbolsVisible(d->waveformHandles_[traceIndex], d->cb_sampleSymbols_->isChecked());
            d->curveHelper_.setInterpolatedSymbolsVisible(d->waveformHandles_[traceIndex], d->cb_interpolatedSymbols_->isChecked());

            newBoundingRect = newBoundingRect.united(ipolData->boundingRect());

            double alpha = 0.1 + slope * (traceCount - traceIndex);
            set_curve_alpha(curves.rawCurve, alpha);
            set_curve_alpha(curves.interpolatedCurve, alpha);
        }
    }

    waveforms::update_plot_axes(getPlot(), d->zoomer_, newBoundingRect, d->maxBoundingRect_);
    d->maxBoundingRect_ = newBoundingRect;

    histo_ui::PlotWidget::replot();

    std::ostringstream oss;
    d->makeStatusText(oss, d->frameTimer_.interval());

    getStatusBar()->clearMessage();
    getStatusBar()->showMessage(oss.str().c_str());

    spdlog::trace("end WaveformSinkDontKnowYetWidget::replot()");
}

void WaveformSinkDontKnowYetWidget::Private::makeInfoText(std::ostringstream &out)
{
}

void WaveformSinkDontKnowYetWidget::Private::makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame)
{
    auto selectedChannel = channelSelect_->value();
    auto visibleTraceCount = waveformHandles_.size();

    out << fmt::format("Channel: {}, #Traces: {}", selectedChannel, visibleTraceCount);
    out << fmt::format(", Frame time: {} ms", static_cast<int>(dtFrame.count()));
}

void WaveformSinkDontKnowYetWidget::Private::printInfo()
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

void WaveformSinkDontKnowYetWidget::Private::exportPlotToPdf()
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

void WaveformSinkDontKnowYetWidget::Private::exportPlotToClipboard()
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
