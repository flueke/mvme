#include "analysis/waveform_sink_widget.h"

#include <cmath>
#include <set>
#include <qwt_symbol.h>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include "analysis_service_provider.h"
#include "mdpp-sampling/mdpp_sampling_p.h"
#include "mdpp-sampling/mdpp_sampling.h"
#include "util/qt_logview.h"

using namespace mesytec::mvme;

namespace analysis
{

static const int ReplotInterval_ms = 33;

struct WaveformSinkWidget::Private
{
    WaveformSinkWidget *q = nullptr;
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
    TracePlotWidget *plotWidget_ = nullptr;
    QTimer replotTimer_;
    DecodedMdppSampleEvent currentEvent_;
    QSpinBox *channelSelect_ = nullptr;
    QSpinBox *traceSelect_ = nullptr;
    ModuleTraceHistory traceHistories_;
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
    void printInfo();
    void updatePlotAxisScales();
};

WaveformSinkWidget::WaveformSinkWidget(
    const std::shared_ptr<analysis::WaveformSink> &sink,
    AnalysisServiceProvider *asp,
    QWidget *parent)
    : histo_ui::IPlotWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->sink_ = sink;
    d->asp_ = asp;
    d->plotWidget_ = new TracePlotWidget(this);
    d->replotTimer_.setInterval(ReplotInterval_ms);
    connect(&d->replotTimer_, &QTimer::timeout, this, &WaveformSinkWidget::replot);
    d->replotTimer_.start();

    auto l = make_hbox<0, 0>(this);
    l->addWidget(d->plotWidget_);

    auto tb = getToolBar();

