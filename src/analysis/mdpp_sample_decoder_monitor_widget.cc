#include "mdpp_sample_decoder_monitor_widget.h"

#include <QTimer>

#include "analysis/analysis.h"
#include "util/qt_font.h"
#include "ui_mdpp_sample_decoder_monitor_widget.h"

using namespace mesytec::mvme;

namespace analysis::ui
{

struct MdppSampleDecoderMonitorWidget::Private
{
    MdppSampleDecoderMonitorWidget *q;
    std::unique_ptr<Ui::MdppSampleDecoderMonitorWidget> ui;
    std::shared_ptr<analysis::DataSourceMdppSampleDecoder> source_;
    AnalysisServiceProvider *asp_;
    QTimer refreshTimer_;

    void refresh();
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

    for (auto tb: { d->ui->tb_input, d->ui->tb_output, d->ui->tb_log })
    {
        tb->setReadOnly(true);
        tb->setFont(make_monospace_font());
    }

    connect(d->ui->pb_pauseResumeRefresh, &QPushButton::clicked, this, [this]
    {
        if (d->ui->pb_pauseResumeRefresh->isChecked())
            d->ui->pb_pauseResumeRefresh->setText("Resume");
        else
            d->ui->pb_pauseResumeRefresh->setText("Pause");
    });

    connect(d->ui->pb_refreshNow, &QPushButton::clicked, this, [this] { d->refresh(); });

    connect(&d->refreshTimer_, &QTimer::timeout, this, [this]
    {
        if (!d->ui->pb_pauseResumeRefresh->isChecked())
            d->refresh();
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
}

MdppSampleDecoderMonitorWidget::~MdppSampleDecoderMonitorWidget() = default;

void MdppSampleDecoderMonitorWidget::Private::refresh()
{
    bool errorOrWarningSeen = false;

    auto logger = [this, &errorOrWarningSeen] (const std::string &level, const std::string &message)
    {
        ui->tb_log->append(fmt::format("[{}] {}", level, message).c_str());

        if (level == "warn" || level == "error")
        {
            errorOrWarningSeen = true;
        }
    };

    ui->tb_input->clear();
    ui->tb_output->clear();
    ui->tb_log->clear();

    const auto decodedByAnalysis = source_->getDecodedEvent();
    const auto &inputData = decodedByAnalysis.inputData;

    if (decodedByAnalysis.moduleType.empty())
    {
        ui->tb_output->append("module_type not set in analysis data. DAQ not running?");
        return;
    }

    // Rerun the decoder, redirecting log output and checking for errors and warnings.
    auto decoded = mdpp_sampling::decode_mdpp_samples(
        inputData.data(), inputData.size(), decodedByAnalysis.moduleType.c_str(), logger);

    for (size_t i=0; i<inputData.size(); ++i)
    {
        ui->tb_input->append(fmt::format("{:3d}: {:#010x}", i, inputData[i]).c_str());
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

    ui->tb_output->append(oss.str().c_str());

    if (errorOrWarningSeen && ui->cb_holdRefreshOnError->isChecked())
    {
        ui->pb_pauseResumeRefresh->setChecked(true);
    }
}

}
