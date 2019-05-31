#include "mvlc_stream_workers.h"

#include <QThread>

#include "mvme_context.h"
#include "vme_analysis_common.h"
#include "mvlc/mvlc_impl_eth.h"


using namespace vme_analysis_common;
using namespace mesytec::mvlc;

/* analysis calls:
 * getVMEObjectSettings(eventConfig | moduleConfig)
 *  -> multi event processing enabled for an event? module enabled? module header filter.
 *     Multi-Event processing will move into a second stage after event reassembly (linearization).
 * processTimetick
 * beginEvent(eventIndex)
 *   processModuleData(eventIndex, moduleIndex, data *, size) // needs linear data
 * endEvent
 *
 * to be added: processModulePrefix(ei, mi, data *, size);
 *              processModuleSuffix(ei, mi, data *, size);
 *
 *
 * Purpose: The readout_proc system is used to parse a possibly lossfull
 * sequence of MVLC readout buffers, reassemble complete readout events and
 * make the reassembled data available to a consumer.
 *
 * Commands that produce output:
 *   marker         -> one word
 *   single_read    -> one word
 *   block_read     -> dynamic part (0xF5 framed)
 *
 * Restrictions per module:
 * - one fixed part
 * - one dynamic block part
 * - one fixed part
 */

struct Parser_ModuleParts
{
    u8 prefixLen; // length in words of the fixed part prefix
    u8 suffixLen; // length in words of the fixed part suffix
    bool hasDynamic; // true if a dynamic part (block read) is present
};

struct SpanInfo
{
    u32 offset;
    u32 size;
};

struct ModuleReadoutSpan
{
    SpanInfo prefixSpan;
    SpanInfo dynamicSpan;
    SpanInfo suffixSpan;
};

struct ReadoutProcCallbacks
{
    std::function<void (int ei)> beginEvent;
    std::function<void (int ei, int mi, u32 *data, u32 size)> moduleData;
    std::function<void (int ei)> endEvent;
};

struct ReadoutProcessor
{
    using ModuleInfos = std::vector<Parser_ModuleParts>;
    using EventInfos = std::vector<ModuleInfos>;
    EventInfos eventInfos;
    DataBuffer workBuffer;
};

ReadoutProcessor::EventInfos parse_readout_event_info(const VMEConfig &vmeConfig)
{
    auto parse_readout_script = [](const vme_script::VMEScript &rdoScript)
    {
        using namespace vme_script;
        enum State { Prefix, Dynamic, Suffix };
        State state = Prefix;
        Parser_ModuleParts modParts = {};

        for (auto &cmd: rdoScript)
        {
            if (cmd.type == CommandType::Read
                || cmd.type == CommandType::Marker)
            {
                switch (state)
                {
                    case Prefix:
                        modParts.prefixLen++;
                        break;
                    case Dynamic:
                        modParts.suffixLen++;
                        state = Suffix;
                        break;
                    case Suffix:
                        modParts.suffixLen++;
                        break;
                }
            }
            else if (is_block_read_command(cmd.type))
            {
                switch (state)
                {
                    case Prefix:
                        modParts.hasDynamic = true;
                        state = Dynamic;
                        break;
                    case Dynamic:
                        throw std::runtime_error("multiple block reads in module readout");
                    case Suffix:
                        throw std::runtime_error("block read after suffix in module readout");
                }
            }
        }

        return modParts;
    };

    ReadoutProcessor::EventInfos result;

    for (auto &eventConfig: vmeConfig.getEventConfigs())
    {
        ReadoutProcessor::ModuleInfos moduleInfos;

        for (auto &moduleConfig: eventConfig->getModuleConfigs())
        {
            auto rdoScript = moduleConfig->getReadoutScript()->getScript();
            auto modParts = parse_readout_script(rdoScript);
            moduleInfos.emplace_back(modParts);
        }

        result.emplace_back(moduleInfos);
    }

    return result;
}

struct ReadoutProcessor_ETH: public ReadoutProcessor
{
    s32 lastPacketNumber;
    eth::PacketReadResult prr;
};

void rdo_process_buffer(
    ReadoutProcessor_ETH &proc, ReadoutProcCallbacks &callbacks,
    u32 *buffer, size_t bufferSize)
{
}

struct ReadoutProcessor_USB: public ReadoutProcessor
{
};

void rdo_process_buffer(
    ReadoutProcessor_USB &proc, ReadoutProcCallbacks &callbacks,
    u32 *buffer, size_t bufferSize)
{
}

//
// MVLC_StreamWorkerBase
//
MVLC_StreamWorkerBase::MVLC_StreamWorkerBase(
    MVMEContext *context,
    ThreadSafeDataBufferQueue *freeBuffers,
    ThreadSafeDataBufferQueue *fullBuffers,
    QObject *parent)
: StreamWorkerBase(parent)
, m_context(context)
, m_freeBuffers(freeBuffers)
, m_fullBuffers(fullBuffers)
, m_state(MVMEStreamWorkerState::Idle)
, m_desiredState(MVMEStreamWorkerState::Idle)
, m_stopFlag(StopWhenQueueEmpty)
{
}

void MVLC_StreamWorkerBase::setState(MVMEStreamWorkerState newState)
{
    // This implementation copies the behavior of MVMEStreamWorker::setState.
    // Signal emission is done in the exact same order.
    // The implementation was and is buggy: the transition into Running always
    // caused started() to be emitted even when coming from Paused state.

    m_state = newState;
    m_desiredState = newState;
    emit stateChanged(m_state);

    switch (newState)
    {
        case MVMEStreamWorkerState::Idle:
            emit stopped();
            break;

        case MVMEStreamWorkerState::Running:
            emit started();
            break;

        case MVMEStreamWorkerState::Paused:
        case MVMEStreamWorkerState::SingleStepping:
            break;
    }
}