    {
        d->channelSelect_ = new QSpinBox;
        d->channelSelect_->setMinimum(0);
        d->channelSelect_->setMaximum(0);
        auto boxStruct = make_vbox_container(QSL("Channel"), d->channelSelect_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->traceSelect_ = new QSpinBox;
        d->traceSelect_->setMinimum(0);
        d->traceSelect_->setMaximum(1);
        d->traceSelect_->setSpecialValueText("latest");
        auto boxStruct = make_vbox_container(QSL("Trace#"), d->traceSelect_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->spin_dtSample_ = new QDoubleSpinBox;
        d->spin_dtSample_->setMinimum(1.0);
        d->spin_dtSample_->setMaximum(1e9);
        d->spin_dtSample_->setSingleStep(0.1);
        d->spin_dtSample_->setSuffix(" ns");
        d->spin_dtSample_->setValue(MdppDefaultSamplePeriod);

        auto pb_useDefaultSampleInterval = new QPushButton(QIcon(":/reset_to_default.png"), {});

        connect(pb_useDefaultSampleInterval, &QPushButton::clicked, this, [this] {
            d->spin_dtSample_->setValue(MdppDefaultSamplePeriod);
        });

        auto [w0, l0] = make_widget_with_layout<QWidget, QHBoxLayout>();
        l0->addWidget(d->spin_dtSample_);
        l0->addWidget(pb_useDefaultSampleInterval);

        auto boxStruct = make_vbox_container(QSL("Sample Interval"), w0, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->spin_interpolationFactor_ = new QSpinBox;
        d->spin_interpolationFactor_->setSpecialValueText("off");
        d->spin_interpolationFactor_->setMinimum(0);
        d->spin_interpolationFactor_->setMaximum(100);
        d->spin_interpolationFactor_->setValue(5);
        auto boxStruct = make_vbox_container(QSL("Interpolation Factor"), d->spin_interpolationFactor_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

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

    connect(d->pb_printInfo_, &QPushButton::clicked, this, [this] { d->printInfo(); });

    connect(d->channelSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &WaveformSinkWidget::replot);
    connect(d->traceSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &WaveformSinkWidget::replot);

    d->geoSaver_ = new WidgetGeometrySaver(this);
    d->geoSaver_->addAndRestore(this, "WindowGeometries/WaveformSinkWidget");
}

WaveformSinkWidget::~WaveformSinkWidget()
{
}

QwtPlot *WaveformSinkWidget::getPlot()
{
    return d->plotWidget_->getPlot();
}

const QwtPlot *WaveformSinkWidget::getPlot() const
{
    return d->plotWidget_->getPlot();
}


QToolBar *WaveformSinkWidget::getToolBar()
{
    return d->plotWidget_->getToolBar();
}

QStatusBar *WaveformSinkWidget::getStatusBar()
{
    return d->plotWidget_->getStatusBar();
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
        traceSelect_->setMaximum(std::max(1, tracebuffer.size()-1));
    }
    else
    {
        traceSelect_->setMaximum(0);
    }

    // add / remove symbols from / to the curves

    if (auto curve = plotWidget_->getRawCurve();
        curve && !cb_sampleSymbols_->isChecked())
    {
        curve->setSymbol(nullptr);
    }
    else if (curve && !curve->symbol())
    {
        auto crossSymbol = new QwtSymbol(QwtSymbol::Diamond);
        crossSymbol->setSize(QSize(5, 5));
        crossSymbol->setColor(Qt::red);
        curve->setSymbol(crossSymbol);
    }

    if (auto curve = plotWidget_->getInterpolatedCurve();
        curve && !cb_interpolatedSymbols_->isChecked())
    {
        curve->setSymbol(nullptr);
    }
    else if (curve && !curve->symbol())
    {
        auto crossSymbol = new QwtSymbol(QwtSymbol::Triangle);
        crossSymbol->setSize(QSize(5, 5));
        crossSymbol->setColor(Qt::blue);
        curve->setSymbol(crossSymbol);
    }
}

void WaveformSinkWidget::replot()
{
    spdlog::trace("begin WaveformSinkWidget::replot()");

    // Thread-safe copy of the trace history shared with the analysis runtime.
    // Might be very expensive depending on the size of the trace history.
    d->traceHistories_ = d->sink_->getTraceHistory();

    d->updateUi(); // update, selection boxes, buttons, etc.

    ChannelTrace *trace = nullptr;

    const auto channelCount = d->traceHistories_.size();
    const auto selectedChannel = d->channelSelect_->value();

    if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) < channelCount)
    {
        auto &tracebuffer = d->traceHistories_[selectedChannel];

        // selector 3: trace number in the trace history. 0 is the latest trace.
        auto selectedTraceIndex = d->traceSelect_->value();

        if (0 <= selectedTraceIndex && selectedTraceIndex < tracebuffer.size())
            trace = &tracebuffer[selectedTraceIndex];
    }

    if (trace)
    {
        auto interpolationFactor = 1+ d->spin_interpolationFactor_->value();
        auto dtSample = d->spin_dtSample_->value();

        trace->dtSample = dtSample;
        interpolate(*trace, interpolationFactor);
    }

    d->plotWidget_->setTrace(trace);
    d->updatePlotAxisScales();
    d->plotWidget_->replot();

    auto sb = getStatusBar();
    sb->clearMessage();

    if (trace)
    {
        // TODO: update the status bar text
        sb->showMessage("TODO: update the status bar text");
    }

    spdlog::trace("end WaveformSinkWidget::replot()");
}

void WaveformSinkWidget::Private::printInfo()
{
    auto trace = plotWidget_->getTrace();

    if (!trace)
        return;

    QString text;
    QTextStream out(&text);

    auto moduleName = asp_->getVMEConfig()->getModuleConfig(trace->moduleId)->getObjectPath();

    out << QSL("module %1\n").arg(moduleName);
    out << fmt::format("linear event number: {}\n", trace->eventNumber).c_str();
    out << fmt::format("amplitude: {}\n", trace->amplitude).c_str();
    out << fmt::format("time: {}\n", trace->time).c_str();
    out << fmt::format("sampleCount: {}\n", trace->samples.size()).c_str();
    out << fmt::format("samples: [{}]\n", fmt::join(trace->samples, ", ")).c_str();
    out << fmt::format("interpolated: [{}]\n", fmt::join(trace->interpolated, ", ")).c_str();
    out << fmt::format("interpolatedCount: {}\n", trace->interpolated.size()).c_str();
    out << fmt::format("dtSample: {}\n", trace->dtSample).c_str();

    if (!logView_)
    {
        logView_ = make_logview().release();
        logView_->setWindowTitle("MDPP Sampling Mode: Trace Info");
        logView_->setAttribute(Qt::WA_DeleteOnClose);
        logView_->resize(1000, 600);
        connect(logView_, &QWidget::destroyed, q, [this] { logView_ = nullptr; });
        add_widget_close_action(logView_);
        geoSaver_->addAndRestore(logView_, "WindowGeometries/WaveformSinkWidgetLogView");
    }

    assert(logView_);
    logView_->setPlainText(text);
    logView_->show();
    logView_->raise();
}

void adjust_y_axis_scale(QwtPlot *plot, double yMin, double yMax, QwtPlot::Axis axis = QwtPlot::yLeft)
{
    if (histo_ui::is_logarithmic_axis_scale(plot, QwtPlot::yLeft))
    {
        if (yMin <= 0.0)
            yMin = 0.1;

        yMin = std::max(yMin, yMax);
    }

    {
        // Scale the y-axis by 5% to have some margin to the top and bottom of the
        // widget. Mostly to make the top scrollbar not overlap the plotted graph.
        yMin *= (yMin < 0.0) ? 1.05 : 0.95;
        yMax *= (yMax < 0.0) ? 0.95 : 1.05;
    }

    plot->setAxisScale(axis, yMin, yMax);
}

void WaveformSinkWidget::Private::updatePlotAxisScales()
{
    spdlog::trace("entering WaveformSinkWidget::Private::updatePlotAxisScales()");

    auto plot = plotWidget_->getPlot();
    auto zoomer = histo_ui::get_zoomer(plotWidget_);
    assert(plot && zoomer);

    // Grow the bounding rect to the max of every trace seen in this run to keep
    // the display from jumping when switching between traces.
    auto newBoundingRect = maxBoundingRect_;

    if (!newBoundingRect.isValid())
    {
        newBoundingRect = plotWidget_->traceBoundingRect();
        auto xMin = newBoundingRect.left();
        auto xMax = newBoundingRect.right();
        auto yMax = newBoundingRect.bottom();
        auto yMin = newBoundingRect.top();
        spdlog::trace("updatePlotAxisScales(): setting initial bounding rect from trace: xMin={}, xMax={}, yMin={}, yMax={}", xMin, xMax, yMin, yMax);
    }

    newBoundingRect = newBoundingRect.united(plotWidget_->traceBoundingRect());

    spdlog::trace("updatePlotAxisScales(): zoomRectIndex()={}", zoomer->zoomRectIndex());

    if (!maxBoundingRect_.isValid() || zoomer->zoomRectIndex() == 0)
    {
        auto xMin = newBoundingRect.left();
        auto xMax = newBoundingRect.right();
        auto yMax = newBoundingRect.bottom();
        auto yMin = newBoundingRect.top();

        spdlog::trace("updatePlotAxisScales(): updatePlotAxisScales(): forcing axis scales to: xMin={}, xMax={}, yMin={}, yMax={}", xMin, xMax, yMin, yMax);

        plot->setAxisScale(QwtPlot::xBottom, xMin, xMax);
        adjust_y_axis_scale(plot, yMin, yMax);
        plot->updateAxes();

        if (zoomer->zoomRectIndex() == 0)
        {
            spdlog::trace("updatePlotAxisScales(): zoomer fully zoomed out -> setZoomBase()");
            zoomer->setZoomBase();
        }
    }

    maxBoundingRect_ = newBoundingRect;
}

}
