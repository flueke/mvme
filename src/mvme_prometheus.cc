#include "mvme_prometheus.h"

#include <prometheus/counter.h>
#include <prometheus/gauge.h>

#include "stream_worker_base.h"
#include "vme_config.h"

namespace mesytec::mvme
{

static std::shared_ptr<PrometheusContext> theInstance;

void set_prometheus_instance(std::shared_ptr<PrometheusContext> prom)
{
    theInstance = prom;
}

std::shared_ptr<PrometheusContext> get_prometheus_instance()
{
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
    Logger logger_;
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

    Private(prometheus::Registry &registry)
        : bytes_processed_family_(prometheus::BuildGauge()
                                      .Name("bytes_processed")
                                      .Help("Bytes processed by the mvme analysis")
                                      .Register(registry))
        , bytes_processed_(bytes_processed_family_.Add({}))

        , buffers_processed_family_(prometheus::BuildGauge()
                                      .Name("buffers_processed")
                                      .Help("Buffers processed by the mvme analysis")
                                      .Register(registry))
        , buffers_processed_(buffers_processed_family_.Add({}))

        , events_processed_family_(prometheus::BuildGauge()
                                      .Name("events_processed")
                                      .Help("Total events processed by the mvme analysis")
                                      .Register(registry))
        , events_processed_(events_processed_family_.Add({}))

        , event_hits_family_(prometheus::BuildGauge()
                                      .Name("events_hits")
                                      .Help("Events processed by the mvme analysis")
                                      .Register(registry))

        , module_hits_family_(prometheus::BuildGauge()
                                      .Name("module_hits")
                                      .Help("Per module processed events by the mvme analysis")
                                      .Register(registry))
    {
        std::fill(std::begin(event_hits_), std::end(event_hits_), nullptr);

        for (size_t i=0; i<module_hits_.size(); ++i)
            std::fill(std::begin(module_hits_[i]), std::end(module_hits_[i]), nullptr);
    }

    void recreateMetrics(const VMEConfig *vmeConfig)
    {
        for (size_t i=0; i<event_hits_.size(); ++i)
        {
            if (event_hits_[i])
            {
                event_hits_family_.Remove(event_hits_[i]);
                event_hits_[i] = nullptr;
            }
        }

        for (size_t i=0; i<module_hits_.size(); ++i)
        {
            for (size_t j=0; j<module_hits_[i].size(); ++j)
            {
                if (module_hits_[i][j])
                {
                    module_hits_family_.Remove(module_hits_[i][j]);
                    module_hits_[i][j] = nullptr;
                }
            }
        }

        auto eventConfigs = vmeConfig->getEventConfigs();

        for (size_t i=0; i<std::min(event_hits_.size(), static_cast<size_t>(eventConfigs.size())); ++i)
        {
            event_hits_[i] = &event_hits_family_.Add({
                {"event_index", std::to_string(i)},
                {"event_name", eventConfigs[i]->objectName().toStdString()}
                });
        }

        for (size_t i=0; i<std::min(module_hits_.size(), static_cast<size_t>(eventConfigs.size())); ++i)
        {
            auto moduleConfigs = eventConfigs[i]->getModuleConfigs();

            for (size_t j=0; j<std::min(module_hits_[i].size(), static_cast<size_t>(moduleConfigs.size())); ++j)
            {
                module_hits_[i][j] = &module_hits_family_.Add({
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
        bytes_processed_.Set(counters.bytesProcessed);
        buffers_processed_.Set(counters.buffersProcessed);
        events_processed_.Set(counters.totalEvents);

        for (size_t i=0; i<event_hits_.size(); ++i)
        {
            if (event_hits_[i])
                event_hits_[i]->Set(counters.eventCounters[i]);
        }

        for (size_t i=0; i<event_hits_.size(); ++i)
        {
            for (size_t j=0; j<event_hits_.size(); ++j)
            {
                if (module_hits_[i][j])
                    module_hits_[i][j]->Set(counters.moduleCounters[i][j]);
            }
        }
    }
};

StreamProcCountersPromExporter::StreamProcCountersPromExporter()
    : d(std::make_unique<Private>(*get_prometheus_instance()->registry()))
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
    #if 0
    // TODO: not sure if the counters should be reset or not. For monitoring the
    // rate seems to be the more interesting metric anyways.
    d->bytes_processed_.Set(0);
    d->buffers_processed_.Set(0);
    d->events_processed_.Set(0);
    #endif
    d->recreateMetrics(vmeConfig);
}

void StreamProcCountersPromExporter::endRun(const DAQStats &stats, const std::exception *e)
{
    if (auto streamWorker = getStreamWorker())
    {
        d->update(streamWorker->getCounters());
    }
}

void StreamProcCountersPromExporter::processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer, size_t bufferSize)
{
    if (auto streamWorker = getStreamWorker())
    {
        d->update(streamWorker->getCounters());
    }
}

}
