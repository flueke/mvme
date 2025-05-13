#include "analysis/waveform_sink_1d_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QMenu>
#include <QStack>
#include <QTimer>
#include <qwt_legend.h>
#include <map>

#include <mesytec-mvlc/util/stopwatch.h>

#include "analysis_service_provider.h"
#include "analysis/waveform_sink_widget_common.h"
#include "mdpp-sampling/waveform_plotting.h"
#include "mvme_qwt.h"
#include "util/qledindicator.h"
#include "util/qt_logview.h"

using namespace mesytec::mvme;
using namespace mesytec::mvme::mdpp_sampling;

inline QSpinBox *add_maxdepth_spin(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setMinimum(1);
    result->setMaximum(100);
    result->setValue(10);
    auto boxStruct = make_vbox_container(QSL("Max Traces per Channel"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
    return result;
}

enum RefreshMode
{
    RefreshMode_LatestData,
    RefreshMode_EventSnapshot
};

template<>
struct fmt::formatter<RefreshMode>: fmt::formatter<string_view>
{
    auto format(RefreshMode mode, fmt::format_context &ctx) const
    {
        switch (mode)
        {
        case RefreshMode_LatestData:
            return format_to(ctx.out(), "Latest Data");
        case RefreshMode_EventSnapshot:
            return format_to(ctx.out(), "Event Snapshot");
        default:
            return format_to(ctx.out(), "Unknown RefreshMode");
        }
    }
};

inline QComboBox *add_mode_selector(QToolBar *toolbar)
{
    auto result = new QComboBox;
    result->addItem(fmt::format("{}", RefreshMode_LatestData).c_str(), RefreshMode_LatestData);
    result->addItem(fmt::format("{}", RefreshMode_EventSnapshot).c_str(), RefreshMode_EventSnapshot);
    auto boxstruct = make_vbox_container("Refresh Mode", result, 0, -2);
    toolbar->addWidget(boxstruct.container.release());
    result->setToolTip(QSL(
        "Latest Data: Buffers and displays the latest data from the module, not neccessarily belonging to the same readout event.\n"
        "Event Snapshot: Shows data originating from the same readout event."
    ));
    return result;
}

inline QSpinBox *add_trace_selector(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setMinimum(0);
    result->setMaximum(1);
    result->setSpecialValueText("latest");
    result->setValue(0);
    auto boxStruct = make_vbox_container(QSL("Trace#"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
    return result;
}


struct CurvesWithData
{
    waveforms::WaveformCurves curves;
    waveforms::WaveformPlotData *rawData; // owned by curves.rawCurve (qwt)
    waveforms::WaveformPlotData *interpolatedData; // owned by curves.interpolatedCurve (qwt)
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

struct DisplayParams
{
    RefreshMode refreshMode = RefreshMode_LatestData;
    size_t maxTracesPerChannel = 10;    // "maxDepth", the maximum number of traces to keep per channel
    size_t traceIndex = 0;              // in RefreshMode_EventSnapshot this is the linear index of the trace to show
    size_t chanMin = 0;                 // first channel index to show
    size_t chanMax = 0;                 // last channel index to show
    bool showSampleSymbols = false;
    bool showInterpolatedSymbols = false;
    double dtSample = 0.0;
    waveforms::PhaseCorrectionMode phaseCorrection = waveforms::PhaseCorrection_Auto;
};

struct WaveformSink1DWidget::Private
{
    WaveformSink1DWidget *q = nullptr;

    // Source sink to pull data from.
    std::shared_ptr<analysis::WaveformSink> sink_;

    // Provides access to the system context and other services.
    AnalysisServiceProvider *asp_ = nullptr;

    // Helpers for plotting and managing qwt curves, etc.
    waveforms::WaveformPlotCurveHelper curveHelper_;
    std::map<size_t, std::vector<waveforms::WaveformPlotCurveHelper::Handle>> channelToWaveformHandles_;

    // Unmodified copy of the latest trace data from the analysis sink.
    waveforms::TraceHistories analysisTraceSnapshot_;

    // When analysisTraceSnapshot_ was last updated. Might have to make this more
    // fancy to detect if there is actual new data in the new snapshot from the
    // sink.
    QTime traceDataUpdateTime_;

    // Trace histories for displaying. In RefreshMode_LatestData non-empty
    // traces from analysisTraceSnapshot_ are prepended to the trace histories.
    // In RefreshMode_EventSnapshot this is just a copy of analysisTraceSnapshot_.
    waveforms::TraceHistories displayTraceData_;

    // Linear list of traces that are displayed in the plot. This holds the data
    // that is referenced by the qwt curves when plotting.
    waveforms::TraceHistory tracesToPlot_;

    RefreshMode refreshMode_ = RefreshMode_LatestData;
    RefreshMode prevRefreshMode_ = refreshMode_;

    QwtPlotZoomer *zoomer_ = nullptr;
    QTimer replotTimer_;
    mesytec::mvlc::util::Stopwatch frameTimer_;

    QComboBox *combo_modeSelect_ = nullptr;
    QSpinBox *traceSelect_ = nullptr;
    QSpinBox *spin_chanSelect = nullptr;
    QCheckBox *cb_showAllChannels_ = nullptr;
    bool showSampleSymbols_ = false;
    bool showInterpolatedSymbols_ = false;
    QAction *actionHold_ = nullptr;
    QPushButton *pb_printInfo_ = nullptr;
    QDoubleSpinBox *spin_dtSample_ = nullptr;
    QCheckBox *cb_showSamples_ = nullptr;
    QAction *actionInterpolation_ = nullptr;
    InterpolationSettingsUi *interpolationUi_ = nullptr;
    QComboBox *combo_phaseCorrection_ = nullptr;
    // set both the max number of traces to keep per channel and the number of traces to show in the plot at the same time.
    QSpinBox *spin_maxDepth_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QLedIndicator *updateLed_ = nullptr;

    WidgetGeometrySaver *geoSaver_ = nullptr;
    QRectF maxBoundingRect_;

    bool selectedTraceChanged_ = false;
    bool selectedChannelChanged_ = false;
    bool interpolationFactorChanged_ = false;

    bool updateDataFromAnalysis(); // Returns true if new data was copied, false if the data was unchanged.
    void updateDisplayTraceData(const DisplayParams &params, const waveforms::TraceHistories &analysisTraceData);
    void processTracesToPlot(const DisplayParams &params);


    void postProcessData();
    void updateUi();
    void resetPlotAxes();
    void makeInfoText(std::ostringstream &oss);
    void makeStatusText(std::ostringstream &out, const QTime &lastUpdate);
    void printInfo();
    void exportPlotToPdf();
    void exportPlotToClipboard();

    void setInterpolationUiVisible(bool visible)
    {
        interpolationUi_->setVisible(visible);
        s32 x = q->width() - interpolationUi_->width();
        s32 y = q->getToolBar()->height() + 15;
        interpolationUi_->move(q->mapToGlobal(QPoint(x, y)));
    }

    std::unique_ptr<waveforms::IInterpolator> makeInterpolator()
    {
        if (interpolationUi_->getInterpolationType() == "sinc")
        {
            return std::make_unique<waveforms::SincInterpolator>(interpolationUi_->getInterpolationFactor());
        }
        else if (interpolationUi_->getInterpolationType() == "spline")
        {
            return std::make_unique<waveforms::SplineInterpolator>(
                interpolationUi_->getSplineParams(),
                interpolationUi_->getInterpolationFactor());
        }

        return std::make_unique<waveforms::NullInterpolator>();
    }
};

WaveformSink1DWidget::WaveformSink1DWidget(
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

    setWindowTitle("WaveformSink1DWidget");
    getPlot()->axisWidget(QwtPlot::xBottom)->setTitle("Time [ns]");

    auto legend = new mvme_qwt::StableQwtLegend;
    legend->setDefaultItemMode(QwtLegendData::Checkable);
    getPlot()->insertLegend(legend, QwtPlot::LeftLegend);

    connect(getPlot(), &QwtPlot::legendDataChanged, getPlot(), [this] (const QVariant &itemInfo, const QList<QwtLegendData> &data) {
        auto item = itemInfo.value<QwtPlotItem *>();
        auto itemList = this->getPlot()->itemList(QwtPlotItem::Rtti_PlotCurve);
        qDebug() << "legendDataChanged" << itemInfo << item->title().text() << item->rtti() << data.size() << itemList.size();
    });

    auto tb = getToolBar();
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    set_widget_font_pointsize(tb, 7);

    d->zoomer_ = histo_ui::install_scrollzoomer(this);
    tb->addSeparator();

    d->combo_modeSelect_ = add_mode_selector(tb);

    d->actionHold_ = tb->addAction(QIcon(":/control_pause.png"), "Hold");
    d->actionHold_->setCheckable(true);
    d->actionHold_->setChecked(false);
    tb->addSeparator();

    d->traceSelect_ = add_trace_selector(tb);
    d->traceSelect_->setEnabled(false);
    tb->addSeparator();

    auto [chanSelWidget, chanSelLayout] = make_widget_with_layout<QWidget, QGridLayout>();
    d->spin_chanSelect = new QSpinBox;
    d->cb_showAllChannels_ = new QCheckBox("All");

    chanSelLayout->addWidget(new QLabel("Channel Select"), 0, 0, 1, 2, Qt::AlignCenter);
    chanSelLayout->addWidget(d->spin_chanSelect, 1, 0);
    chanSelLayout->addWidget(d->cb_showAllChannels_, 1, 1);

    tb->addWidget(chanSelWidget);
    tb->addSeparator();

    tb->addAction(QIcon(":/selection-resize.png"), QSL("Fit Axes"), this, [this] {
        d->resetPlotAxes();
        replot();
    });

    {
        auto menu = new QMenu;
        auto a1 = menu->addAction("Sampled Values", this, [this] { d->showSampleSymbols_ = !d->showSampleSymbols_; replot(); });
        auto a2 = menu->addAction("Interpolated Values", this, [this] { d->showInterpolatedSymbols_ = !d->showInterpolatedSymbols_; replot(); });
        for (auto a: {a1, a2})
        {
            a->setCheckable(true);
            a->setChecked(false);
        }
        auto button = make_toolbutton(QSL(":/resources/asterisk.png"), QSL("Symbols"));
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        tb->addWidget(button);
    }

    d->spin_maxDepth_ = add_maxdepth_spin(tb);

    DO_AND_ASSERT(connect(histo_ui::get_zoomer(this), SIGNAL(zoomed(const QRectF &)), this, SLOT(replot())));

    // enable both the zoomer and mouse cursor tracker by default

    if (auto zoomAction = findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);

    //histo_ui::install_tracker_picker(this);
    //if (auto trackerPickerAction = findChild<QAction *>("trackerPickerAction"))
    //    trackerPickerAction->setChecked(true);

    tb->addSeparator();
    auto dtSampleUi = add_dt_sample_setter(tb);
    d->spin_dtSample_ = dtSampleUi.spin_dtSample;
    d->cb_showSamples_ = dtSampleUi.cb_showSamples;
    tb->addSeparator();
    d->interpolationUi_ = new InterpolationSettingsUi(this);
    d->actionInterpolation_ = tb->addAction(QIcon(QSL(":/interpolation.png")), QSL("Interpolation"));
    d->actionInterpolation_->setCheckable(true);
    d->actionInterpolation_->setChecked(false);
    tb->addSeparator();

    {
        d->combo_phaseCorrection_ = new QComboBox;
        d->combo_phaseCorrection_->addItem("Auto", waveforms::PhaseCorrectionMode::PhaseCorrection_Auto);
        d->combo_phaseCorrection_->addItem("On", waveforms::PhaseCorrectionMode::PhaseCorrection_On);
        d->combo_phaseCorrection_->addItem("Off", waveforms::PhaseCorrectionMode::PhaseCorrection_Off);
        d->combo_phaseCorrection_->setToolTip(QSL(
            "Auto: use phase correction if 'NoResampling' is set in the traces 'config' field.\n"
            "On: always use phase correction.\n"
            "Off: never use phase correction."));
        auto [widget, layout] = make_widget_with_layout<QWidget, QVBoxLayout>();
        layout->addWidget(new QLabel("Phase Correction"), 0, Qt::AlignCenter);
        layout->addWidget(d->combo_phaseCorrection_);
        tb->addWidget(widget);
    }

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

    tb->addSeparator();
    d->pb_printInfo_ = new QPushButton(QIcon(":/info.png"), "Show Info");
    tb->addWidget(d->pb_printInfo_);

    d->updateLed_ = new QLedIndicator;
    d->updateLed_->setEnabled(false); // no clicky on the thingy
    d->updateLed_->setMinimumSize(16, 16);
    d->updateLed_->setToolTip("Indicates when new data is available");
    getStatusBar()->addPermanentWidget(d->updateLed_);

    connect(d->pb_printInfo_, &QPushButton::clicked, this, [this] { d->printInfo(); });

    connect(d->combo_modeSelect_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this] {
        d->prevRefreshMode_ = d->refreshMode_;
        d->refreshMode_ = static_cast<RefreshMode>(d->combo_modeSelect_->currentData().toInt());
        d->traceSelect_->setEnabled(d->refreshMode_ == RefreshMode_EventSnapshot);
        replot();
    });

    connect(d->traceSelect_, qOverload<int>(&QSpinBox::valueChanged),
        this, [this]  {
        d->selectedTraceChanged_ = true;
        replot();
    });

    connect(d->spin_chanSelect, qOverload<int>(&QSpinBox::valueChanged),
        this, [this] { d->selectedChannelChanged_ = true; replot(); });

    connect(d->cb_showAllChannels_, &QCheckBox::stateChanged, this, [this] (int state) {
        d->spin_chanSelect->setEnabled(state == Qt::Unchecked);
        d->selectedChannelChanged_ = true;
        replot();
    });

    auto on_sample_interval_changed = [this]
    {
        // Shrink the grid back to the minimum size when dtSample changes.
        // Nice-to-have when decreasing dtSample, not much impact when
        // increasing it. Note: this forces a QwtPlot::replot() internally.
        d->resetPlotAxes();
    };

    connect(d->spin_dtSample_, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, on_sample_interval_changed);

    connect(dtSampleUi.cb_showSamples, &QCheckBox::stateChanged,
        this, on_sample_interval_changed);

    connect(d->actionInterpolation_, &QAction::triggered,
        this, [this] (bool checked) { d->setInterpolationUiVisible(checked); });

    connect(d->interpolationUi_, &QDialog::rejected, this, [this] {
        d->actionInterpolation_->setChecked(false);
    });

    connect(d->interpolationUi_, &InterpolationSettingsUi::interpolationFactorChanged, this,
            [this]
            {
                d->interpolationFactorChanged_ = true;
                replot();
            });

    connect(&d->replotTimer_, &QTimer::timeout, this, &WaveformSink1DWidget::replot);
    d->replotTimer_.start();

    d->geoSaver_ = new WidgetGeometrySaver(this);
    d->geoSaver_->addAndRestore(this, "WindowGeometries/WaveformSink1DWidget");
}

WaveformSink1DWidget::~WaveformSink1DWidget()
{
}

bool WaveformSink1DWidget::Private::updateDataFromAnalysis()
{
    auto newAnalysisTraceData = sink_->getTraceHistories();

    // Note: this makes the method return true even if no single trace was
    // decoded. The reason is that the trace meta (module_header, timestamp)
    // changes, so the != comparsion is true if the module produced new data.
    if (newAnalysisTraceData != analysisTraceSnapshot_)
    {
        std::swap(analysisTraceSnapshot_, newAnalysisTraceData);
        traceDataUpdateTime_ = QTime::currentTime();
        return true;
    }

    return false;
}

// This decides how the most recent data from the analysis sink is integrated:
// either prepend the latest, non-empty traces to the local trace history buffers
// or replace the local buffers with the latest snapshot from the analysis sink.
void WaveformSink1DWidget::Private::updateDisplayTraceData(
    const DisplayParams &params,
    const waveforms::TraceHistories &analysisTraceData)
{
    if (params.refreshMode == RefreshMode_EventSnapshot)
    {
        // In EventSnapshot mode we just copy the analysis trace data to the
        // display traces.
        displayTraceData_ = analysisTraceData;
    }
    else
    {
        // In LatestData mode we prepend the latest traces from the analysis
        // sink to the display traces.
        waveforms::prepend_latest_traces(analysisTraceData, displayTraceData_, params.maxTracesPerChannel);
    }
}

// Pick the traces that are going to the displayed and run post-processing on them.
void WaveformSink1DWidget::Private::processTracesToPlot(const DisplayParams &params)
{
    auto interpolator = makeInterpolator();
    auto &tracesToPlot = tracesToPlot_;
    tracesToPlot.clear();

    // TODO: optimize this later by reusing qwt plot curves
    curveHelper_.clear();
    channelToWaveformHandles_.clear();

    // traverse row wise from first to last channel
    for (size_t chanIndex = params.chanMin; chanIndex < params.chanMax; ++chanIndex)
    {
        if (chanIndex >= displayTraceData_.size())
            break;

        const auto &channelTraces = displayTraceData_[chanIndex];

        // traverse column wise, from newest to oldest trace
        for (size_t traceIndex = params.traceIndex; traceIndex < params.traceIndex + params.maxTracesPerChannel; ++traceIndex)
        {
            if (traceIndex >= channelTraces.size())
                break;

            const auto &inputTrace = channelTraces[traceIndex];

            double phase = 1.0;
            u32 traceConfig = 0;

            if (params.phaseCorrection != waveforms::PhaseCorrection_Off)
            {
                if (auto it = inputTrace.meta.find("config"); it != std::end(inputTrace.meta))
                    traceConfig = std::get<u32>(it->second);

                if (auto it = inputTrace.meta.find("phase"); it != std::end(inputTrace.meta))
                    phase = std::get<double>(it->second);
            }

            if (params.phaseCorrection == waveforms::PhaseCorrection_Auto)
            {
                if (!(traceConfig & mdpp_sampling::SamplingSettings::NoResampling))
                {
                    phase = 1.0; // was done in the FPGA, turn it off here
                }
            }

            waveforms::Trace rawTrace;
            waveforms::scale_x_values(inputTrace, rawTrace, params.dtSample, phase);

            waveforms::Trace interpolatedTrace;
            (*interpolator)(rawTrace, interpolatedTrace);

            // Create new qwt curve and data objects and assign the data to them.
            auto curves = make_curves_with_data();
            curves.rawData->setTrace(&tracesToPlot_.emplace_back(std::move(rawTrace)));
            curves.interpolatedData->setTrace(&tracesToPlot_.emplace_back(std::move(interpolatedTrace)));

            // Store the handle in a per channel list.
            channelToWaveformHandles_[chanIndex].emplace_back(curveHelper_.addWaveform(std::move(curves.curves)));
        }
    }
}

void WaveformSink1DWidget::Private::updateUi()
{
    if (sink_)
    {
        auto pathParts = analysis::make_parent_path_list(sink_);
        pathParts.push_back(sink_->objectName());
        q->setWindowTitle(pathParts.join('/'));
    }

    // selector 1: Update the channel number spinbox
    const auto maxChannel = static_cast<signed>(analysisTraceSnapshot_.size()) - 1;
    spin_chanSelect->setMaximum(std::max(0, maxChannel));

    // selector 2: Update the trace number spinbox
    size_t maxTrace = 0;

    if (!analysisTraceSnapshot_.empty() && !analysisTraceSnapshot_.front().empty())
        maxTrace = analysisTraceSnapshot_.front().size() - 1;

    traceSelect_->setMaximum(maxTrace);
}

// Warning: do not call this from within replot()! That will lead to infinite
// recursion.
void WaveformSink1DWidget::Private::resetPlotAxes()
{
    maxBoundingRect_ = {};
    zoomer_->setZoomStack(QStack<QRectF>(), -1);
    zoomer_->zoom(0);
}

inline void set_curve_color(QwtPlotCurve *curve, const QColor &color)
{
    auto pen = curve->pen();
    pen.setColor(color);
    curve->setPen(pen);
}

inline void set_curve_alpha(QwtPlotCurve *curve, double alpha)
{
    auto pen = curve->pen();
    auto penColor = pen.color();
    penColor.setAlphaF(std::min(alpha, 1.0));
    pen.setColor(penColor);
    curve->setPen(pen);
}

inline const QVector<QColor> make_plot_colors()
{
    static const QVector<QColor> result =
    {
        "#000000",
        "#e6194b",
        "#3cb44b",
        "#ffe119",
        "#0082c8",
        "#f58231",
        "#911eb4",
        "#46f0f0",
        "#f032e6",
        "#d2f53c",
        "#fabebe",
        "#008080",
        "#e6beff",
        "#aa6e28",
        "#fffac8",
        "#800000",
        "#aaffc3",
        "#808000",
        "#ffd8b1",
        "#000080",
    };

    return result;
};

void WaveformSink1DWidget::replot()
{
    spdlog::trace("begin WaveformSink1DWidget::replot()");
    qDebug() << "enter WaveformSink1DWidget::replot()";

    const bool gotNewData = !d->actionHold_->isChecked() ?  d->updateDataFromAnalysis() : false;

    d->updateUi(); // update, selection boxes, buttons, etc.

    const auto channelCount = d->analysisTraceSnapshot_.size();
    const bool showAllChannels = d->cb_showAllChannels_->isChecked();
    const bool xAxisShowsSamples = d->cb_showSamples_->isChecked();

    DisplayParams params;
    params.refreshMode = d->refreshMode_;
    params.maxTracesPerChannel = d->spin_maxDepth_->value();
    params.traceIndex = params.refreshMode == RefreshMode_LatestData ? 0 : d->traceSelect_->value();
    params.chanMax = channelCount;
    if (!showAllChannels)
    {
        auto selectedChannel = d->spin_chanSelect->value();
        selectedChannel = std::clamp(selectedChannel, 0, static_cast<int>(channelCount) - 1);
        params.chanMin = selectedChannel;
        params.chanMax = selectedChannel + 1;
    }
    else
    {
        params.chanMin = 0;
        params.chanMax = channelCount;
    }
    params.showSampleSymbols = d->showSampleSymbols_;
    params.showInterpolatedSymbols = d->showInterpolatedSymbols_;
    params.dtSample = xAxisShowsSamples ? 1.0 : d->spin_dtSample_->value();

    if (gotNewData)
        d->updateDisplayTraceData(params, d->analysisTraceSnapshot_);

    d->processTracesToPlot(params);

    size_t totalChannels = d->channelToWaveformHandles_.size();
    size_t totalHandles = std::accumulate(
        std::begin(d->channelToWaveformHandles_),
        std::end(d->channelToWaveformHandles_),
        0,
        [] (size_t sum, const auto &pair) { return sum + pair.second.size(); });

    spdlog::debug("WaveformSink1DWidget::replot(): totalChannels={}, totalHandles={}", totalChannels, totalHandles);
    auto itemList = this->getPlot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    qDebug() << "replot(): #PlotCurves=" <<  itemList.size();

    static const auto colors = make_plot_colors();
    QRectF newBoundingRect = d->maxBoundingRect_;

    for (auto &[chanIndex, handles]: d->channelToWaveformHandles_)
    {
        //spdlog::warn("WaveformSink1DWidget::replot(): channelIndex={}, #handles={}", chanIndex, handles.size());
        const auto traceCount = handles.size();
        const double slope = (1.0 - 0.1) / traceCount; // want alpha from 1.0 to 0.1
        const auto traceColor = colors.value(chanIndex % colors.size());

        for (size_t traceIndex=0; traceIndex < traceCount; ++traceIndex)
        {
            auto &handle = handles[traceIndex];
            d->curveHelper_.setRawSymbolsVisible(handle, params.showSampleSymbols);
            d->curveHelper_.setInterpolatedSymbolsVisible(handle, params.showInterpolatedSymbols);

            double alpha = std::min(0.1 + slope * (traceCount - traceIndex), 1.0);
            auto thisColor = traceColor;
            thisColor.setAlphaF(alpha);

            auto curves = d->curveHelper_.getWaveform(handle);
            set_curve_color(curves.rawCurve, thisColor);
            set_curve_color(curves.interpolatedCurve, thisColor);

            // adjust alpha of the symbols as well
            if (params.showSampleSymbols)
            {
                auto symCache = d->curveHelper_.getRawSymbolCache(handle);
                mvme_qwt::set_symbol_cache_alpha(symCache, alpha);
                auto newSymbol = mvme_qwt::make_symbol_from_cache(symCache);
                curves.rawCurve->setSymbol(newSymbol.release());
            }

            if (params.showInterpolatedSymbols)
            {
                auto symCache = d->curveHelper_.getInterpolatedSymbolCache(handle);
                mvme_qwt::set_symbol_cache_alpha(symCache, alpha);
                auto newSymbol = mvme_qwt::make_symbol_from_cache(symCache);
                curves.interpolatedCurve->setSymbol(newSymbol.release());
            }

            // FIXME: legend still jumps around. legend position does not matter.
            // giving all curves a visible entry in the legend fixes it, maybe because of the scroll bars?
            // try printing legend and plot geometries. maybe there's a pattern there.

            // The raw curve, purely for showing symbols where sampleed values
            // are. The curve itself is not rendered.
            curves.rawCurve->setItemAttribute(QwtPlotItem::Legend, false);

            // The interpolated curves. Only the first curve for each channel
            // has a legend entry. The other curves show older data and are
            // plotted with a lower alpha value.
            curves.interpolatedCurve->setTitle(fmt::format("Channel {:2d}", chanIndex).c_str());
            curves.interpolatedCurve->setItemAttribute(QwtPlotItem::Legend, traceIndex == 0);

            newBoundingRect = newBoundingRect.united(curves.interpolatedCurve->boundingRect());
        }
    }

    waveforms::update_plot_axes(getPlot(), d->zoomer_, newBoundingRect, d->maxBoundingRect_);
    d->maxBoundingRect_ = newBoundingRect;

    getPlot()->updateLegend();
    histo_ui::PlotWidget::replot();

    std::ostringstream oss;
    d->makeStatusText(oss, d->traceDataUpdateTime_);

    getStatusBar()->clearMessage();
    getStatusBar()->showMessage(oss.str().c_str());

    if (gotNewData)
    {
        if (!d->updateLed_->isChecked())
        {
            d->updateLed_->setChecked(true);
            QTimer::singleShot(100, Qt::PreciseTimer, d->updateLed_, [this] () {
                d->updateLed_->setChecked(false);
            });
        }
    }

    if (xAxisShowsSamples)
        getPlot()->axisWidget(QwtPlot::xBottom)->setTitle("Sample");
    else
        getPlot()->axisWidget(QwtPlot::xBottom)->setTitle("Time [ns]");

    qDebug() << "leave WaveformSink1DWidget::replot()";
    spdlog::trace("end WaveformSink1DWidget::replot()");
}

void WaveformSink1DWidget::Private::makeInfoText(std::ostringstream &out)
{
    double totalMemory = mesytec::mvme::waveforms::get_used_memory(analysisTraceSnapshot_);
    totalMemory += mesytec::mvme::waveforms::get_used_memory(displayTraceData_);
    totalMemory += mesytec::mvme::waveforms::get_used_memory(tracesToPlot_);

    const size_t channelCount = displayTraceData_.size();
    size_t historyDepth = !displayTraceData_.empty() ? displayTraceData_[0].size() : 0u;
    auto selectedChannel = spin_chanSelect->value();

    const size_t totalTraces = std::accumulate(std::begin(displayTraceData_), std::end(displayTraceData_), 0u,
        [](size_t sum, const auto &traces) { return sum + traces.size(); });

    const size_t totalSamples = std::accumulate(std::begin(displayTraceData_), std::end(displayTraceData_), 0u,
        [](size_t sum, const auto &traces) {
            return sum + std::accumulate(std::begin(traces), std::end(traces), 0u,
                [](size_t sum, const auto &trace) { return sum + trace.size(); });
        });

    size_t chanMin = 0;
    size_t chanMax = channelCount;

    if (!cb_showAllChannels_->isChecked())
    {
        auto selectedChannel = spin_chanSelect->value();
        selectedChannel = std::clamp(selectedChannel, 0, static_cast<int>(channelCount) - 1);
        chanMin = selectedChannel;
        chanMax = selectedChannel + 1;
    }

    out << "Trace History:\n";
    out << fmt::format("  Total memory used: {:} B / {:.2f} MiB\n", totalMemory, static_cast<double>(totalMemory) / Megabytes(1));
    out << fmt::format("  Number of channels: {}\n", channelCount);
    out << fmt::format("  History depth: {}\n", historyDepth);
    out << fmt::format("  Total traces: {}\n", totalTraces);
    out << fmt::format("  Total samples: {}\n", totalSamples);
    out << fmt::format("  Selected channel: {}\n", cb_showAllChannels_->isChecked() ? "All" : std::to_string(selectedChannel));
    out << "\n";

    out << "Visible Traces:\n";

    // row wise: walk the channels
    for (size_t chan = chanMin; chan < chanMax; ++chan)
    {
        auto &handles = channelToWaveformHandles_[chan];
        size_t traceIndex = 0;

        for (auto &handle: handles)
        {
            auto curves = curveHelper_.getWaveform(handle);
            auto rawData = reinterpret_cast<waveforms::WaveformPlotData *>(curves.rawCurve->data());
            auto ipolData = reinterpret_cast<waveforms::WaveformPlotData *>(curves.interpolatedCurve->data());
            assert(rawData && ipolData);

            auto rawTrace = rawData->getTrace();
            auto ipolTrace = ipolData->getTrace();

            out << fmt::format("Channel{}, Trace#{}: {} samples, meta={}\n", chan, traceIndex, rawData->size(),
                mesytec::mvme::waveforms::trace_meta_to_string(rawTrace->meta));

            out << fmt::format("Channel{}, Trace#{} sample input ({} samples): ", chan, traceIndex, rawTrace->size());
            mesytec::mvme::waveforms::print_trace_compact(out, *rawTrace);

            out << fmt::format("Channel{}, Trace#{} interpolated ({} samples): ", chan, traceIndex, ipolTrace->size());
            mesytec::mvme::waveforms::print_trace_compact(out, *ipolTrace);

            ++traceIndex;
        }

        out << "\n";
    }
}

inline std::string format_age(s64 ms)
{
    if (ms < 1000)
        return fmt::format("{} ms", ms);
    else if (ms < 1000 * 60)
        return fmt::format("{:.1f} s", static_cast<double>(ms) / 1000.0);
    else if (ms < 1000 * 60 * 60)
        return fmt::format("{:.1f} min", static_cast<double>(ms) / (1000.0 * 60.0));
    else
        return fmt::format("{:.1f} h", static_cast<double>(ms) / (1000.0 * 60.0 * 60.0));
}

void WaveformSink1DWidget::Private::makeStatusText(std::ostringstream &out, const QTime &lastUpdate)
{
    auto selectedChannel = std::to_string(spin_chanSelect->value());

    if (cb_showAllChannels_->isChecked())
        selectedChannel = "All";

    // internally we have 2 traces per channel: the raw and the interpolated one
    auto visibleTraceCount = tracesToPlot_.size() * 0.5;
    size_t sampleCount = 0;

    for (auto &trace: tracesToPlot_)
    {
        if (!trace.empty())
        {
            sampleCount = trace.size();
            break;
        }
    }

    auto dt_ms = lastUpdate.msecsTo(QTime::currentTime());
    auto dt_str = lastUpdate.isValid() ? format_age(dt_ms) : "unknown";

    out << fmt::format("Channel: {}, Visible Traces: {}, Samples/Trace: {}, Data Age: {}",
         selectedChannel, visibleTraceCount, sampleCount, dt_str);

    if (actionHold_->isChecked())
        out << " (hold active)";
}

void WaveformSink1DWidget::Private::printInfo()
{
    if (!logView_ || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
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

void WaveformSink1DWidget::Private::exportPlotToPdf()
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

void WaveformSink1DWidget::Private::exportPlotToClipboard()
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
