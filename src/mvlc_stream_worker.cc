/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvlc/mvlc_impl_eth.h" // keep on top to avoid a winsock2 warning
#include "mvlc_stream_worker.h"

#include <algorithm>
#include <mutex>
#include <QThread>

#include "analysis/analysis_util.h"
#include "analysis/analysis_session.h"
#include "databuffer.h"
#include "mvme_context.h"
#include "vme_config_scripts.h"
#include "vme_analysis_common.h"

using namespace vme_analysis_common;
using namespace mesytec::mvlc;

using WorkerState = MVMEStreamWorkerState;

VMEConfReadoutScripts collect_readout_scripts(const VMEConfig &vmeConfig)
{
    VMEConfReadoutScripts readoutScripts;

    for (const auto &eventConfig: vmeConfig.getEventConfigs())
    {
        std::vector<vme_script::VMEScript> moduleReadoutScripts;

        for (const auto &moduleConfig: eventConfig->getModuleConfigs())
        {
            if (moduleConfig->isEnabled())
            {
                auto rdoScript = mesytec::mvme::parse(moduleConfig->getReadoutScript());
                moduleReadoutScripts.emplace_back(rdoScript);
            }
            else
                moduleReadoutScripts.emplace_back(vme_script::VMEScript{});
        }

        readoutScripts.emplace_back(moduleReadoutScripts);
    }

    return readoutScripts;
}

void begin_event_record(
    EventRecord &record, int eventIndex)
{
    record.eventIndex = eventIndex;
    record.modulesData.clear();
}

void record_module_part(
    EventRecord &record, EventRecord::RecordModulePart part,
    int moduleIndex, const u32 *data, u32 size)
{
    if (record.modulesData.size() <= moduleIndex)
        record.modulesData.resize(moduleIndex + 1);

    QVector<u32> *dest = nullptr;

    switch (part)
    {
        case EventRecord::Prefix:
            dest = &record.modulesData[moduleIndex].prefix;
            break;

        case EventRecord::Dynamic:
            dest = &record.modulesData[moduleIndex].dynamic;
            break;

        case EventRecord::Suffix:
            dest = &record.modulesData[moduleIndex].suffix;
            break;
    }

    assert(dest);

    std::copy(data, data + size, std::back_inserter(*dest));
}

