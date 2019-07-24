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

struct DataBuffer;
class MesytecDiagnostics;
class VMEConfig;

struct LIBMVME_EXPORT MVMEStreamProcessorCounters
{
    QDateTime startTime;
    QDateTime stopTime;

    u64 bytesProcessed = 0;
    u32 buffersProcessed = 0;
    u32 buffersWithErrors = 0;
    u32 eventSections = 0;
    u32 invalidEventIndices = 0;

    using ModuleCounters = std::array<u32, MaxVMEModules>;

    std::array<u32, MaxVMEEvents> eventCounters;
    std::array<ModuleCounters, MaxVMEEvents> moduleCounters;
};

/* Interface for consumers of raw module data. */
class LIBMVME_EXPORT IMVMEStreamModuleConsumer
{
    public:
        using Logger = std::function<void (const QString &)>;

        virtual ~IMVMEStreamModuleConsumer() {};

        virtual void startup() {}
        virtual void shutdown() {}

        virtual void beginRun(const RunInfo &runInfo,
                              const VMEConfig *vmeConfig,
                              const analysis::Analysis *analysis) = 0;

        virtual void endRun(const DAQStats &stats, const std::exception *e = nullptr) = 0;

        virtual void beginEvent(s32 eventIndex) = 0;
        virtual void endEvent(s32 eventIndex) = 0;
        virtual void processModulePrefix(s32 eventIndex,
                                       s32 moduleIndex,
                                       const u32 *data, u32 size) = 0;
        virtual void processModuleData(s32 eventIndex,
                                       s32 moduleIndex,
                                       const u32 *data, u32 size) = 0;
        virtual void processModuleSuffix(s32 eventIndex,
                                       s32 moduleIndex,
                                       const u32 *data, u32 size) = 0;
        virtual void processTimetick() = 0;
        virtual void setLogger(Logger logger) = 0;
};

/* Interface for consumers of raw mvme stream formatted data buffers. */
class LIBMVME_EXPORT IMVMEStreamBufferConsumer
{
    public:
        using Logger = std::function<void (const QString &)>;

        virtual ~IMVMEStreamBufferConsumer() {};

        virtual void startup() {}
        virtual void shutdown() {}

        virtual void beginRun(const RunInfo &runInfo,
                              const VMEConfig *vmeConfig,
                              const analysis::Analysis *analysis,
                              Logger logger) = 0;

        virtual void endRun(const std::exception *e = nullptr) = 0;

        virtual void processDataBuffer(const DataBuffer *buffer) = 0;
        virtual void processTimetick() = 0;
};

struct MVMEStreamProcessorPrivate;

class LIBMVME_EXPORT MVMEStreamProcessor
{
    public:
        using Logger = std::function<void (const QString &)>;

        MVMEStreamProcessor();
        ~MVMEStreamProcessor();

        // Invokes startup() on attached module and buffer consumers.
        void startup();

        // Invokes shutdown() on attached module and buffer consumers.
        void shutdown();

        //
        // Statistics
        //
        MVMEStreamProcessorCounters getCounters() const;
        MVMEStreamProcessorCounters &getCounters();

        //
        // Processing
        //
        void beginRun(const RunInfo &runInfo, analysis::Analysis *analysis,
                      VMEConfig *vmeConfig, u32 listfileVersion, Logger logger);
        void endRun(const DAQStats &stats);
        void processDataBuffer(DataBuffer *buffer);

        // Used in DAQ Readout mode to generate timeticks for the analysis
        // independent of the readout data rate or analysis efficiency.
        void processExternalTimetick();


        //
        // Single Step Processing
        //

        /* Contains information about what was processed in the last call to
         * singleStepNextStep(). */
        struct ProcessingState
        {
            ProcessingState(DataBuffer *buffer = nullptr)
                : buffer(buffer)
            {
                resetModuleDataOffsets();
            }

            void resetModuleDataOffsets()
            {
                lastModuleDataSectionHeaderOffsets.fill(-1);
                lastModuleDataBeginOffsets.fill(-1);
                lastModuleDataEndOffsets.fill(-1);
            }

            /* Note: all offsets are counted in 32-bit words and are relative
             * to the beginning of the current buffer. Offsets are set to -1 if
             * the corresponding data is not present/invalid. */

            /* The buffer pointer that was passed to singleStepInitState().
             * Will not be cleared on error or end of buffer. */
            DataBuffer *buffer = nullptr;

            /* Word offset of the last section header in the buffer. */
            s32 lastSectionHeaderOffset = -1;

            // points to the listfile subevent/module header preceding the module data
            std::array<s32, MaxVMEModules> lastModuleDataSectionHeaderOffsets;
            // first word offset of the module event data
            std::array<s32, MaxVMEModules> lastModuleDataBeginOffsets;
            // last word offset of the module event data
            std::array<s32, MaxVMEModules> lastModuleDataEndOffsets;

            enum StepResult: u8
            {
                StepResult_Unset,           // a non-event section was processed
                StepResult_EventHasMore,    // event section was processed but there's more subevents left
                StepResult_EventComplete,   // event section was processed and completed
                StepResult_AtEnd,           // the last input buffer was fully processed. Further calls to singleStepNextStep() are not allowed.
                StepResult_Error,           // an error occured during processing. Further calls to singleStepNextStep() are not allowed.
            };

            StepResult stepResult = StepResult_Unset;
        };

        ProcessingState singleStepInitState(DataBuffer *buffer);
        ProcessingState &singleStepNextStep(ProcessingState &procState);

        //
        // Additional data consumers
        //

        void attachDiagnostics(std::shared_ptr<MesytecDiagnostics> diag);
        void removeDiagnostics();
        bool hasDiagnostics() const;

        void attachBufferConsumer(IMVMEStreamBufferConsumer *consumer);
        void removeBufferConsumer(IMVMEStreamBufferConsumer *consumer);

        void attachModuleConsumer(IMVMEStreamModuleConsumer *consumer);
        void removeModuleConsumer(IMVMEStreamModuleConsumer *consumer);

    private:
        std::unique_ptr<MVMEStreamProcessorPrivate> m_d;
};

#endif /* __MVME_STREAM_PROCESSOR_H__ */
