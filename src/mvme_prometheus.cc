#include "mvme_prometheus.h"

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <spdlog/spdlog.h>

#include "stream_worker_base.h"
#include "vme_config.h"

namespace mesytec::mvme
{

static std::shared_ptr<PrometheusContext> theInstance;

void set_prometheus_instance(std::shared_ptr<PrometheusContext> prom)
{
    theInstance = prom;
    spdlog::info("prometheus context instance set to {}. &theInstance={}",
        fmt::ptr(theInstance.get()),
        fmt::ptr(&theInstance)
        );
}

std::shared_ptr<PrometheusContext> get_prometheus_instance()
{
    spdlog::info("returning prometheus context instance {}. &theInstance={}",
        fmt::ptr(theInstance.get()),
        fmt::ptr(&theInstance)
        );
    return theInstance;
}

PrometheusContext::PrometheusContext(const std::string &bind_address)
    : exposer_(bind_address)
    , registry_(std::make_shared<prometheus::Registry>())
{
    exposer_.RegisterCollectable(registry_);
}

struct StreamProcCountersPromExporter::Private
{
    struct Metrics
    {
        prometheus::Family<prometheus::Gauge> &bytes_processed_family_;
        prometheus::Gauge &bytes_processed_;

        prometheus::Family<prometheus::Gauge> &buffers_processed_family_;
        prometheus::Gauge &buffers_processed_;

        prometheus::Family<prometheus::Gauge> &events_processed_family_;
        prometheus::Gauge &events_processed_;

        prometheus::Family<prometheus::Gauge> &event_hits_family_;
        std::array<prometheus::Gauge *, MaxVMEEvents> event_hits_;

        using PrometheusModuleHits = std::array<prometheus::Gauge *, MaxVMEModules>;
        prometheus::Family<prometheus::Gauge> &module_hits_family_;
        std::array<PrometheusModuleHits, MaxVMEEvents> module_hits_;

        Metrics(prometheus::Registry &registry)
            : bytes_processed_family_(prometheus::BuildGauge()
                                        .Name("analysis_bytes_processed")
                                        .Help("Bytes processed by the mvme analysis")
                                        .Register(registry))
            , bytes_processed_(bytes_processed_family_.Add({}))

            , buffers_processed_family_(prometheus::BuildGauge()
                                        .Name("analysis_buffers_processed")
                                        .Help("Buffers processed by the mvme analysis")
                                        .Register(registry))
            , buffers_processed_(buffers_processed_family_.Add({}))

            , events_processed_family_(prometheus::BuildGauge()
                                        .Name("analysis_events_processed")
                                        .Help("Total events processed by the mvme analysis")
                                        .Register(registry))
            , events_processed_(events_processed_family_.Add({}))

            , event_hits_family_(prometheus::BuildGauge()
                                        .Name("analysis_event_hits")
                                        .Help("Events processed by the mvme analysis")
                                        .Register(registry))

            , module_hits_family_(prometheus::BuildGauge()
                                        .Name("analysis_module_hits")
                                        .Help("Per module processed events by the mvme analysis")
                                        .Register(registry))
        {
            std::fill(std::begin(event_hits_), std::end(event_hits_), nullptr);

            for (size_t i=0; i<module_hits_.size(); ++i)
                std::fill(std::begin(module_hits_[i]), std::end(module_hits_[i]), nullptr);
        }
    };

    Logger logger_;
    std::unique_ptr<Metrics> metrics_;

    Private()
    {
        if (auto prom = get_prometheus_instance())
        {
            if (auto registry = prom->registry())
            {
                metrics_ = std::make_unique<Metrics>(*registry);
            }
        }
    }

    void recreateMetrics(const VMEConfig *vmeConfig)
    {
        if (!metrics_) return;

        for (size_t i=0; i<metrics_->event_hits_.size(); ++i)
        {
            if (metrics_->event_hits_[i])
            {
                metrics_->event_hits_family_.Remove(metrics_->event_hits_[i]);
                metrics_->event_hits_[i] = nullptr;
            }
        }

        for (size_t i=0; i<metrics_->module_hits_.size(); ++i)
        {
            for (size_t j=0; j<metrics_->module_hits_[i].size(); ++j)
            {
                if (metrics_->module_hits_[i][j])
                {
                    metrics_->module_hits_family_.Remove(metrics_->module_hits_[i][j]);
                    metrics_->module_hits_[i][j] = nullptr;
                }
            }
        }

        auto eventConfigs = vmeConfig->getEventConfigs();

        for (size_t i=0; i<std::min(metrics_->event_hits_.size(), static_cast<size_t>(eventConfigs.size())); ++i)
        {
            metrics_->event_hits_[i] = &metrics_->event_hits_family_.Add({
                {"event_index", std::to_string(i)},
                {"event_name", eventConfigs[i]->objectName().toStdString()}
                });
        }

        for (size_t i=0; i<std::min(metrics_->module_hits_.size(), static_cast<size_t>(eventConfigs.size())); ++i)
        {
            auto moduleConfigs = eventConfigs[i]->getModuleConfigs();

            for (size_t j=0; j<std::min(metrics_->module_hits_[i].size(), static_cast<size_t>(moduleConfigs.size())); ++j)
            {
                metrics_->module_hits_[i][j] = &metrics_->module_hits_family_.Add({
                    {"event_index", std::to_string(i)},
                    {"event_name", eventConfigs[i]->objectName().toStdString()},
                    {"module_index", std::to_string(j)},
                    {"module_name", moduleConfigs[j]->objectName().toStdString()},
                    });
            }
        }
    }

    void update(const MVMEStreamProcessorCounters &counters)
    {
        if (!metrics_) return;

        metrics_->bytes_processed_.Set(counters.bytesProcessed);
        metrics_->buffers_processed_.Set(counters.buffersProcessed);
        metrics_->events_processed_.Set(counters.totalEvents);

        for (size_t i=0; i<metrics_->event_hits_.size(); ++i)
        {
            if (metrics_->event_hits_[i])
                metrics_->event_hits_[i]->Set(counters.eventCounters[i]);
        }

        for (size_t i=0; i<metrics_->event_hits_.size(); ++i)
        {
            for (size_t j=0; j<metrics_->event_hits_.size(); ++j)
            {
                if (metrics_->module_hits_[i][j])
                    metrics_->module_hits_[i][j]->Set(counters.moduleCounters[i][j]);
            }
        }
    }
};

StreamProcCountersPromExporter::StreamProcCountersPromExporter()
    : d(std::make_unique<Private>())
{
}

StreamProcCountersPromExporter::~StreamProcCountersPromExporter()
{
}

void StreamProcCountersPromExporter::setLogger(Logger logger)
{
    d->logger_ = logger;
}

StreamConsumerBase::Logger &StreamProcCountersPromExporter::getLogger()
{
    return d->logger_;
}

void StreamProcCountersPromExporter::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig, const analysis::Analysis *analysis)
{
    (void) runInfo;
    (void) analysis;

    d->recreateMetrics(vmeConfig);
}

void StreamProcCountersPromExporter::endRun(const DAQStats &stats, const std::exception *e)
{
    (void) stats;
    (void) e;

    if (auto streamWorker = qobject_cast<StreamWorkerBase *>(getWorker()))
    {
        d->update(streamWorker->getCounters());
    }
}

void StreamProcCountersPromExporter::processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer, size_t bufferSize)
{
    (void) bufferType;
    (void) bufferNumber;
    (void) buffer;
    (void) bufferSize;

    if (auto streamWorker = qobject_cast<StreamWorkerBase *>(getWorker()))
    {
        d->update(streamWorker->getCounters());
    }
}

}
