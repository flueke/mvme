#include "mvlc/mvlc_impl_eth.h" // keep on top to avoid a winsock2 warning
#include "mvlc_stream_worker.h"

#include <algorithm>
#include <QThread>

#include "analysis/analysis_util.h"
#include "databuffer.h"
#include "mvme_context.h"
#include "vme_analysis_common.h"

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
, m_debugInfoRequest(DebugInfoRequest::None)
{
    qRegisterMetaType<mesytec::mvlc::ReadoutParserState>(
        "mesytec::mvlc::ReadoutParserState");
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

void MVLC_StreamWorker::setupParserCallbacks(const VMEConfig *vmeConfig, analysis::Analysis *analysis)
{
    m_parserCallbacks = ReadoutParserCallbacks();

    m_parserCallbacks.beginEvent = [this, analysis](int ei)
    {
        //qDebug() << "beginEvent" << ei;
        analysis->beginEvent(ei);

        for (auto c: m_moduleConsumers)
        {
            c->beginEvent(ei);
        }
    };

    m_parserCallbacks.modulePrefix = [this, analysis](int ei, int mi, const u32 *data, u32 size)
    {
        //qDebug() << "  modulePrefix" << ei << mi << data << size;

        // FIXME: The IMVMEStreamModuleConsumer interface doesn't support
        // prefix/suffix data right now. Add this in.

        // FIXME: Hack checking if the module does not have a dynamic part. In
        // this case the readout data is handed to the analysis via
        // processModuleData(). This workaround makes the MVLC readout
        // compatible to readouts with the older controllers.
        // Once the analysis is updates and proper filter templates for
        // prefix/suffix have been added this change should be removed!
        // Note: this works for scripts containing only register reads, e.g.
        // the standard MesytecCounter script.
        auto moduleParts = m_parser.readoutInfo[ei][mi];

        if (!moduleParts.hasDynamic)
        {
            analysis->processModuleData(ei, mi, data, size);
            for (auto c: m_moduleConsumers)
                c->processModuleData(ei, mi, data, size);
        }
        else
        {
            analysis->processModulePrefix(ei, mi, data, size);
        }
    };

    m_parserCallbacks.moduleDynamic = [this, analysis](int ei, int mi, const u32 *data, u32 size)
    {
        //qDebug() << "  moduleDynamic" << ei << mi << data << size;
        analysis->processModuleData(ei, mi, data, size);

        for (auto c: m_moduleConsumers)
        {
            c->processModuleData(ei, mi, data, size);
        }

        if (0 <= ei && ei < MaxVMEEvents && 0 <= mi && mi < MaxVMEModules)
        {
            UniqueLock guard(m_countersMutex);
            m_counters.moduleCounters[ei][mi]++;
        }
    };

    m_parserCallbacks.moduleSuffix = [analysis](int ei, int mi, const u32 *data, u32 size)
    {
        //qDebug() << "  moduleSuffix" << ei << mi << data << size;
        analysis->processModuleSuffix(ei, mi, data, size);

        // FIXME: The IMVMEStreamModuleConsumer interface doesn't support
        // prefix/suffix data right now
    };

    m_parserCallbacks.endEvent = [this, analysis](int ei)
    {
        //qDebug() << "endEvent" << ei;
        analysis->endEvent(ei);

        for (auto c: m_moduleConsumers)
        {
            c->endEvent(ei);
        }

        if (0 <= ei && ei < MaxVMEEvents)
        {
            UniqueLock guard(m_countersMutex);
            m_counters.eventSections++;
            m_counters.eventCounters[ei]++;
        }
    };

    m_parserCallbacks.systemEvent = [this, analysis](u32 *header, u32 size)
    {
        u8 subtype = system_event::extract_subtype(*header);

        // IMPORTANT: This assumes that a timestamp is added to the listfile
        // every 1 second. Jitter is not taken into account and the actual
        // timestamp value is not used at the moment.
        if (subtype == system_event::subtype::UnixTimestamp)
        {
            analysis->processTimetick();
        }

        for (auto c: m_moduleConsumers)
        {
            c->processTimetick();
        }
    };

    const auto eventConfigs = vmeConfig->getEventConfigs();

    // Setup multi event splitting if needed
    if (uses_multi_event_splitting(*vmeConfig, *analysis))
    {
        using namespace mvme;

        auto filterStrings = collect_multi_event_splitter_filter_strings(
            *vmeConfig, *analysis);

        m_multiEventSplitter = multi_event_splitter::make_splitter(filterStrings);

        // Copy our callbacks, which are driving the analysis, to the callbacks
        // for the multi event splitter.
        auto &splitterCallbacks = m_multiEventSplitterCallbacks;
        splitterCallbacks.beginEvent = m_parserCallbacks.beginEvent;
        splitterCallbacks.modulePrefix = m_parserCallbacks.modulePrefix;
        splitterCallbacks.moduleDynamic = m_parserCallbacks.moduleDynamic;
        splitterCallbacks.moduleSuffix = m_parserCallbacks.moduleSuffix;
        splitterCallbacks.endEvent = m_parserCallbacks.endEvent;

        // Now overwrite our own callbacks to drive the splitter instead of the
        // analysis.
        // Note: the systemEvent callback is not overwritten as there is no
        // special handling for it in the multi event splitting logic.
        m_parserCallbacks.beginEvent = [this] (int ei)
        {
            multi_event_splitter::begin_event(m_multiEventSplitter, ei);
        };

        m_parserCallbacks.modulePrefix = [this](int ei, int mi, const u32 *data, u32 size)
        {
            multi_event_splitter::module_prefix(m_multiEventSplitter, ei, mi, data, size);
        };

        m_parserCallbacks.moduleDynamic = [this](int ei, int mi, const u32 *data, u32 size)
        {
            multi_event_splitter::module_data(m_multiEventSplitter, ei, mi, data, size);
        };

        m_parserCallbacks.moduleSuffix = [this](int ei, int mi, const u32 *data, u32 size)
        {
            multi_event_splitter::module_suffix(m_multiEventSplitter, ei, mi, data, size);
        };

        m_parserCallbacks.endEvent = [this](int ei)
        {
            multi_event_splitter::end_event(m_multiEventSplitter, m_multiEventSplitterCallbacks, ei);
        };
    }
}

void dump_parser_info(const ReadoutParserState &parser)
{
    auto &readoutInfo = parser.readoutInfo;

    for (size_t eventIndex=0; eventIndex<readoutInfo.size(); eventIndex++)
    {
        const auto &modules = readoutInfo[eventIndex];

        for (size_t moduleIndex=0; moduleIndex<modules.size(); moduleIndex++)
        {
            const auto &moduleParts = modules[moduleIndex];

            qDebug("mvlc readout info: ei=%u, mi=%u: prefixLen=%u, suffixLen=%u, hasDynamic=%d",
                   eventIndex, moduleIndex,
                   moduleParts.prefixLen,
                   moduleParts.suffixLen,
                   moduleParts.hasDynamic);
        }
    }
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

    setupParserCallbacks(vmeConfig, analysis);

    try
    {
        UniqueLock guard(m_parserMutex);
        m_parser = make_readout_parser(collect_readout_scripts(*vmeConfig));
        dump_parser_info(m_parser);
        assert(false);
    }
    catch (const std::exception &e)
    {
        logError(QSL("Error setting up MVLC stream parser: %1")
                 .arg(e.what()));
        return;
    }

    for (auto c: m_bufferConsumers)
    {
        c->beginRun(runInfo, vmeConfig, analysis,
                    [this] (const QString &msg) { m_context->logMessage(msg); });
    }

    for (auto c: m_moduleConsumers)
    {
        c->beginRun(runInfo, vmeConfig, analysis);
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

                for (auto c: m_moduleConsumers)
                {
                    c->processTimetick();
                }

                elapsedSeconds--;
            }
        }
    }

    for (auto c: m_bufferConsumers)
    {
        c->endRun();
    }

    for (auto c: m_moduleConsumers)
    {
        c->endRun(m_context->getDAQStats());
    }

    analysis->endRun();

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
    DebugInfoRequest debugRequest = m_debugInfoRequest;
    ReadoutParserState debugSavedParserState;

    // If debug info was requested create a copy of the parser state before
    // attempting to parse the input buffer.
    if (debugRequest != DebugInfoRequest::None)
    {
        debugSavedParserState = m_parser;
    }

    bool processingOk = false;
    bool exceptionSeen = false;

    try
    {
        for (auto c: m_bufferConsumers)
        {
            c->processDataBuffer(buffer);
        }

        UniqueLock guard(m_parserMutex);
        ParseResult pr = {};

        if (buffer->tag == static_cast<int>(ListfileBufferFormat::MVLC_ETH))
        {
            // Input buffers are MVLC_ETH formatted buffers as generated by
            // MVLCReadoutWorker::readout_eth().
            pr = parse_readout_buffer_eth(
                m_parser, m_parserCallbacks,
                buffer->id, buffer->data, buffer->used);
        }
        else if (buffer->tag == static_cast<int>(ListfileBufferFormat::MVLC_USB))
        {
            // Input buffers are MVLC_USB formatted buffers as generated by
            // MVLCReadoutWorker::readout_usb()
            pr = parse_readout_buffer_usb(
                m_parser, m_parserCallbacks,
                buffer->id, buffer->data, buffer->used);
        }
        else
            throw std::runtime_error("unexpected buffer format (expected MVLC_ETH or MVLC_USB)");

        if (pr == ParseResult::Ok)
        {
            // No exception was thrown and the parse result for the buffer is
            // ok.
            processingOk = true;
        }
        else
            qDebug() << __PRETTY_FUNCTION__ << (int)pr << get_parse_result_name(pr);
    }
    catch (const end_of_buffer &e)
    {
        logWarn(QSL("end_of_buffer (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->id),
                true);
        exceptionSeen = true;
    }
    catch (const std::exception &e)
    {
        logWarn(QSL("exception (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->id),
                true);
        exceptionSeen = true;
    }
    catch (...)
    {
        logWarn(QSL("unknown exception when parsing buffer #%1")
                .arg(buffer->id),
                true);
        exceptionSeen = true;
    }

    if (exceptionSeen)
        qDebug() << __PRETTY_FUNCTION__ << "exception seen";

    if (debugRequest == DebugInfoRequest::OnNextBuffer
        || (debugRequest == DebugInfoRequest::OnNextError && !processingOk))
    {
        m_debugInfoRequest = DebugInfoRequest::None;
        emit debugInfoReady(*buffer, debugSavedParserState, vmeConfig, analysis);
    }

    // Put the buffer back onto the free queue
    enqueue(m_freeBuffers, buffer);

    {
        UniqueLock guard(m_countersMutex);
        m_counters.bytesProcessed += buffer->used;
        m_counters.buffersProcessed++;
        if (!processingOk)
        {
            m_counters.buffersWithErrors++;
        }
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
