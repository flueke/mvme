#ifndef __MVME_STREAM_PROCESSOR_MODULE_CONSUMER_H__
#define __MVME_STREAM_PROCESSOR_MODULE_CONSUMER_H__

#include <QString>
#include <functional>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include "libmvme_export.h"
#include "typedefs.h"

class VMEConfig;
namespace analysis { class Analysis; }
struct DAQStats;
struct RunInfo;
class StreamWorkerBase;

class LIBMVME_EXPORT StreamConsumerBase
{
    public:
        using Logger = std::function<void (const QString &)>;
        virtual ~StreamConsumerBase() {}
        virtual void setLogger(Logger logger) = 0;
        virtual Logger &getLogger() = 0;
        void setStreamWorker(StreamWorkerBase *worker) { worker_ = worker; }
        StreamWorkerBase *getStreamWorker() const { return worker_; }

    protected:
        void logMessage(const QString &msg)
        {
            if (auto &logger = getLogger())
                logger(msg);
        }

    private:
        StreamWorkerBase *worker_;
 };

/* Interface for consumers of raw module data. */
class LIBMVME_EXPORT IStreamModuleConsumer: public StreamConsumerBase
{
    public:
        using ModuleData = mesytec::mvlc::readout_parser::ModuleData;

        virtual void startup() {}
        virtual void shutdown() {}

        virtual void beginRun(const RunInfo &runInfo,
                              const VMEConfig *vmeConfig,
                              analysis::Analysis *analysis) = 0;

        virtual void endRun(const DAQStats &stats, const std::exception *e = nullptr) = 0;

        virtual void beginEvent(s32 eventIndex) = 0;
        virtual void endEvent(s32 eventIndex) = 0;

        // Old interface still used by MVMEStreamProcessor.
        virtual void processModuleData(s32 eventIndex, s32 moduleIndex,
            const u32 *data, u32 size) = 0;

        // New interface used by the MVLC code. Requires only one call per event
        // instead of one per module.
        virtual void processModuleData(s32 crateIndex, s32 eventIndex,
            const ModuleData *moduleDataList, unsigned moduleCount) = 0;

        // Note: Having both system event and timetick methods is not redunant:
        // during live DAQ runs the latter method is called while during replays
        // timeticks are extracted from incoming system event buffers.
        virtual void processSystemEvent(s32 crateIndex, const u32 *header, u32 size) = 0;
        virtual void processTimetick() = 0;

        //virtual void setEnabled(bool enabled) = 0;
};

/* Interface for consumers of raw readout data. The bufferType argument to
 * processBuffer() can be used to hold a VME controller specific buffer type tag. */
class LIBMVME_EXPORT IStreamBufferConsumer: public StreamConsumerBase
{
    public:
        virtual ~IStreamBufferConsumer() {}

        virtual void startup() {}
        virtual void shutdown() {}

        virtual void beginRun(const RunInfo &runInfo,
                              const VMEConfig *vmeConfig,
                              const analysis::Analysis *analysis) = 0;

        virtual void endRun(const DAQStats &stats, const std::exception *e = nullptr) = 0;

        virtual void processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer, size_t bufferSize) = 0;
};

#endif /* __MVME_STREAM_PROCESSOR_MODULE_CONSUMER_H__ */
