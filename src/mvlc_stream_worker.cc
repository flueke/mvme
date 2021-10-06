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
#include "mvlc_stream_worker.h"

#include <algorithm>
#include <mutex>
#include <QThread>

#include "analysis/analysis_util.h"
#include "analysis/analysis_session.h"
#include "databuffer.h"
#include "mesytec-mvlc/mvlc_command_builders.h"
#include "mvme_context.h"
#include "vme_config_scripts.h"
#include "vme_analysis_common.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "vme_script.h"

using namespace vme_analysis_common;
using namespace mvme;
using namespace mesytec;
using namespace mesytec::mvme_mvlc;

using WorkerState = AnalysisWorkerState;

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
    mesytec::mvlc::ReadoutBufferQueues &snoopQueues,
    QObject *parent)
: StreamWorkerBase(parent)
, m_context(context)
, m_snoopQueues(snoopQueues)
, m_parserCounters({})
, m_parserCountersSnapshot({})
, m_state(AnalysisWorkerState::Idle)
, m_desiredState(AnalysisWorkerState::Idle)
, m_startPaused(false)
, m_stopFlag(StopWhenQueueEmpty)
, m_debugInfoRequest(DebugInfoRequest::None)
{
    qRegisterMetaType<mesytec::mvlc::readout_parser::ReadoutParserState>(
        "mesytec::mvlc::readout_parser::ReadoutParserState");

    qRegisterMetaType<mesytec::mvlc::readout_parser::ReadoutParserCounters>(
        "mesytec::mvlc::readout_parser::ReadoutParserCounters");
}

MVLC_StreamWorker::~MVLC_StreamWorker()
{
}

void MVLC_StreamWorker::setState(AnalysisWorkerState newState)
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
        case AnalysisWorkerState::Idle:
            emit stopped();
            break;

        case AnalysisWorkerState::Running:
            emit started();
            break;

        case AnalysisWorkerState::Paused:
        case AnalysisWorkerState::SingleStepping:
            break;
    }
}

void MVLC_StreamWorker::fillModuleIndexMaps(const VMEConfig *vmeConfig)
{
    m_eventModuleIndexMaps.fill({});

    auto events = vmeConfig->getEventConfigs();

    for (int ei=0; ei<events.size(); ++ei)
    {
        auto modules = vmeConfig->getAllModuleConfigs();
        auto mapIter = m_eventModuleIndexMaps[ei].begin();
        const auto mapEnd = m_eventModuleIndexMaps[ei].end();

        for (int mi=0; mi<modules.size(); ++mi)
        {
            if (modules.at(mi)->isEnabled() && mapIter != mapEnd)
                *mapIter++ = mi;
        }
    }

#if 0
    for (int ei=0; ei<events.size(); ++ei)
    {
        qDebug() << __PRETTY_FUNCTION__ << "moduleIndexMap for event" << ei << ":";

        for (unsigned pim=0; pim<m_eventModuleIndexMaps[ei].size(); ++pim)
        {
            qDebug() << "   " << __PRETTY_FUNCTION__ << "parserModuleIndex=" << pim
                << " -> mvmeModuleIndex=" << m_eventModuleIndexMaps[ei][pim];
        }
    }
#endif
}

