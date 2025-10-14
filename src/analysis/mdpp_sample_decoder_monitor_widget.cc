#include "mdpp_sample_decoder_monitor_widget.h"

#include <QtConcurrent>
#include <QTimer>
#include <mesytec-mvlc/util/stopwatch.h>

#include "analysis/analysis.h"
#include "util/qt_font.h"
#include "ui_mdpp_sample_decoder_monitor_widget.h"

using namespace mesytec::mvme;

namespace analysis::ui
{

namespace
{

struct DecoderDebugResult
{
    bool errorOrWarningSeen = false;
    QString logText;
    QString inputText;
    QString outputText;
};

DecoderDebugResult run_decoder_collect_debug_info(const mesytec::mvme::mdpp_sampling::DecodedMdppSampleEvent &decodedByAnalysis)
{
    mesytec::mvlc::util::Stopwatch sw;
    DecoderDebugResult result;
    QTextStream logStream(&result.logText);
    QTextStream inputStream(&result.inputText);
    QTextStream outputStream(&result.outputText);

    auto logger = [&] (const std::string &level, const std::string &message)
    {
        logStream << fmt::format("[{}] {}\n", level, message).c_str();

        if (level == "warn" || level == "error")
        {
            result.errorOrWarningSeen = true;
        }
    };

    const auto &inputData = decodedByAnalysis.inputData;

    if (decodedByAnalysis.moduleType.empty())
    {
        logStream << "module_type not set in analysis data. DAQ not running?\n";
        return result;
    }

    // Rerun the decoder, redirecting log output and checking for errors and warnings.
    auto decoded = mdpp_sampling::decode_mdpp_samples(
        inputData.data(), inputData.size(), decodedByAnalysis.moduleType.c_str(), logger);

    for (size_t i=0; i<inputData.size(); ++i)
    {
        inputStream << fmt::format("{:3d}: {:#010x}\n", i, inputData[i]).c_str();
    }

    std::ostringstream oss;
    oss << fmt::format("module_type={}, input size={}\n", decoded.moduleType, inputData.size());
    oss << fmt::format("module_header={:#010x}, moduleId={:04x}, timestamp={}\n",
        decoded.header, decoded.headerModuleId, decoded.timestamp);
    oss << fmt::format("#traces={}\n", decoded.traces.size());

    if (!decoded.traces.empty())
    {
        oss << "\n";
        const auto &traces = decoded.traces;

        for (size_t traceIndex=0; traceIndex<static_cast<size_t>(traces.size()); ++traceIndex)
        {
            auto thDebug = traces[traceIndex].traceHeader.parts.debug;
            auto thConfig = traces[traceIndex].traceHeader.parts.config;
            auto thPhase = traces[traceIndex].traceHeader.parts.phase;
            auto thLength = traces[traceIndex].traceHeader.parts.length;

            oss << "trace #" << traceIndex << ":\n";

            oss <<  fmt::format("  channel={}\n", traces[traceIndex].channel);

            oss <<  fmt::format("  channel={}, #samples={}, trace header={:#010x}, .debug={}, .config={}, .phase={}, .length={}\n",
                traces[traceIndex].channel, traces[traceIndex].samples.size(),
                traces[traceIndex].traceHeader.value, thDebug, thConfig, thPhase, thLength);

            oss <<  fmt::format("  channel={}, samples: {}\n",
                traces[traceIndex].channel, fmt::join(traces[traceIndex].samples, ", "));
        }
    }

    outputStream << oss.str().c_str();

    qDebug() << __PRETTY_FUNCTION__ << "done, took" << sw.interval().count() / 1000.0 << "ms";

    return result;
}

}

struct MdppSampleDecoderMonitorWidget::Private
{
    MdppSampleDecoderMonitorWidget *q;
    std::unique_ptr<Ui::MdppSampleDecoderMonitorWidget> ui;
    std::shared_ptr<analysis::DataSourceMdppSampleDecoder> source_;
    AnalysisServiceProvider *asp_;
    QTimer refreshTimer_;
    QFutureWatcher<DecoderDebugResult> debugResultWatcher_;

