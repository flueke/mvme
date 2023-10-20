#include "mvme_prometheus.h"

#include <prometheus/counter.h>
#include <prometheus/gauge.h>

#include "stream_worker_base.h"

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
                                      .Help("Events processed by the mvme analysis")
                                      .Register(registry))
        , events_processed_(events_processed_family_.Add({}))
    {
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
    d->bytes_processed_.Set(0);
    d->buffers_processed_.Set(0);
    d->events_processed_.Set(0);
}

void StreamProcCountersPromExporter::endRun(const DAQStats &stats, const std::exception *e)
{
    if (auto streamWorker = getStreamWorker())
    {
        auto counters = streamWorker->getCounters();
        d->bytes_processed_.Set(counters.bytesProcessed);
        d->buffers_processed_.Set(counters.buffersProcessed);
        d->events_processed_.Set(counters.totalEvents);
    }
}

void StreamProcCountersPromExporter::processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer, size_t bufferSize)
{
    if (auto streamWorker = getStreamWorker())
    {
        auto counters = streamWorker->getCounters();
        d->bytes_processed_.Set(counters.bytesProcessed);
        d->buffers_processed_.Set(counters.buffersProcessed);
        d->events_processed_.Set(counters.totalEvents);
    }
}

}