void MVLC_StreamWorkerBase::start()
{
    using WorkerState = MVMEStreamWorkerState;

    if (m_state != WorkerState::Idle)
    {
        logError("worker state != Idle, ignoring request to start");
        return;
    }

    const auto runInfo = m_context->getRunInfo();
    const auto vmeConfig = m_context->getVMEConfig();
    auto analysis = m_context->getAnalysis();

    {
        CountersLock guard(m_countersMutex);
        m_counters = {};
        m_counters.startTime = QDateTime::currentDateTime();
    }
    beginRun_(runInfo, vmeConfig, analysis);

    setState(m_startPaused ? WorkerState::Paused : WorkerState::Running);

    TimetickGenerator timetickGen;

    while (true)
    {
        // running
        if (likely(m_state == WorkerState::Running && m_desiredState == WorkerState::Running))
        {
            static const unsigned FullBufferWaitTime_ms = 100;

            if (auto buffer = dequeue(m_fullBuffers, FullBufferWaitTime_ms))
                processBuffer(buffer, runInfo, vmeConfig, analysis);
        }
        // pause
        else if (m_state == WorkerState::Running && m_desiredState == WorkerState::Paused)
        {
            setState(WorkerState::Paused);
        }
        // resume
        else if (m_state == WorkerState::Paused && m_desiredState == WorkerState::Running)
        {
            setState(WorkerState::Running);
        }
        // stopping
        else if (m_desiredState == WorkerState::Idle)
        {
            if (m_stopFlag == StopImmediately)
                break;

            // The StopWhenQueueEmpty case
            if (auto buffer = dequeue(m_fullBuffers))
                processBuffer(buffer, runInfo, vmeConfig, analysis);
            else
                break;
        }
        // paused
        else if (m_state == WorkerState::Paused)
        {
            static const unsigned PauseSleepDuration_ms = 100;

            QThread::msleep(std::min(
                    PauseSleepDuration_ms,
                    static_cast<unsigned>(timetickGen.getTimeToNextTick_ms())));
        }
        else
            InvalidCodePath;

        if (!runInfo.isReplay)
        {
            int elapsedSeconds = timetickGen.generateElapsedSeconds();

            while (elapsedSeconds >= 1)
            {
                analysis->processTimetick();
                elapsedSeconds--;
            }
        }
    }

    {
        CountersLock guard(m_countersMutex);
        m_counters.stopTime = QDateTime::currentDateTime();
    }

    setState(WorkerState::Idle);
}

void MVLC_StreamWorkerBase::processBuffer(
    DataBuffer *buffer,
    const RunInfo &runInfo,
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis)
{
    bool ok = processBuffer_(buffer, runInfo, vmeConfig, analysis);
    enqueue(m_freeBuffers, buffer);

    {
        CountersLock guard(m_countersMutex);
        m_counters.bytesProcessed += buffer->used;
        m_counters.buffersProcessed++;
        if (!ok) m_counters.buffersWithErrors++;
    }
}

void MVLC_StreamWorkerBase::stop(bool whenQueueEmpty)
{
    m_stopFlag = (whenQueueEmpty ? StopWhenQueueEmpty : StopImmediately);
    m_desiredState = MVMEStreamWorkerState::Idle;
}

void MVLC_StreamWorkerBase::pause()
{
    m_desiredState = MVMEStreamWorkerState::Paused;
}

void MVLC_StreamWorkerBase::resume()
{
    m_desiredState = MVMEStreamWorkerState::Running;
}

void MVLC_StreamWorkerBase::singleStep()
{
    logError("SingleSteppping not implemented for the MVLC data processor");
    //m_desiredState = MVMEStreamWorkerState::SingleStepping;
}

void MVLC_StreamWorkerBase::startupConsumers()
{
    for (auto c: m_bufferConsumers)
    {
        c->startup();
    }

    for (auto c: m_moduleConsumers)
    {
        c->startup();
    }
}

void MVLC_StreamWorkerBase::shutdownConsumers()
{
    for (auto c: m_bufferConsumers)
    {
        c->shutdown();
    }

    for (auto c: m_moduleConsumers)
    {
        c->shutdown();
    }
}

//
// MVLC_ETH_StreamWorker
//
MVLC_ETH_StreamWorker::~MVLC_ETH_StreamWorker()
{
}

void MVLC_ETH_StreamWorker::beginRun_(
    const RunInfo &runInfo,
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis)
{
}

// Input is a sequence of MVLC_ETH formatted buffers as generated by
// MVLCReadoutWorker::readout_eth().
bool MVLC_ETH_StreamWorker::processBuffer_(
    DataBuffer *buffer,
    const RunInfo &runInfo,
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis)
{
    if (buffer->tag != static_cast<int>(DataBufferFormatTags::MVLC_ETH))
        return false;

    try
    {
    } catch (const end_of_buffer &)
    {
    }
    catch (const std::runtime_error &)
    {
    }

    return false;
}

//
// MVLC_USB_StreamWorker
//
MVLC_USB_StreamWorker::~MVLC_USB_StreamWorker()
{
}

void MVLC_USB_StreamWorker::beginRun_(
    const RunInfo &runInfo,
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis)
{
}

// Input is a sequence of MVLC_USB formatted buffers as generated by
// MVLCReadoutWorker::readout_usb()
bool MVLC_USB_StreamWorker::processBuffer_(
    DataBuffer *buffer,
    const RunInfo &runInfo,
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis)
{
    if (buffer->tag != static_cast<int>(DataBufferFormatTags::MVLC_USB))
        return false;

    try
    {
        return true;
    } catch (const end_of_buffer &)
    {
        return false;
    }
    catch (const std::runtime_error &)
    {
        return false;
    }

    return false;
}
