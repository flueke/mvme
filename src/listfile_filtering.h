#ifndef _MVME_LISTFILE_FILTERING_H_
#define _MVME_LISTFILE_FILTERING_H_

#include "stream_processor_consumers.h"
#include "stream_processor_counters.h"
#include <memory>

class LIBMVME_EXPORT ListfileFilterStreamConsumer: public IStreamModuleConsumer
{
    public:
        ListfileFilterStreamConsumer();
        ~ListfileFilterStreamConsumer() override;

        void setLogger(Logger logger) override;
        Logger &getLogger() override;

        void startup() override {}
        void shutdown() override {}

        void beginRun(const RunInfo &runInfo,
                      const VMEConfig *vmeConfig,
                      const analysis::Analysis *analysis) override;

        void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

        void beginEvent(s32 eventIndex) override;
        void endEvent(s32 eventIndex) override;
        void processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size) override;
        void processModuleData(s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount) override;
        void processSystemEvent(s32 crateIndex, const u32 *header, u32 size) override;
        void processTimetick() override {}; // noop

        // Thread-safe, returns a copy of the counters.
        MVMEStreamProcessorCounters getCounters() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};


#endif // _MVME_LISTFILE_FILTERING_H_
