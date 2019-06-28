#include "mvlc_stream_worker.h"

#include <QThread>

#include "databuffer.h"
#include "mvme_context.h"
#include "vme_analysis_common.h"
#include "mvlc/mvlc_impl_eth.h"


using namespace vme_analysis_common;
using namespace mesytec::mvlc;

VMEConfReadoutScripts collect_readout_scripts(const VMEConfig &vmeConfig)
{
    VMEConfReadoutScripts readoutScripts;

    for (const auto &eventConfig: vmeConfig.getEventConfigs())
    {
        std::vector<vme_script::VMEScript> moduleReadoutScripts;

        for (const auto &moduleConfig: eventConfig->getModuleConfigs())
        {
            auto rdoScript = moduleConfig->getReadoutScript()->getScript();
            moduleReadoutScripts.emplace_back(rdoScript);
        }

        readoutScripts.emplace_back(moduleReadoutScripts);
    }

    return readoutScripts;
}

//
// MVLC_StreamWorker
//
MVLC_StreamWorker::MVLC_StreamWorker(
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
, m_startPaused(false)
, m_stopFlag(StopWhenQueueEmpty)
{
}

MVLC_StreamWorker::~MVLC_StreamWorker()
{
}

void MVLC_StreamWorker::setState(MVMEStreamWorkerState newState)
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

void MVLC_StreamWorker::setupParserCallbacks(analysis::Analysis *analysis)
{
    m_parserCallbacks.beginEvent = [analysis](int ei)
    {
        //qDebug() << "beginEvent" << ei;
        analysis->beginEvent(ei);
    };

    m_parserCallbacks.modulePrefix = [analysis](int ei, int mi, u32 *data, u32 size)
    {
        //qDebug() << "  modulePrefix" << ei << mi << data << size;
        analysis->processModulePrefix(ei, mi, data, size);
    };

    m_parserCallbacks.moduleDynamic = [this, analysis](int ei, int mi, u32 *data, u32 size)
    {
        //qDebug() << "  moduleDynamic" << ei << mi << data << size;
        analysis->processModuleData(ei, mi, data, size);

        if (0 <= ei && ei < MaxVMEEvents && 0 <= mi && mi < MaxVMEModules)
        {
            UniqueLock guard(m_countersMutex);
            m_counters.moduleCounters[ei][mi]++;
        }
    };

    m_parserCallbacks.moduleSuffix = [analysis](int ei, int mi, u32 *data, u32 size)
    {
        //qDebug() << "  moduleSuffix" << ei << mi << data << size;
        analysis->processModuleSuffix(ei, mi, data, size);
    };

    m_parserCallbacks.endEvent = [this, analysis](int ei)
    {
        //qDebug() << "endEvent" << ei;
        analysis->endEvent(ei);

        if (0 <= ei && ei < MaxVMEEvents)
        {
            UniqueLock guard(m_countersMutex);
            m_counters.eventSections++;
            m_counters.eventCounters[ei]++;
        }
    };

    m_parserCallbacks.systemEvent = [analysis](u32 *header, u32 size)
    {
        u8 subtype = system_event::extract_subtype(*header);

        // IMPORTANT: This assumes that a timestamp is added to the listfile
        // every 1 second. Jitter is not taken into account and the actual
        // timestamp value is not used.
        if (subtype == system_event::subtype::UnixTimestamp)
        {
            analysis->processTimetick();
        }
    };
}

void MVLC_StreamWorker::start()
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
        UniqueLock guard(m_countersMutex);
        m_counters = {};
        m_counters.startTime = QDateTime::currentDateTime();
    }

    setupParserCallbacks(analysis);

    try
    {
        UniqueLock guard(m_parserMutex);
        m_parser = make_readout_parser(collect_readout_scripts(*vmeConfig));
    }
    catch (const std::exception &e)
    {
        logError(QSL("Error setting up MVLC stream parser: %1")
                 .arg(e.what()));
        return;
    }

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
        UniqueLock guard(m_countersMutex);
        m_counters.stopTime = QDateTime::currentDateTime();
    }

    setState(WorkerState::Idle);
}

void MVLC_StreamWorker::processBuffer(
    DataBuffer *buffer,
    const RunInfo &runInfo,
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis)
{
    bool ok = false;

    try
    {
        UniqueLock guard(m_parserMutex);

        if (buffer->tag == static_cast<int>(ListfileBufferFormat::MVLC_ETH))
        {
            // Input buffers are MVLC_ETH formatted buffers as generated by
            // MVLCReadoutWorker::readout_eth().
            parse_readout_buffer_eth(
                m_parser, m_parserCallbacks,
                buffer->id, buffer->data, buffer->used);
        }
        else if (buffer->tag == static_cast<int>(ListfileBufferFormat::MVLC_USB))
        {
            // Input buffers are MVLC_USB formatted buffers as generated by
            // MVLCReadoutWorker::readout_usb()
            parse_readout_buffer_usb(
                m_parser, m_parserCallbacks,
                buffer->id, buffer->data, buffer->used);
        }
        else
            throw std::runtime_error("unexpected buffer format (expected MVLC_ETH or MVLC_USB)");

        ok = true; // No exception was thrown above
    }
    catch (const end_of_buffer &e)
    {
        logWarn(QSL("end_of_buffer (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->id),
                true);
    }
    catch (const std::exception &e)
    {
        logWarn(QSL("exception (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->id),
                true);
    }
    catch (...)
    {
        logWarn(QSL("unknown exception when parsing buffer #%1")
                .arg(buffer->id),
                true);
    }

    // Put the buffer back onto the free queue
    enqueue(m_freeBuffers, buffer);

    {
        UniqueLock guard(m_countersMutex);
        m_counters.bytesProcessed += buffer->used;
        m_counters.buffersProcessed++;
        if (!ok) m_counters.buffersWithErrors++;
    }
}

void MVLC_StreamWorker::stop(bool whenQueueEmpty)
{
    m_stopFlag = (whenQueueEmpty ? StopWhenQueueEmpty : StopImmediately);
    m_desiredState = MVMEStreamWorkerState::Idle;
}

void MVLC_StreamWorker::pause()
{
    m_desiredState = MVMEStreamWorkerState::Paused;
}

void MVLC_StreamWorker::resume()
{
    m_desiredState = MVMEStreamWorkerState::Running;
}

void MVLC_StreamWorker::singleStep()
{
    logError("SingleSteppping not implemented for the MVLC stream processor");
    //m_desiredState = MVMEStreamWorkerState::SingleStepping;
}

void MVLC_StreamWorker::startupConsumers()
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

void MVLC_StreamWorker::shutdownConsumers()
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