void MVLC_StreamWorker::setupParserCallbacks(
    const RunInfo &runInfo,
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis)
{

    m_parserCallbacks = mesytec::mvlc::readout_parser::ReadoutParserCallbacks();

    m_parserCallbacks.eventData = [this, analysis] (
        void * /*userContext*/,
        int ei,
        const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
        unsigned moduleCount)
    {
        // beginEvent
        {
            this->blockIfPaused();

            analysis->beginEvent(ei);

            for (auto c: m_moduleConsumers)
                c->beginEvent(ei);

            if (m_state == WorkerState::SingleStepping)
                begin_event_record(m_singleStepEventRecord, ei);

            if (m_diag)
                m_diag->beginEvent(ei);
        }

        // eventData
        for (unsigned parserModuleIndex=0; parserModuleIndex<moduleCount; ++parserModuleIndex)
        {
            auto moduleParts = m_parser.readoutStructure[ei][parserModuleIndex];
            auto &moduleData = moduleDataList[parserModuleIndex];
            int mi = m_eventModuleIndexMaps[ei][parserModuleIndex];

            // prefix
            if (moduleData.prefix.size)
            {
                // FIXME: Hack checking if the module does not have a dynamic part. In
                // this case the readout data is handed to the analysis via
                // processModuleData(). This workaround makes the MVLC readout
                // compatible to readouts with the older controllers.
                // Once the analysis is updated and proper filter templates for
                // prefix/suffix have been added this change should be removed!
                // Note: this works for scripts containing only register reads, e.g.
                // the standard MesytecCounter script.
                if (!moduleParts.hasDynamic)
                {
                    analysis->processModuleData(
                        ei, mi, moduleData.prefix.data, moduleData.prefix.size);

                    for (auto c: m_moduleConsumers)
                        c->processModuleData(
                            ei, mi, moduleData.prefix.data, moduleData.prefix.size);

                    if (m_diag)
                        m_diag->processModuleData(
                            ei, mi, moduleData.prefix.data, moduleData.prefix.size);
                }
                else
                {
                    analysis->processModulePrefix(
                        ei, mi, moduleData.prefix.data, moduleData.prefix.size);
                }
            }

            // dynamic
            if (moduleParts.hasDynamic)
            {
                analysis->processModuleData(
                    ei, mi, moduleData.dynamic.data, moduleData.dynamic.size);

                for (auto c: m_moduleConsumers)
                    c->processModuleData(
                        ei, mi, moduleData.dynamic.data, moduleData.dynamic.size);

                if (m_diag)
                    m_diag->processModuleData(
                        ei, mi, moduleData.dynamic.data, moduleData.dynamic.size);
            }

            // suffix
            if (moduleData.suffix.size)
                analysis->processModuleSuffix(
                    ei, mi, moduleData.suffix.data, moduleData.suffix.size);

            if (m_state == WorkerState::SingleStepping)
            {
                record_module_part(
                    m_singleStepEventRecord, EventRecord::Prefix,
                    mi, moduleData.prefix.data, moduleData.prefix.size);

                record_module_part(
                    m_singleStepEventRecord, EventRecord::Dynamic,
                    mi, moduleData.dynamic.data, moduleData.dynamic.size);

                record_module_part(
                    m_singleStepEventRecord, EventRecord::Suffix,
                    mi, moduleData.suffix.data, moduleData.suffix.size);
            }

            if (moduleData.prefix.size || moduleData.dynamic.size || moduleData.suffix.size)
            {
                UniqueLock guard(m_countersMutex);
                m_counters.moduleCounters[ei][mi]++;
            }
        }

        // endEvent
        {
            analysis->endEvent(ei);

            for (auto c: m_moduleConsumers)
            {
                c->endEvent(ei);
            }

            if (m_diag)
                m_diag->endEvent(ei);

            if (0 <= ei && ei < MaxVMEEvents)
            {
                UniqueLock guard(m_countersMutex);
                m_counters.totalEvents++;
                m_counters.eventCounters[ei]++;
            }

            this->publishStateIfSingleStepping();
        }
    };

    m_parserCallbacks.systemEvent = [this, runInfo, analysis](
        void *, const u32 *header, u32 /*size*/)
    {
        u8 subtype = mvlc::system_event::extract_subtype(*header);

        // IMPORTANT: This assumes that a timestamp is added to the listfile
        // every 1 second. Jitter is not taken into account and the actual
        // timestamp value is not used at the moment.

        // For replays the timeticks are contained in the incoming data
        // buffers.  For live daq runs timeticks are generated in start() using
        // a TimetickGenerator. This has to happen on the analysis side  due to
        // the possibility of having internal buffer loss and thus potentially
        // missing timeticks.
        // TODO extract timestamp from the UnixTimetick event, calculate a
        // delta time and pass it to the analysis.
        if (runInfo.isReplay && subtype == mvlc::system_event::subtype::UnixTimetick)
        {
            analysis->processTimetick();

            for (auto c: m_moduleConsumers)
            {
                c->processTimetick();
            }
        }
    };

    const auto eventConfigs = vmeConfig->getEventConfigs();

    // Setup multi event splitting if needed. The processing chain then looks
    // like this: readout_parser -> multi_event_splitter -> call into the analysis
    if (uses_multi_event_splitting(*vmeConfig, *analysis))
    {
        namespace multi_event_splitter = ::mvme::multi_event_splitter;

        auto filterStrings = collect_multi_event_splitter_filter_strings(
            *vmeConfig, *analysis);

        logInfo("enabling multi_event_splitter");

        m_multiEventSplitter = multi_event_splitter::make_splitter(filterStrings);

        // Copy our callback, which is driving the analysis, to the callback
        // for the multi event splitter.
        auto &splitterCallbacks = m_multiEventSplitterCallbacks;
        splitterCallbacks.eventData = m_parserCallbacks.eventData;

        // Now overwrite our own callbacks to drive the splitter instead of the
        // analysis.
        // Note: the systemEvent callback is not overwritten as there is no
        // special handling for it in the multi event splitting logic.
        m_parserCallbacks.eventData = [this] (
            void *userContext,
            int ei,
            const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
            unsigned moduleCount)
        {
            multi_event_splitter::event_data(
                m_multiEventSplitter, m_multiEventSplitterCallbacks,
                userContext, ei, moduleDataList, moduleCount);
        };
    }

    // Event builder setup would go here. The processing chain then looks like this:
    // readout_parser -> [multi_event_splitter ->] event_builder -> call into the analysis

    auto is_event_builder_enabled = [] (const auto &vmeConfig, const auto &analysis)
    {
        return true;
    };

    if (is_event_builder_enabled(*vmeConfig, *analysis))
    {
        // TODO: create and setup the event builder. It needs the reference
        // module and a timestamp interval relative to the reference module for
        // each of the other modules in the same event.

        static const int crateIndex = 0;

        if (uses_multi_event_splitting(*vmeConfig, *analysis))
        {
            m_eventBuilderCallbacks.eventData = m_multiEventSplitterCallbacks.eventData;

            m_multiEventSplitterCallbacks.eventData = [this] (
                void *userContext,
                int eventIndex,
                const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
                unsigned moduleCount)
            {
                m_eventBuilder.pushEventData(userContext, crateIndex, eventIndex, moduleDataList, moduleCount);
            };
        }
        else
        {
            m_eventBuilderCallbacks.eventData = m_parserCallbacks.eventData;

            m_parserCallbacks.eventData = [this] (
                void *userContext,
                int eventIndex,
                const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
                unsigned moduleCount)
            {
                m_eventBuilder.pushEventData(userContext, crateIndex, eventIndex, moduleDataList, moduleCount);
                m_eventBuilder.buildEvents(m_eventBuilderCallbacks);
            };
        }

        m_eventBuilderCallbacks.systemEvent = m_parserCallbacks.systemEvent;

        m_parserCallbacks.systemEvent = [this] (
            void *userContext,
            const u32 *data,
            u32 size)
        {
            m_eventBuilder.pushSystemEvent(userContext, crateIndex, data, size);
        };
    }
}

