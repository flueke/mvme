#ifndef E37CF686_6939_4588_98BC_2E8192089B98
#define E37CF686_6939_4588_98BC_2E8192089B98

#ifdef MVME_ENABLE_PROMETHEUS

#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include "libmvme_export.h"
#include "stream_processor_consumers.h"

namespace mesytec::mvme
{

class LIBMVME_EXPORT PrometheusContext
{
    public:
        PrometheusContext(const std::string &bind_address);

        PrometheusContext(const PrometheusContext &) = delete;
        PrometheusContext(PrometheusContext &&) = delete;
        PrometheusContext &operator=(const PrometheusContext &) = delete;
        PrometheusContext &operator=(PrometheusContext &&) = delete;

        prometheus::Exposer &exposer() { return exposer_; }
        // Default registry for "/metrics". Others can be added if needed.
        std::shared_ptr<prometheus::Registry> registry() { return registry_; }

    private:
        prometheus::Exposer exposer_;
        std::shared_ptr<prometheus::Registry> registry_;
};

void LIBMVME_EXPORT set_prometheus_instance(std::shared_ptr<PrometheusContext> prom);
std::shared_ptr<PrometheusContext> LIBMVME_EXPORT get_prometheus_instance();

// Expose MVMEStreamProcessorCounters using the IStreamBufferConsumer so the
// instance can be attached to both the old mvme and the newer mvlc stream
// workers.
class LIBMVME_EXPORT StreamProcCountersPromExporter: public IStreamBufferConsumer
{
    public:
        StreamProcCountersPromExporter();
        ~StreamProcCountersPromExporter() override;

        void setLogger(Logger logger) override;
        Logger &getLogger() override;

        void beginRun(const RunInfo &runInfo,
                      const VMEConfig *vmeConfig,
                      const analysis::Analysis *analysis) override;

        void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

        void processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer, size_t bufferSize) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // MVME_ENABLE_PROMETHEUS
#endif /* E37CF686_6939_4588_98BC_2E8192089B98 */
