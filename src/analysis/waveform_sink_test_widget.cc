#include "analysis/waveform_sink_test_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QMenu>
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

    // Copy of the data stored in the WaveformSink. Sampled every CopyFromAnalysisInterval_ms.
    waveforms::TraceHistories analysisTraceData_;

    // Post processed trace data. One history buffer per channel in the source
    // sink. A new trace is prepended to each history buffer every ReplotInterval_ms.
    waveforms::TraceHistories displayTraceData_;
    waveforms::Trace traceWorkBuffer_;

    QwtPlotZoomer *zoomer_ = nullptr;
    QTimer replotTimer_;
    QTimer copyFromAnalysisTimer_;
    mesytec::mvlc::util::Stopwatch frameTimer_;

    QSpinBox *channelSelect_ = nullptr;
    QPushButton *pb_printInfo_ = nullptr;
    QDoubleSpinBox *spin_dtSample_ = nullptr;
    QSpinBox *spin_interpolationFactor_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QCheckBox *cb_sampleSymbols_ = nullptr;
    QCheckBox *cb_interpolatedSymbols_ = nullptr;
    WidgetGeometrySaver *geoSaver_ = nullptr;
    QRectF maxBoundingRect_;
    QPushButton *pb_resetBoundingRect = nullptr;

    void updateDataFromAnalysis();
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

    connect(&d->copyFromAnalysisTimer_, &QTimer::timeout, this, [this] { d->updateDataFromAnalysis(); });
    d->copyFromAnalysisTimer_.start();

    d->geoSaver_ = new WidgetGeometrySaver(this);
    d->geoSaver_->addAndRestore(this, "WindowGeometries/WaveformSinkDontKnowYetWidget");
}

WaveformSinkDontKnowYetWidget::~WaveformSinkDontKnowYetWidget()
{
}

void WaveformSinkDontKnowYetWidget::Private::updateDataFromAnalysis()
{
    analysisTraceData_ = sink_->getTraceHistories();
}

void WaveformSinkDontKnowYetWidget::Private::updateUi()
{
    // selector 1: Update the channel number spinbox
    const auto maxChannel = static_cast<signed>(displayTraceData_.size()) - 1;
    channelSelect_->setMaximum(std::max(maxChannel, channelSelect_->maximum()));
}

void WaveformSinkDontKnowYetWidget::replot()
{
    spdlog::trace("begin WaveformSinkDontKnowYetWidget::replot()");

    d->updateUi(); // update, selection boxes, buttons, etc.

    waveforms::Trace *trace = nullptr;

    const auto channelCount = d->displayTraceData_.size();
    const auto selectedChannel = d->channelSelect_->value();

    if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) < channelCount)
    {
        auto &tracebuffer = d->displayTraceData_[selectedChannel];

        // TODO: update all existing curves with the data from the tracebuffer
    }

    #if 0
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
    #endif

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