    void startRefresh();
    void onRefreshDone();
};

MdppSampleDecoderMonitorWidget::MdppSampleDecoderMonitorWidget(
    const std::shared_ptr<analysis::DataSourceMdppSampleDecoder> &source,
    AnalysisServiceProvider *serviceProvider,
    QWidget *parent)
: QMainWindow(parent)
, d(std::make_unique<Private>())
{
    d->q = this;
    d->ui = std::make_unique<Ui::MdppSampleDecoderMonitorWidget>();
    d->ui->setupUi(this);
    d->source_ = source;
    d->asp_ = serviceProvider;
    d->refreshTimer_.setSingleShot(true);

    for (auto tb: { d->ui->tb_input, d->ui->tb_output, d->ui->tb_log })
    {
        tb->setReadOnly(true);
        tb->setFont(make_monospace_font());
    }

    connect(d->ui->pb_pauseResumeRefresh, &QPushButton::clicked, this, [this] (bool checked)
    {
        if (checked)
        {
            d->ui->pb_pauseResumeRefresh->setText("Resume");
        }
        else
        {
            d->ui->pb_pauseResumeRefresh->setText("Pause");
            d->refreshTimer_.start();
        }
    });

    connect(d->ui->pb_refreshNow, &QPushButton::clicked, this, [this] { d->startRefresh(); });

    connect(&d->refreshTimer_, &QTimer::timeout, this, [this]
    {
        if (!d->ui->pb_pauseResumeRefresh->isChecked())
            d->startRefresh();
    });

    const auto refreshIntervals = { 0, 100, 500, 1000 };

    for (const auto &interval: refreshIntervals)
    {
        d->ui->combo_refreshInterval->addItem(
            interval == 0 ? "off" : QString::number(interval) + " ms",
            interval);
    }

    connect(d->ui->combo_refreshInterval, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] (int index)
    {
        const auto interval = d->ui->combo_refreshInterval->itemData(index).toInt();
        d->refreshTimer_.setInterval(interval);
        if (interval == 0)
            d->refreshTimer_.stop();
        else
            d->refreshTimer_.start();
    });

    d->ui->combo_refreshInterval->setCurrentIndex(1);

    connect(&d->debugResultWatcher_, &QFutureWatcher<DecoderDebugResult>::finished,
        this, [this] { d->onRefreshDone(); });
}

MdppSampleDecoderMonitorWidget::~MdppSampleDecoderMonitorWidget()
{
    d->refreshTimer_.stop();
    d->debugResultWatcher_.waitForFinished();
}

void MdppSampleDecoderMonitorWidget::Private::startRefresh()
{
    if (debugResultWatcher_.isRunning())
    {
        qDebug() << __PRETTY_FUNCTION__ << "early return, previous refresh still in progress";
        return;
    }

    auto future = QtConcurrent::run(run_decoder_collect_debug_info, source_->getDecodedEvent());
    debugResultWatcher_.setFuture(future);
}

void MdppSampleDecoderMonitorWidget::Private::onRefreshDone()
{
    mesytec::mvlc::util::Stopwatch sw;
    auto result = debugResultWatcher_.result();

    if (ui->tb_input->toPlainText() != result.inputText)
        ui->tb_input->setText(result.inputText);

    if (ui->tb_output->toPlainText() != result.outputText)
        ui->tb_output->setText(result.outputText);

    if (ui->tb_log->toPlainText() != result.logText)
        ui->tb_log->setText(result.logText);

    if (result.errorOrWarningSeen && ui->cb_holdRefreshOnError->isChecked())
    {
        ui->pb_pauseResumeRefresh->setChecked(true);
    }

    // pull the current selected refresh interval from the ui and kick of the timer again.
    const auto interval = ui->combo_refreshInterval->currentData().toInt();
    refreshTimer_.setInterval(interval);
    refreshTimer_.setSingleShot(true);
    if (interval == 0)
        refreshTimer_.stop();
    else
        refreshTimer_.start();

    qDebug() << __PRETTY_FUNCTION__ << "done, took" << sw.interval().count() / 1000.0 << "ms";
}

}
