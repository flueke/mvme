#include "event_builder_monitor.hpp"

#include <QDockWidget>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QTimer>

#include "multiplot_widget.h"
#include "mvlc_stream_worker.h"
#include "qt_util.h"
#include "util/qt_monospace_textedit.h"

using namespace mesytec;

namespace analysis
{

struct EventBuilderMonitorWidget::Private
{
    static const int UpdateInterval = 500;
    AnalysisServiceProvider *asp_ = nullptr;
    QPlainTextEdit *countersTextEdit_ = nullptr;
    MultiPlotWidget *inputHistosWidget_ = nullptr;
    MultiPlotWidget *outputHistosWidget_ = nullptr;
    std::vector<Histo1DPtr> inputHistos_;
    std::vector<Histo1DPtr> outputHistos_;
    QTimer updateTimer_;

    void update();
    void updateCountersText(const mesytec::mvlc::event_builder2::BuilderCounters &counters);
    void updateHistos(const mesytec::mvlc::event_builder2::BuilderCounters &counters);
};

EventBuilderMonitorWidget::EventBuilderMonitorWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QMainWindow(parent)
    , d(std::make_unique<Private>())
{
    assert(asp);
    d->asp_ = asp;
    d->countersTextEdit_ = mvme::util::make_monospace_plain_textedit().release();
    d->inputHistosWidget_ = new MultiPlotWidget(asp);
    d->outputHistosWidget_ = new MultiPlotWidget(asp);

    auto dwInputHistos = new QDockWidget("Input dt histograms", this);
    auto dwOutputHistos = new QDockWidget("Output dt histograms", this);

    for (auto dw: {dwInputHistos, dwOutputHistos})
    {
        dw->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    }

    dwInputHistos->setWidget(d->inputHistosWidget_);
    dwOutputHistos->setWidget(d->outputHistosWidget_);

    auto gbCounters = new QGroupBox("Event Builder Counters");
    auto l_gbCounters = make_hbox(gbCounters);
    l_gbCounters->addWidget(d->countersTextEdit_);

    // this is fucked
    gbCounters->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    d->inputHistosWidget_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
    d->outputHistosWidget_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);

    setCentralWidget(gbCounters);
    addDockWidget(Qt::DockWidgetArea::RightDockWidgetArea, dwInputHistos);
    addDockWidget(Qt::DockWidgetArea::RightDockWidgetArea, dwOutputHistos);
    setWindowTitle("Event Builder Monitor");

    connect(&d->updateTimer_, &QTimer::timeout, this, [this] { d->update(); });
    d->updateTimer_.start(Private::UpdateInterval);
}

EventBuilderMonitorWidget::~EventBuilderMonitorWidget() = default;

void EventBuilderMonitorWidget::closeEvent(QCloseEvent *event)
{
    event->accept();
    emit aboutToClose();
}

void EventBuilderMonitorWidget::Private::update()
{
    if (auto w = qobject_cast<MVLC_StreamWorker *>(asp_->getMVMEStreamWorker()))
    {
        auto counters = w->getEventBuilder2Counters();
        updateCountersText(counters);
        updateHistos(counters);
    }
}

inline void
print_dt_histos(std::stringstream &oss,
                const std::vector<mesytec::mvlc::event_builder2::ModuleDeltaHisto> &dtHistos)
{
    for (const auto &dtHisto: dtHistos)
    {
        oss << fmt::format("  {}: counts={}, underflows={}, overflows={}\n", dtHisto.histo.title,
                           counts(dtHisto.histo), dtHisto.histo.underflows,
                           dtHisto.histo.overflows);
    }
}