void MVLC_StreamWorker::logParserInfo(
    const mesytec::mvlc::readout_parser::ReadoutParserState &parser)
{
    auto &readoutInfo = parser.readoutStructure;

    for (size_t eventIndex=0; eventIndex<readoutInfo.size(); eventIndex++)
    {
        const auto &modules = readoutInfo[eventIndex];

        for (size_t moduleIndex=0; moduleIndex<modules.size(); moduleIndex++)
        {
#if 1
            const auto &moduleParts = modules[moduleIndex];

            logInfo(QString("mvlc readout parser info: eventIndex=%1"
                            ", moduleIndex=%2: prefixLen=%3, suffixLen=%4, hasDynamic=%5")
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

    fillModuleIndexMaps(vmeConfig);
    setupParserCallbacks(runInfo, vmeConfig, analysis);

    try
    {
        auto mvlcCrateConfig = mesytec::mvme::vmeconfig_to_crateconfig(vmeConfig);

        // Removes non-output-producing command groups from each of the readout
        // stacks. This is done because the converted CrateConfig contains
        // groups for the "Cycle Start" and "Cycle End" event scripts which do
        // not produce any output. Having a Cycle Start script (called
        // "readout_start" in the CrateConfig) will confuse the readout parser
        // because the readout stack group indexes and the mvme module indexes
        // won't match up.
        std::vector<mvlc::StackCommandBuilder> sanitizedReadoutStacks;

        for (auto &srcStack: mvlcCrateConfig.stacks)
        {
            mvlc::StackCommandBuilder dstStack;

            for (auto &srcGroup: srcStack.getGroups())
            {
                if (mvlc::produces_output(srcGroup))
                    dstStack.addGroup(srcGroup);
            }

            sanitizedReadoutStacks.emplace_back(dstStack);
        }

        m_parser = mesytec::mvlc::readout_parser::make_readout_parser(
            sanitizedReadoutStacks);

        // Reset the parser counters and the snapshot copy
        m_parserCounters = {};
        m_parserCountersSnapshot.access().ref() = m_parserCounters;
        //logParserInfo(m_parser);
    }
    catch (const vme_script::ParseError &e)
    {
        logError(QSL("Error setting up MVLC stream parser: %1")
                 .arg(e.toString()));
        emit stopped();
        return;
    }
    catch (const std::exception &e)
    {
        logError(QSL("Error setting up MVLC stream parser: %1")
                 .arg(e.what()));
        emit stopped();
        return;
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

    auto &filled = m_snoopQueues.filledBufferQueue();
    auto &empty = m_snoopQueues.emptyBufferQueue();

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
            auto buffer = filled.dequeue(std::chrono::milliseconds(100));

            if (buffer && buffer->empty()) // sentinel
                break;
            else if (buffer)
            {
                try
                {
                    processBuffer(buffer, vmeConfig, analysis);
                    empty.enqueue(buffer);
                    m_parserCountersSnapshot.access().ref() = m_parserCounters;
                }
                catch (...)
                {
                    empty.enqueue(buffer);
                    throw;
                }
            }
        }
        // stopping
        else if (desiredState == WorkerState::Idle)
        {
            if (m_stopFlag == StopImmediately)
            {
                qDebug() << __PRETTY_FUNCTION__ << "immediate stop, buffers left in queue:" <<
                    filled.size();

                // Move the remaining buffers to the empty queue.
                while (auto buffer = filled.dequeue())
                    empty.enqueue(buffer);

                break;
            }

            // The StopWhenQueueEmpty case
            if (auto buffer = filled.dequeue())
            {
                try
                {
                    processBuffer(buffer, vmeConfig, analysis);
                    empty.enqueue(buffer);
                    m_parserCountersSnapshot.access().ref() = m_parserCounters;
                }
                catch (...)
                {
                    empty.enqueue(buffer);
                    throw;
                }
            }
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
    const mesytec::mvlc::ReadoutBuffer *buffer,
    const VMEConfig *vmeConfig,
    const analysis::Analysis *analysis)
{
    using namespace mesytec::mvlc;
    using namespace mesytec::mvlc::readout_parser;

    DebugInfoRequest debugRequest = m_debugInfoRequest;
    ReadoutParserState debugSavedParserState;
    ReadoutParserCounters debugSavedParserCounters;

    // If debug info was requested create a copy of the parser state before
    // attempting to parse the input buffer.
    if (debugRequest != DebugInfoRequest::None)
    {
        debugSavedParserState = m_parser;
        debugSavedParserCounters = m_parserCounters;
    }

    bool processingOk = false;
    bool exceptionSeen = false;

    auto bufferView = buffer->viewU32();

    try
    {
        ParseResult pr = readout_parser::parse_readout_buffer(
            buffer->type(),
            m_parser,
            m_parserCallbacks,
            m_parserCounters,
            buffer->bufferNumber(),
            bufferView.data(),
            bufferView.size());

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
                .arg(buffer->bufferNumber()),
                true);
        exceptionSeen = true;
    }
    catch (const std::exception &e)
    {
        logWarn(QSL("exception (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->bufferNumber()),
                true);
        exceptionSeen = true;
    }
    catch (...)
    {
        logWarn(QSL("unknown exception when parsing buffer #%1")
                .arg(buffer->bufferNumber()),
                true);
        exceptionSeen = true;
    }

    if (exceptionSeen)
        qDebug() << __PRETTY_FUNCTION__ << "exception seen";

    if (debugRequest == DebugInfoRequest::OnNextBuffer
        || (debugRequest == DebugInfoRequest::OnNextError && !processingOk))
    {
        m_debugInfoRequest = DebugInfoRequest::None;

        DataBuffer bufferCopy(bufferView.size() * sizeof(u32));

        std::copy(std::begin(bufferView), std::end(bufferView),
                  bufferCopy.asU32());

        bufferCopy.used = bufferView.size() * sizeof(u32);
        bufferCopy.tag = static_cast<int>(buffer->type() == ConnectionType::ETH
                                          ? ListfileBufferFormat::MVLC_ETH
                                          : ListfileBufferFormat::MVLC_USB);
        bufferCopy.id = buffer->bufferNumber();

        emit debugInfoReady(
            bufferCopy,
            debugSavedParserState,
            debugSavedParserCounters,
            vmeConfig,
            analysis);
    }

    {
        UniqueLock guard(m_countersMutex);
        m_counters.bytesProcessed += buffer->used();
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
        m_desiredState = AnalysisWorkerState::Idle;
    }
    m_stateCondVar.notify_one();
}

void MVLC_StreamWorker::pause()
{
    qDebug() << __PRETTY_FUNCTION__ << "enter";
    {
        std::unique_lock<std::mutex> guard(m_stateMutex);
        m_desiredState = AnalysisWorkerState::Paused;
    }
    m_stateCondVar.notify_one();
    qDebug() << __PRETTY_FUNCTION__ << "leave";
}

void MVLC_StreamWorker::resume()
{
    qDebug() << __PRETTY_FUNCTION__ << "enter";
    {
        std::unique_lock<std::mutex> guard(m_stateMutex);
        m_desiredState = AnalysisWorkerState::Running;
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
        m_desiredState = AnalysisWorkerState::SingleStepping;
    }
    m_stateCondVar.notify_one();
    qDebug() << __PRETTY_FUNCTION__ << "leave";
}

void MVLC_StreamWorker::startupConsumers()
{
    for (auto c: m_moduleConsumers)
    {
        c->startup();
    }
}

void MVLC_StreamWorker::shutdownConsumers()
{
    for (auto c: m_moduleConsumers)
    {
        c->shutdown();
    }
}
