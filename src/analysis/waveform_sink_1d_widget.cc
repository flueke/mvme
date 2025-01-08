#include "analysis/waveform_sink_1d_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QMenu>
#include <QStack>
#include <QTimer>
#include <qwt_legend.h>

#include <mesytec-mvlc/util/stopwatch.h>

#include "analysis_service_provider.h"
#include "analysis/waveform_sink_widget_common.h"
#include "mdpp-sampling/waveform_plotting.h"
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
    result->addItem("Latest Data", RefreshMode_LatestData);
    result->addItem("Event Snapshot", RefreshMode_EventSnapshot);
    auto boxstruct = make_vbox_container("Refresh Mode", result, 0, -2);
    toolbar->addWidget(boxstruct.container.release());
    return result;
}

inline QSpinBox *add_trace_selector(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setMinimum(0);
    result->setMaximum(1);
    result->setSpecialValueText("latest");
    auto boxStruct = make_vbox_container(QSL("Trace#"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
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

struct WaveformSink1DWidget::Private
{
    WaveformSink1DWidget *q = nullptr;
    std::shared_ptr<analysis::WaveformSink> sink_;
    AnalysisServiceProvider *asp_ = nullptr;
    waveforms::WaveformPlotCurveHelper curveHelper_;
    std::vector<waveforms::WaveformPlotCurveHelper::Handle> waveformHandles_;

    waveforms::TraceHistories analysisTraceData_;

    // Post processed trace data. One history buffer per channel in the source
    // sink. A new trace is prepended to each history buffer every ReplotInterval_ms.
    waveforms::TraceHistories rawDisplayTraces_;            // x-scaling only, no interpolation
    waveforms::TraceHistories interpolatedDisplayTraces_;   // x-scaling and interpolation

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
    QSpinBox *spin_interpolationFactor_ = nullptr;
    QComboBox *combo_phaseCorrection_ = nullptr;
    // set both the max number of traces to keep per channel and the number of traces to show in the plot at the same time.
    QSpinBox *spin_maxDepth_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;

    WidgetGeometrySaver *geoSaver_ = nullptr;
    QRectF maxBoundingRect_;
    QPushButton *pb_resetBoundingRect = nullptr;

    bool selectedTraceChanged_ = false;
    bool selectedChannelChanged_ = false;
    bool dtSampleChanged_ = false;
    bool interpolationFactorChanged_ = false;

    bool updateDataFromAnalysis(); // Returns true if new data was copied, false if the data was unchanged.
    void postProcessData();
    void reprocessData();
    void updateUi();
    void resetPlotAxes();
    void makeInfoText(std::ostringstream &oss);
    void makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame);
    void printInfo();
    void exportPlotToPdf();
    void exportPlotToClipboard();
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

    auto legend = new QwtLegend;
    getPlot()->insertLegend(legend, QwtPlot::RightLegend);


    auto tb = getToolBar();
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    set_widget_font_pointsize(tb, 7);

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

    d->zoomer_ = histo_ui::install_scrollzoomer(this);
    tb->addAction(QIcon(":/selection-resize.png"), QSL("Rescale Axes"), this, [this] {
        d->resetPlotAxes();
        replot();
    });

    histo_ui::setup_axis_scale_changer(this, QwtPlot::yLeft, "Y-Scale");

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
    d->spin_dtSample_ = add_dt_sample_setter(tb);

    tb->addSeparator();
    d->spin_interpolationFactor_ = add_interpolation_factor_setter(tb);

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

    connect(d->spin_dtSample_, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, [this] {
        d->dtSampleChanged_ = true;
        // Shrink the grid back to the minimum size when dtSample changes.
        // Nice-to-have when decreasing dtSample, not much impact when
        // increasing it. Note: this forces a QwtPlot::replot() internally.
        d->resetPlotAxes();
    });

    connect(d->spin_interpolationFactor_, qOverload<int>(&QSpinBox::valueChanged),
        this, [this] { d->interpolationFactorChanged_ = true; replot(); });

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

    if (newAnalysisTraceData != analysisTraceData_)
    {
        std::swap(analysisTraceData_, newAnalysisTraceData);
        return true;
    }

    return false;
}

void WaveformSink1DWidget::Private::postProcessData()
{
    spdlog::trace("WaveformSink1DWidget::Private::postProcessData()");

    const auto dtSample = spin_dtSample_->value();
    const auto interpolationFactor = 1 + spin_interpolationFactor_->value();
    const size_t maxDepth = spin_maxDepth_->value();
    auto phaseCorrection = static_cast<waveforms::PhaseCorrectionMode>(
        combo_phaseCorrection_->currentData().toInt());
    const bool selectedTraceIndex = traceSelect_->value();

    // Note: this potentially removes Traces still referenced by underlying
    // QwtPlotCurves. Have to update/delete superfluous curves before calling
    // replot()!

    // TODO: clear things on mode change

    switch (refreshMode_)
    {
    case RefreshMode_LatestData:
        spdlog::debug("WaveformSink1DWidget::Private::postProcessData(): post_process_waveforms()");
        waveforms::post_process_waveforms(
            analysisTraceData_,
            rawDisplayTraces_,
            interpolatedDisplayTraces_,
            dtSample, interpolationFactor,
            maxDepth, phaseCorrection);
        break;

    case RefreshMode_EventSnapshot:
        spdlog::debug("WaveformSink1DWidget::Private::postProcessData(): post_process_waveform_snapshot()");
        waveforms::post_process_waveform_snapshot(
            analysisTraceData_,
            rawDisplayTraces_,
            interpolatedDisplayTraces_,
            dtSample, interpolationFactor,
            selectedTraceIndex, maxDepth,
            phaseCorrection);
        break;
    }
}

void WaveformSink1DWidget::Private::reprocessData()
{
    spdlog::trace("WaveformSink1DWidget::Private::reprocessData()");

    const auto dtSample = spin_dtSample_->value();
    const auto interpolationFactor = 1 + spin_interpolationFactor_->value();
    const size_t maxDepth = spin_maxDepth_->value();
    auto phaseCorrection = static_cast<waveforms::PhaseCorrectionMode>(
        combo_phaseCorrection_->currentData().toInt());
    const bool selectedTraceIndex = traceSelect_->value();

    switch (refreshMode_)
    {
    case RefreshMode_LatestData:
        waveforms::reprocess_waveforms(
            rawDisplayTraces_,
            interpolatedDisplayTraces_,
            dtSample, interpolationFactor,
            phaseCorrection);
        break;

    case RefreshMode_EventSnapshot:
        waveforms::post_process_waveform_snapshot(
            analysisTraceData_,
            rawDisplayTraces_,
            interpolatedDisplayTraces_,
            dtSample, interpolationFactor,
            selectedTraceIndex, maxDepth,
            phaseCorrection);
        break;
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
    const auto maxChannel = static_cast<signed>(rawDisplayTraces_.size()) - 1;
    spin_chanSelect->setMaximum(std::max(0, maxChannel));

    // selector 2: Update the trace number spinbox
    size_t maxTrace = 0;

    if (!analysisTraceData_.empty())
        maxTrace = analysisTraceData_.front().size() - 1;

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

    const bool refreshModeChanged = d->prevRefreshMode_ != d->refreshMode_;
    const bool holdEnabled = d->actionHold_->isChecked();
    const bool showAllChannels = d->cb_showAllChannels_->isChecked();

    const bool forceProcessing = (
        d->selectedChannelChanged_
        || d->dtSampleChanged_
        || d->interpolationFactorChanged_
        || d->selectedTraceChanged_
        || refreshModeChanged);

    spdlog::debug("WaveformSink1DWidget::replot(): forceProcessing={}, refreshModeChanged={}, selectedChannelChanged_={}, dtSampleChanged_={}, interpolationFactorChanged_={}, selectedTraceChanged_={}",
        forceProcessing, refreshModeChanged, d->selectedChannelChanged_, d->dtSampleChanged_, d->interpolationFactorChanged_, d->selectedTraceChanged_);
    spdlog::debug("WaveformSink1DWidget::replot(): refreshMode_={}, holdEnabled={}, showAllChannels={}", static_cast<int>(d->refreshMode_), holdEnabled, showAllChannels);

    if (refreshModeChanged)
    {
        spdlog::debug("WaveformSink1DWidget::replot(): refreshMode changed from {} to {}, clearing trace data",
            d->prevRefreshMode_, d->refreshMode_);
        d->analysisTraceData_.clear();
    }

    d->selectedChannelChanged_ = d->dtSampleChanged_ = d->interpolationFactorChanged_ = d->selectedTraceChanged_ = false;
    d->prevRefreshMode_ = d->refreshMode_;

    if (d->actionHold_->isChecked() && forceProcessing)
    {
        spdlog::debug("WaveformSink1DWidget::replot(): hold enabled, forceProcessing is true -> reprocessData()");
        d->reprocessData();
    }
    else if (!d->actionHold_->isChecked())
    {
        const bool updated = d->updateDataFromAnalysis();

        if (updated)
        {
            spdlog::debug("WaveformSink1DWidget::replot(): new data from analysis -> postProcessData()");
            d->postProcessData();
        }
        else if (forceProcessing)
        {
            spdlog::debug("WaveformSink1DWidget::replot(): no new analysis but forceProcessing is true -> reprocessData()");
            d->reprocessData();
        }
        else
        {
            spdlog::debug("WaveformSink1DWidget::replot(): no new analysis data, no forceProcessing -> return");
        }
    }

    d->updateUi(); // update, selection boxes, buttons, etc.

    assert(d->waveformHandles_.size() == d->curveHelper_.size());

    static const auto colors = make_plot_colors();
    const auto channelCount = d->rawDisplayTraces_.size();
    QRectF newBoundingRect = d->maxBoundingRect_;
    size_t chanMin = 0;
    size_t chanMax = channelCount;

    if (!showAllChannels)
    {
        auto selectedChannel = d->spin_chanSelect->value();
        selectedChannel = std::clamp(selectedChannel, 0, static_cast<int>(channelCount) - 1);
        chanMin = selectedChannel;
        chanMax = selectedChannel + 1;
    }

    spdlog::debug("WaveformSink1DWidget::replot(): channelCount={}, chanMin={}, chanMax={}", channelCount, chanMin, chanMax);

    size_t totalTraceEntriesNeeded = 0;

    for (size_t chanIndex = chanMin; chanIndex < chanMax; ++chanIndex)
    {
        auto &ipolTraces = d->interpolatedDisplayTraces_.at(chanIndex);
        const size_t traceCount = (d->refreshMode_ == RefreshMode_LatestData
            ? ipolTraces.size()
            : std::min(static_cast<int>(ipolTraces.size()), d->spin_maxDepth_->value()));
        totalTraceEntriesNeeded += traceCount;
    }

    spdlog::debug("WaveformSink1DWidget::replot(): totalTraceEntriesNeeded={}", totalTraceEntriesNeeded);

    assert(d->waveformHandles_.size() == d->curveHelper_.size());

    while (d->curveHelper_.size() < totalTraceEntriesNeeded)
    {
        assert(d->waveformHandles_.size() == d->curveHelper_.size());
        auto cd = make_curves_with_data();
        d->waveformHandles_.push_back(d->curveHelper_.addWaveform(std::move(cd.curves)));
        assert(d->waveformHandles_.size() == d->curveHelper_.size());
    }

    // Clean up old, now unused traces. Important because these can point to
    // trace data that no longer exists.
    while (d->curveHelper_.size() > totalTraceEntriesNeeded)
    {
        assert(d->waveformHandles_.size() == d->curveHelper_.size());
        d->curveHelper_.takeWaveform(d->waveformHandles_.back());
        d->waveformHandles_.pop_back();
        assert(d->waveformHandles_.size() == d->curveHelper_.size());
    }

    assert(d->curveHelper_.size() == totalTraceEntriesNeeded);
    assert(d->waveformHandles_.size() == d->curveHelper_.size());

    size_t absTraceIndex = 0;

    for (size_t chanIndex = chanMin; chanIndex < chanMax; ++chanIndex)
    {
        auto &rawTraces = d->rawDisplayTraces_[chanIndex];
        auto &ipolTraces = d->interpolatedDisplayTraces_[chanIndex];
        assert(rawTraces.size() == ipolTraces.size());

        const auto traceColor = colors.value((chanIndex - chanMin) % colors.size());

        size_t traceIndex = (d->refreshMode_ == RefreshMode_LatestData
            ? 0
            : std::min(ipolTraces.size(), static_cast<size_t>(d->traceSelect_->value())));

        const size_t traceCount = (d->refreshMode_ == RefreshMode_LatestData
            ? ipolTraces.size()
            : std::min(static_cast<int>(ipolTraces.size()), d->spin_maxDepth_->value()));

        const double slope = (1.0 - 0.1) / traceCount; // want alpha from 1.0 to 0.1

        for (;traceIndex < traceCount; ++traceIndex)
        {
            auto &rawTrace = rawTraces[traceIndex];
            auto &ipolTrace = ipolTraces[traceIndex];

            auto curvesHandle = d->waveformHandles_[absTraceIndex];
            auto curves = d->curveHelper_.getWaveform(curvesHandle);
            assert(curves.rawCurve && curves.interpolatedCurve);
            auto rawData = reinterpret_cast<waveforms::WaveformPlotData *>(curves.rawCurve->data());
            auto ipolData = reinterpret_cast<waveforms::WaveformPlotData *>(curves.interpolatedCurve->data());
            assert(rawData && ipolData);
            rawData->setTrace(&rawTrace);
            ipolData->setTrace(&ipolTrace);

            d->curveHelper_.setRawSymbolsVisible(curvesHandle, d->showSampleSymbols_);
            d->curveHelper_.setInterpolatedSymbolsVisible(curvesHandle, d->showInterpolatedSymbols_);

            double alpha = std::min(0.1 + slope * (traceCount - traceIndex), 1.0);
            auto thisColor = traceColor;
            thisColor.setAlphaF(alpha);
            set_curve_color(curves.rawCurve, thisColor);
            set_curve_color(curves.interpolatedCurve, thisColor);

            if (d->showSampleSymbols_)
            {
                auto symCache = d->curveHelper_.getRawSymbolCache(curvesHandle);
                mvme_qwt::set_symbol_cache_alpha(symCache, alpha);
                auto newSymbol = mvme_qwt::make_symbol_from_cache(symCache);
                curves.rawCurve->setSymbol(newSymbol.release());
            }

            if (d->showInterpolatedSymbols_)
            {
                auto symCache = d->curveHelper_.getInterpolatedSymbolCache(curvesHandle);
                mvme_qwt::set_symbol_cache_alpha(symCache, alpha);
                auto newSymbol = mvme_qwt::make_symbol_from_cache(symCache);
                curves.interpolatedCurve->setSymbol(newSymbol.release());
            }

            curves.rawCurve->setItemAttribute(QwtPlotItem::Legend, false);
            curves.interpolatedCurve->setItemAttribute(QwtPlotItem::Legend, traceIndex == 0);
            curves.interpolatedCurve->setTitle(fmt::format("Channel {}", chanIndex).c_str());

            ++absTraceIndex;

            newBoundingRect = newBoundingRect.united(ipolData->boundingRect());
        }
    }

    assert(d->waveformHandles_.size() == d->curveHelper_.size());

    waveforms::update_plot_axes(getPlot(), d->zoomer_, newBoundingRect, d->maxBoundingRect_);
    d->maxBoundingRect_ = newBoundingRect;

    getPlot()->updateLegend();
    histo_ui::PlotWidget::replot();

    std::ostringstream oss;
    d->makeStatusText(oss, d->frameTimer_.interval());

    getStatusBar()->clearMessage();
    getStatusBar()->showMessage(oss.str().c_str());

    spdlog::trace("end WaveformSink1DWidget::replot()");
}

void WaveformSink1DWidget::Private::makeInfoText(std::ostringstream &out)
{
    const auto &rawTraces = rawDisplayTraces_;
    const auto &ipolTraces = interpolatedDisplayTraces_;

    double totalMemory = mesytec::mvme::waveforms::get_used_memory(rawTraces);
    const size_t channelCount = rawTraces.size();
    size_t historyDepth = !rawTraces.empty() ? rawTraces[0].size() : 0u;
    auto selectedChannel = spin_chanSelect->value();

    const size_t totalTraces = std::accumulate(std::begin(rawTraces), std::end(rawTraces), 0u,
        [](size_t sum, const auto &traces) { return sum + traces.size(); });

    const size_t totalSamples = std::accumulate(std::begin(rawTraces), std::end(rawTraces), 0u,
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

    out << "Trace History Info:\n";
    out << fmt::format("  Total memory used: {:} B / {:.2f} MiB\n", totalMemory, static_cast<double>(totalMemory) / Megabytes(1));
    out << fmt::format("  Number of channels: {}\n", channelCount);
    out << fmt::format("  History depth: {}\n", historyDepth);
    out << fmt::format("  Total traces: {}\n", totalTraces);
    out << fmt::format("  Total samples: {}\n", totalSamples);
    out << fmt::format("  Selected channel: {}\n", cb_showAllChannels_->isChecked() ? "All" : std::to_string(selectedChannel));
    out << "\n";

    // row wise: walk the channels
    for (size_t chan = chanMin; chan < chanMax; ++chan)
    {
        auto &rawChanTraces = rawTraces[chan];
        auto &ipolChanTraces = ipolTraces[chan];
        const size_t traceIndexMax = std::min(rawChanTraces.size(), ipolChanTraces.size());

        // column wise, walk backwards in time through the traces
        for (size_t traceIndex = 0; traceIndex < traceIndexMax; ++traceIndex)
        {
            auto &rawTrace = rawChanTraces[traceIndex];
            auto &ipolTrace = ipolChanTraces[traceIndex];

            out << fmt::format("Channel{}, Trace#{} meta: {}\n", chan, traceIndex, mesytec::mvme::waveforms::trace_meta_to_string(rawTrace.meta));
            out << fmt::format("Channel{}, Trace#{} sample input ({} samples): ", chan, traceIndex, rawTrace.size());
            mesytec::mvme::waveforms::print_trace_compact(out, rawTrace);
            out << fmt::format("Channel{}, Trace#{} interpolated ({} samples): ", chan, traceIndex, ipolTrace.size());
            mesytec::mvme::waveforms::print_trace_compact(out, ipolTrace);
        }

        out << "\n";
    }
}

void WaveformSink1DWidget::Private::makeStatusText(std::ostringstream &out, const std::chrono::duration<double, std::milli> &dtFrame)
{
    auto selectedChannel = spin_chanSelect->value();
    auto visibleTraceCount = waveformHandles_.size();

    out << fmt::format("Channel: {}, #Traces: {}", selectedChannel, visibleTraceCount);
    out << fmt::format(", Frame time: {} ms", static_cast<int>(dtFrame.count()));
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