void EventBuilderMonitorWidget::Private::updateCountersText(
    const mesytec::mvlc::event_builder2::BuilderCounters &counters)
{
    countersTextEdit_->clear();

    for (size_t eventIndex = 0; eventIndex < counters.eventCounters.size(); ++eventIndex)
    {
        auto &eventCounters = counters.eventCounters.at(eventIndex);
        if (std::any_of(std::begin(eventCounters.inputHits), std::end(eventCounters.inputHits),
                        [](auto count) { return count > 0; }))
        {
            std::stringstream oss;
            oss << mvlc::event_builder2::dump_counters(eventCounters) << "\n";

            countersTextEdit_->appendPlainText(
                fmt::format("'{}' module counters:", eventCounters.eventName).c_str());
            append_lines(oss, countersTextEdit_, 2);
        }
    }

    std::stringstream oss;
    for (size_t eventIndex = 0; eventIndex < counters.eventCounters.size(); ++eventIndex)
    {
        const auto &eventCounters = counters.eventCounters.at(eventIndex);

        if (eventCounters.dtInputHistos.empty())
            continue;

        oss << fmt::format("'{}' timestamp delta histograms (input side):", eventCounters.eventName)
            << "\n";
        print_dt_histos(oss, eventCounters.dtInputHistos);
        oss << "\n";

        if (eventCounters.dtOutputHistos.empty())
            continue;

        oss << fmt::format("'{}' timestamp delta histograms (output side):",
                           eventCounters.eventName)
            << "\n";
        print_dt_histos(oss, eventCounters.dtOutputHistos);
        oss << "\n";
    }

    append_lines(oss, countersTextEdit_);
}

inline void
update_histos(MultiPlotWidget *widget, std::vector<Histo1DPtr> &uiHistos,
              const std::vector<std::vector<mvlc::event_builder2::ModuleDeltaHisto>> &histos)
{
    const size_t histoCount =
        std::accumulate(std::begin(histos), std::end(histos), static_cast<size_t>(0),
                        [](auto sum, const auto &eventHistos) { return sum + eventHistos.size(); });

    uiHistos.resize(histoCount);
    size_t outHistoIndex = 0;

    // Create/update mvme Histo1D instances from the eb2 histograms.
    for (size_t eventIndex = 0; eventIndex < histos.size(); ++eventIndex)
    {
        const auto &eventHistos = histos.at(eventIndex);

        for (const auto &dtHisto: eventHistos)
        {
            assert(outHistoIndex < uiHistos.size());

            auto &outHisto = uiHistos[outHistoIndex++];

            if (!outHisto)
            {
                outHisto = std::make_shared<Histo1D>(dtHisto.histo.bins.size(),
                                                     dtHisto.histo.binning.minValue,
                                                     dtHisto.histo.binning.maxValue);
            }
            else
            {
                auto ebBinning = dtHisto.histo.binning;
                assert(ebBinning.binCount == dtHisto.histo.bins.size());
                AxisBinning binning(ebBinning.binCount, ebBinning.minValue, ebBinning.maxValue);
                outHisto->setAxisBinning(Qt::XAxis, binning);
                outHisto->resize(ebBinning.binCount);
            }

            outHisto->clear();
            outHisto->setObjectName(QSL("dt(%1, %2), %3")
                                        .arg(dtHisto.moduleIndexes.first)
                                        .arg(dtHisto.moduleIndexes.second)
                                        .arg(dtHisto.histo.title.c_str()));
            outHisto->setTitle(outHisto->objectName());

            for (size_t bin = 0; bin < dtHisto.histo.bins.size(); ++bin)
            {
                outHisto->setBinContent(bin, dtHisto.histo.bins[bin], dtHisto.histo.bins[bin]);
            }
            outHisto->setUnderflow(dtHisto.histo.underflows);
            outHisto->setOverflow(dtHisto.histo.overflows);
        }
    }

    // Add newly created histos, remove stale histos
    size_t widgetEntryCount = widget->getNumberOfEntries();

    if (widgetEntryCount > uiHistos.size())
    {
        widget->clear();
        widgetEntryCount = 0;
    }

    for (size_t i = widgetEntryCount; i < uiHistos.size(); ++i)
    {
        widget->addHisto1D(uiHistos[i]);
    }

    widget->replot();
}

void EventBuilderMonitorWidget::Private::updateHistos(
    const mesytec::mvlc::event_builder2::BuilderCounters &counters)
{
    update_histos(inputHistosWidget_, inputHistos_, counters.getInputDtHistograms());
    update_histos(outputHistosWidget_, outputHistos_, counters.getOutputDtHistograms());
}

} // namespace analysis
