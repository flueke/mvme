#ifndef __MVME_STREAM_PROCESSOR_H__
#define __MVME_STREAM_PROCESSOR_H__

#include "globals.h"
#include "libmvme_export.h"

#include <QDateTime>
#include <QString>

namespace analysis
{
class Analysis;
}

class DataBuffer;
class MesytecDiagnostics;
class VMEConfig;

struct LIBMVME_EXPORT MVMEStreamProcessorCounters
{
    static const u32 MaxModulesPerEvent = 20;

    QDateTime startTime;
    QDateTime stopTime;

    u64 bytesProcessed = 0;
    u32 buffersProcessed = 0;
    u32 buffersWithErrors = 0;
    u32 eventSections = 0;
    u32 invalidEventIndices = 0;

    using ModuleCounters = std::array<u32, MaxVMEModules>;

    std::array<ModuleCounters, MaxVMEEvents> moduleCounters;
    std::array<u32, MaxVMEEvents> eventCounters;
};

class LIBMVME_EXPORT IMVMEStreamConsumer
{
    public:
        virtual ~IMVMEStreamConsumer() {};

        virtual void beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig) = 0;
        virtual void endRun() = 0;

        virtual void beginEvent(s32 eventIndex) = 0;
        virtual void endEvent(s32 eventIndex) = 0;

        virtual void processModuleData(s32 eventIndex, s32 moduleIndex, u32 *data, u32 size) = 0;

        // TODO: figure out how timeticks should be done in the future and add
        // an api to pass them here.
};

struct MVMEStreamProcessorPrivate;

class LIBMVME_EXPORT MVMEStreamProcessor
{
    public:
        using Logger = std::function<void (const QString &)>;

        MVMEStreamProcessor();
        ~MVMEStreamProcessor();

        void beginRun(const RunInfo &runInfo, analysis::Analysis *analysis,
                      VMEConfig *vmeConfig, u32 listfileVersion, Logger logger);
        void endRun();

        void processDataBuffer(DataBuffer *buffer);

        const MVMEStreamProcessorCounters &getCounters() const;
        MVMEStreamProcessorCounters &getCounters();

        void attachDiagnostics(std::shared_ptr<MesytecDiagnostics> diag);
        void removeDiagnostics();
        bool hasDiagnostics() const;

        void attachConsumer(IMVMEStreamConsumer *consumer);
        void removeConsumer(IMVMEStreamConsumer *consumer);

    private:
        void processEventSection(u32 sectionHeader, u32 *data, u32 size);
        void logMessage(const QString &msg);

        std::unique_ptr<MVMEStreamProcessorPrivate> m_d;
};

#endif /* __MVME_STREAM_PROCESSOR_H__ */