bool is_empty(const EventRecord::ModuleData &moduleData)
{
    return moduleData.prefix.isEmpty()
        && moduleData.dynamic.isEmpty()
        && moduleData.suffix.isEmpty();
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
    // Also stateChanged() is emitted even if the old and new states are the
    // same.

    {
        std::unique_lock<std::mutex> guard(m_stateMutex);

        m_state = newState;
        m_desiredState = newState;
    }

    qDebug() << __PRETTY_FUNCTION__ << "emit stateChanged" << to_string(newState);
    emit stateChanged(newState);

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
        this->blockIfPaused();

        //qDebug() << "beginEvent" << ei;
        analysis->beginEvent(ei);

        for (auto c: m_moduleConsumers)
            c->beginEvent(ei);

        if (m_state == WorkerState::SingleStepping)
            begin_event_record(m_singleStepEventRecord, ei);
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
        // Once the analysis is updated and proper filter templates for
        // prefix/suffix have been added this change should be removed!
        // Note: this works for scripts containing only register reads, e.g.
        // the standard MesytecCounter script.
        // FIXME: Missing counters for module prefix & suffix!
        auto moduleParts = m_parser.readoutInfo[ei][mi];

        if (!moduleParts.hasDynamic)
        {
            analysis->processModuleData(ei, mi, data, size);
            for (auto c: m_moduleConsumers)
                c->processModuleData(ei, mi, data, size);

            UniqueLock guard(m_countersMutex);
            m_counters.moduleCounters[ei][mi]++;
        }
        else
        {
            analysis->processModulePrefix(ei, mi, data, size);
        }

        if (m_state == WorkerState::SingleStepping)
        {
            record_module_part(m_singleStepEventRecord, EventRecord::Prefix,
                               mi, data, size);
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

        if (m_state == WorkerState::SingleStepping)
        {
            record_module_part(m_singleStepEventRecord, EventRecord::Dynamic,
                               mi, data, size);
        }
    };

    m_parserCallbacks.moduleSuffix = [this, analysis](int ei, int mi, const u32 *data, u32 size)
    {
        //qDebug() << "  moduleSuffix" << ei << mi << data << size;
        analysis->processModuleSuffix(ei, mi, data, size);

        // FIXME: The IMVMEStreamModuleConsumer interface doesn't support
        // prefix/suffix data right now

        if (m_state == WorkerState::SingleStepping)
        {
            record_module_part(m_singleStepEventRecord, EventRecord::Suffix,
                               mi, data, size);
        }
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

        this->publishStateIfSingleStepping();
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

        logInfo("enabling multi_event_splitter");

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

void MVLC_StreamWorker::logParserInfo(const mesytec::mvlc::ReadoutParserState &parser)
{
    auto &readoutInfo = parser.readoutInfo;

    for (size_t eventIndex=0; eventIndex<readoutInfo.size(); eventIndex++)
    {
        const auto &modules = readoutInfo[eventIndex];

        for (size_t moduleIndex=0; moduleIndex<modules.size(); moduleIndex++)
        {
            const auto &moduleParts = modules[moduleIndex];

#if 0
            logInfo(QString("mvlc readout parser info: ei=%1, mi=%2: prefixLen=%3, suffixLen=%4, hasDynamic=%5")
                    .arg(eventIndex)
                    .arg(moduleIndex)
                    .arg(static_cast<unsigned>(moduleParts.prefixLen))
                    .arg(static_cast<unsigned>(moduleParts.suffixLen))
                    .arg(moduleParts.hasDynamic));
#endif
        }
    }
}

void MVLC_StreamWorker::start()
{
    {
        std::unique_lock<std::mutex> guard(m_stateMutex);

        if (m_state != WorkerState::Idle)
        {
            logError("worker state != Idle, ignoring request to start");
            return;
        }
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
        UniqueLock guard(m_parserCountersMutex);
        m_parser = make_readout_parser(collect_readout_scripts(*vmeConfig));
        m_parserCountersCopy = m_parser.counters;
        logParserInfo(m_parser);
    }
    catch (const std::exception &e)
    {
        logError(QSL("Error setting up MVLC stream parser: %1")
                 .arg(e.what()));
        emit stopped();
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

    // Notify the world that we're up and running.
    setState(WorkerState::Running);

    // Immediately go into paused state.
    if (m_startPaused)
        setState(WorkerState::Paused);

    TimetickGenerator timetickGen;

    while (true)
    {
        WorkerState state = {};
        WorkerState desiredState = {};

        {
            std::unique_lock<std::mutex> guard(m_stateMutex);
            state = m_state;
            desiredState = m_desiredState;
        }

        // running
        if (likely(desiredState == WorkerState::Running
                   || desiredState == WorkerState::Paused
                   || desiredState == WorkerState::SingleStepping))
        {
            static const unsigned FullBufferWaitTime_ms = 100;

            if (auto buffer = dequeue(m_fullBuffers, FullBufferWaitTime_ms))
                processBuffer(buffer, runInfo, vmeConfig, analysis);
        }
        // stopping
        else if (desiredState == WorkerState::Idle)
        {
            if (m_stopFlag == StopImmediately)
            {
                qDebug() << __PRETTY_FUNCTION__ << "immediate stop, buffers left in queue:" <<
                    queue_size(m_fullBuffers);

                break;
            }

            // The StopWhenQueueEmpty case
            if (auto buffer = dequeue(m_fullBuffers))
                processBuffer(buffer, runInfo, vmeConfig, analysis);
            else
                break;
        }
        else
        {
            qDebug() << __PRETTY_FUNCTION__
                << "state=" << to_string(state)
                << ", desiredState=" << to_string(desiredState);
            InvalidCodePath;
        }

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

    // analysis session auto save
    auto sessionPath = m_context->getWorkspacePath(QSL("SessionDirectory"));

    if (!sessionPath.isEmpty())
    {
        auto filename = sessionPath + "/last_session" + analysis::SessionFileExtension;
        auto result   = save_analysis_session(filename, m_context->getAnalysis());

        if (result.first)
        {
            //logInfo(QString("Auto saved analysis session to %1").arg(filename));
        }
        else
        {
            logInfo(QString("Error saving analysis session to %1: %2")
                       .arg(filename)
                       .arg(result.second));
        }
    }

    setState(WorkerState::Idle);
}

void MVLC_StreamWorker::blockIfPaused()
{
    auto predicate = [this] ()
    {
        WorkerState desiredState = m_desiredState;
        //qDebug() << "predicate executing; desiredState=" << to_string(desiredState);
        return desiredState == WorkerState::Running
            || desiredState == WorkerState::Idle
            || desiredState == WorkerState::SingleStepping;
    };

    //qDebug() << "MVLCStreamWorker beginEvent pre lock";
    std::unique_lock<std::mutex> guard(m_stateMutex);

    // Transition from any state into paused
    if (m_desiredState == WorkerState::Paused && m_state != WorkerState::Paused)
    {
        m_state = WorkerState::Paused;
        emit stateChanged(m_state);
    }
    else if (m_desiredState == WorkerState::Running && m_state != WorkerState::Running)
    {
        m_state = WorkerState::Running;
        emit stateChanged(m_state);
    }


    // Block until the predicate becomes true. This means the user wants to
    // stop the run, resume from paused or step one event before pausing
    // again.
    //qDebug() << "MVLCStreamWorker beginEvent pre wait";
    m_stateCondVar.wait(guard, predicate);
    //qDebug() << "MVLCStreamWorker beginEvent post wait";

    if (m_desiredState == WorkerState::SingleStepping)
    {
        m_state = WorkerState::SingleStepping;
        m_desiredState = WorkerState::Paused;
        emit stateChanged(m_state);
    }
}

void MVLC_StreamWorker::publishStateIfSingleStepping()
{
    std::unique_lock<std::mutex> guard(m_stateMutex);
    if (m_state == WorkerState::SingleStepping)
    {
        emit singleStepResultReady(m_singleStepEventRecord);
    }
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

        {
            // Create a copy of the current parser counters after each buffer.
            // From a performance point this is not ideal as the UI currently
            // only pulls the counters once per second.
            UniqueLock guard(m_parserCountersMutex);
            m_parserCountersCopy = m_parser.counters;
        }

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
    {
        std::unique_lock<std::mutex> guard(m_stateMutex);
        m_stopFlag = (whenQueueEmpty ? StopWhenQueueEmpty : StopImmediately);
        m_desiredState = MVMEStreamWorkerState::Idle;
    }
    m_stateCondVar.notify_one();
}

void MVLC_StreamWorker::pause()
{
    qDebug() << __PRETTY_FUNCTION__ << "enter";
    {
        std::unique_lock<std::mutex> guard(m_stateMutex);
        m_desiredState = MVMEStreamWorkerState::Paused;
    }
    m_stateCondVar.notify_one();
    qDebug() << __PRETTY_FUNCTION__ << "leave";
}

void MVLC_StreamWorker::resume()
{
    qDebug() << __PRETTY_FUNCTION__ << "enter";
    {
        std::unique_lock<std::mutex> guard(m_stateMutex);
        m_desiredState = MVMEStreamWorkerState::Running;
        m_startPaused = false;
    }
    m_stateCondVar.notify_one();
    qDebug() << __PRETTY_FUNCTION__ << "leave";
}

void MVLC_StreamWorker::singleStep()
{
    qDebug() << __PRETTY_FUNCTION__ << "enter";
    {
        std::unique_lock<std::mutex> guard(m_stateMutex);
        m_desiredState = MVMEStreamWorkerState::SingleStepping;
    }
    m_stateCondVar.notify_one();
    qDebug() << __PRETTY_FUNCTION__ << "leave";
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
