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

#include "analysis/a2/a2_data_filter.h"
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
using namespace mesytec;

using WorkerState = AnalysisWorkerState;

mvme_mvlc::VMEConfReadoutScripts collect_readout_scripts(const VMEConfig &vmeConfig)
{
    mvme_mvlc::VMEConfReadoutScripts readoutScripts;

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
    EventRecord &record, int moduleIndex, const u32 *data, u32 size)
{
    if (record.modulesData.size() <= moduleIndex)
        record.modulesData.resize(moduleIndex + 1);

    QVector<u32> *dest = nullptr;

    dest = &record.modulesData[moduleIndex].data;

    assert(dest);

    std::copy(data, data + size, std::back_inserter(*dest));
}

bool is_empty(const EventRecord::ModuleData &moduleData)
{
    return moduleData.data.isEmpty();
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
, m_parserCountersSnapshot()
, m_state(AnalysisWorkerState::Idle)
, m_desiredState(AnalysisWorkerState::Idle)
, m_startPaused(false)
, m_stopFlag(StopWhenQueueEmpty)
, m_debugInfoRequest(DebugInfoRequest::None)
, m_eventBuilder({})
{
    qRegisterMetaType<mesytec::mvlc::readout_parser::ReadoutParserState>(
        "mesytec::mvlc::readout_parser::ReadoutParserState");

    qRegisterMetaType<mesytec::mvlc::readout_parser::ReadoutParserCounters>(
        "mesytec::mvlc::readout_parser::ReadoutParserCounters");

    auto logger = mesytec::mvlc::get_logger("mvlc_stream_worker");
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
    auto logger = mesytec::mvlc::get_logger("mvlc_stream_worker");

    // Last part of the eventData callback chain, calling into the analysis.
    auto eventData_analysis = [this, analysis, logger] (
        void * /*userContext*/,
        int /*crateIndex*/,
        int ei,
        const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
        unsigned moduleCount)
    {
        static const char *lambdaName = "eventData_analysis"; (void) lambdaName;
        logger->trace("f={}, ei={}, moduleData={}, moduleCount={}", lambdaName, ei,
                      reinterpret_cast<const void *>(moduleDataList), moduleCount);

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
            auto &moduleData = moduleDataList[parserModuleIndex];
            int mi = m_eventModuleIndexMaps[ei][parserModuleIndex];

            if (moduleData.data.size)
            {
                analysis->processModuleData(
                    ei, mi, moduleData.data.data, moduleData.data.size);

                for (auto c: m_moduleConsumers)
                    c->processModuleData(
                        ei, mi, moduleData.data.data, moduleData.data.size);

                if (m_diag)
                    m_diag->processModuleData(
                        ei, mi, moduleData.data.data, moduleData.data.size);

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

    // Last part of the systemEvent callback chain, calling into the analysis.
    auto systemEvent_analysis = [this, runInfo, analysis, logger](
        void *, int /*crateIndex*/, const u32 *header, u32 size)
    {
        static const char *lambdaName = "systemEvent_analysis"; (void) lambdaName;
        logger->trace("f={}, header={}, size={}", lambdaName,
                      reinterpret_cast<const void *>(header), size);

        if (!size)
            return;

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

    // Potential middle part of the eventData chain. Calls into to multi event splitter.
    auto eventData_splitter = [this, logger] (
        void *userContext,
        int /*crateIndex*/,
        int ei,
        const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
        unsigned moduleCount)
    {
        static const char *lambdaName = "systemEvent_splitter"; (void) lambdaName;
        logger->trace("f={}, ei={}, moduleData={}, moduleCount={}", lambdaName, ei,
                      reinterpret_cast<const void *>(moduleDataList), moduleCount);

        mvme::multi_event_splitter::event_data(
            m_multiEventSplitter, m_multiEventSplitterCallbacks,
            userContext, ei, moduleDataList, moduleCount);
    };

    static const int crateIndex = 0;
    static const bool alwaysFlushEventBuilder = false;

    // Potential middle part of the eventData chain. Calls into to event builder.
    auto eventData_builder = [this, logger] (
        void * /*userContext*/,
        int /*crateIndex*/,
        int ei,
        const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
        unsigned moduleCount)
    {
        static const char *lambdaName = "eventData_builder"; (void) lambdaName;
        logger->trace("f={}, ei={}, moduleData={}, moduleCount={}", lambdaName, ei,
                      reinterpret_cast<const void *>(moduleDataList), moduleCount);

        m_eventBuilder.recordEventData(crateIndex, ei, moduleDataList, moduleCount);
        m_eventBuilder.buildEvents(m_eventBuilderCallbacks, alwaysFlushEventBuilder);
    };

    // Potential middle part of the systemEvent chain. Calls into to event builder.
    auto systemEvent_builder = [this, logger] (
        void * /*userContext*/,
        int /*crateIndex*/,
        const u32 *header,
        u32 size)
    {
        static const char *lambdaName = "systemEvent_builder"; (void) lambdaName;
        logger->trace("f={}, header={}, size={}", lambdaName,
                      reinterpret_cast<const void *>(header), size);

        // Note: we could directory call systemEvent_analysis here as we are in
        // the same thread as the analysis. Otherwise the analysis would live
        // in the builder/analysis thread and buildEvents() would be called in
        // that thread.
        m_eventBuilder.recordSystemEvent(crateIndex, header, size);
        m_eventBuilder.buildEvents(m_eventBuilderCallbacks, alwaysFlushEventBuilder);
    };

    // event builder setup
    // if used the chain is event_builder -> analysis
    if (uses_event_builder(*vmeConfig, *analysis))
    {
        auto eventConfigs = vmeConfig->getEventConfigs();
        std::vector<mesytec::mvlc::EventSetup> eventSetups;

        for (auto eventIndex = 0; eventIndex < eventConfigs.size(); ++eventIndex)
        {
            auto eventConfig = eventConfigs.at(eventIndex);
            auto eventSettings = analysis->getVMEObjectSettings(eventConfig->getId());
            bool enabledForEvent = eventSettings["EventBuilderEnabled"].toBool();

            auto ebSettings = eventSettings["EventBuilderSettings"].toMap();
            auto mainModuleId = ebSettings["MainModule"].toUuid();

            mesytec::mvlc::EventSetup eventSetup = {};
            eventSetup.enabled = enabledForEvent;

            if (eventSetup.enabled)
            {
                auto moduleConfigs = eventConfig->getModuleConfigs();
                auto matchWindows = ebSettings["MatchWindows"].toMap();

                mesytec::mvlc::EventSetup::CrateSetup crateSetup;

                for (int moduleIndex = 0; moduleIndex < moduleConfigs.size(); ++moduleIndex)
                {
                    auto moduleConfig = moduleConfigs.at(moduleIndex);

                    if (moduleConfig->getId() == mainModuleId)
                        eventSetup.mainModule = std::make_pair(crateIndex, moduleIndex);

                    auto windowSettings = matchWindows[moduleConfig->getId().toString()].toMap();

                    auto matchWindow = std::make_pair<s32, s32>(
                        windowSettings.value("lower", mesytec::mvlc::event_builder::DefaultMatchWindow.first).toInt(),
                        windowSettings.value("upper", mesytec::mvlc::event_builder::DefaultMatchWindow.second).toInt());

                    crateSetup.moduleMatchWindows.push_back(matchWindow);

                    bool moduleIgnored = windowSettings.value("ignoreModule", false).toBool();

                    if (!moduleIgnored)
                    {
                        crateSetup.moduleTimestampExtractors.push_back(
                            mesytec::mvlc::IndexedTimestampFilterExtractor(
                                a2::data_filter::make_filter("11DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"), -1, 'D'));
                    }
                    else
                    {
                        // This extractor always produces an invalid timestamp
                        // no matter the input data.
                        crateSetup.moduleTimestampExtractors.push_back(
                            mesytec::mvlc::InvalidTimestampExtractor());
                    }
                }

                eventSetup.crateSetups = { crateSetup };
            }

            eventSetups.push_back(eventSetup);
        }

        mesytec::mvlc::EventBuilderConfig cfg;
        cfg.setups = eventSetups;
        m_eventBuilder = mesytec::mvlc::EventBuilder(cfg);
        // event builder -> analysis
        m_eventBuilderCallbacks.eventData = eventData_analysis;
        m_eventBuilderCallbacks.systemEvent = systemEvent_analysis;
    }
    else
    {
        m_eventBuilder = mesytec::mvlc::EventBuilder({});
    }

    // multi event splitter setup
    // if used the chain is splitter [-> event_builder] -> analysis
    if (uses_multi_event_splitting(*vmeConfig, *analysis))
    {
        namespace multi_event_splitter = ::mvme::multi_event_splitter;

        auto filterStrings = collect_multi_event_splitter_filter_strings(
            *vmeConfig, *analysis);

        logInfo("enabling multi_event_splitter");

        m_multiEventSplitter = multi_event_splitter::make_splitter(filterStrings);

        if (uses_event_builder(*vmeConfig, *analysis))
        {
            // splitter -> event builder
            m_multiEventSplitterCallbacks.eventData = eventData_builder;
        }
        else
        {
            // splitter -> analysis
            m_multiEventSplitterCallbacks.eventData = eventData_analysis;
        }
    }

    // readout parser callback setup
    if (uses_event_builder(*vmeConfig, *analysis))
    {
        // parser -> event builder
        m_parserCallbacks.eventData = eventData_builder;
        m_parserCallbacks.systemEvent = systemEvent_builder;
    }
    else
    {
        // parser -> analysis
        m_parserCallbacks.eventData = eventData_analysis;
        m_parserCallbacks.systemEvent = systemEvent_analysis;
    }

    if (uses_multi_event_splitting(*vmeConfig, *analysis))
    {
        // parser -> splitter
        m_parserCallbacks.eventData = eventData_splitter;
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
#if 0
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

        auto logger = mesytec::mvlc::get_logger("mvlc_stream_worker");

        logger->trace("VmeConfig -> CrateConfig result:\n{}", to_yaml(mvlcCrateConfig));

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

            for (const auto &srcGroup: srcStack.getGroups())
            {
                if (mvlc::produces_output(srcGroup))
                    dstStack.addGroup(srcGroup);
            }

            logger->trace("produces output: originalStack: {}, sanitizedStack: {}",
                          produces_output(srcStack), produces_output(dstStack));

            sanitizedReadoutStacks.emplace_back(dstStack);
        }

        m_parser = mesytec::mvlc::readout_parser::make_readout_parser(
            sanitizedReadoutStacks);

        if (logger->level() == spdlog::level::trace)
        {
            logger->trace("begin parser readout structure:");

            for (size_t ei=0; ei<m_parser.readoutStructure.size(); ++ei)
            {
                const auto &eventStructure = m_parser.readoutStructure[ei];

                for (size_t mi=0; mi<eventStructure.size(); ++mi)
                {
                    const auto &moduleStructure = eventStructure[mi];

                    logger->trace("  ei={}, mi={}: len={}",
                                  ei, mi, moduleStructure.len);
                }
            }

            logger->trace("end parser readout structure:");
        }

        // Reset the parser counters and the snapshot copy
        m_parserCounters = {};
        m_parserCountersSnapshot.access().ref() = m_parserCounters;
        logParserInfo(m_parser);
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
            auto maybe_flush_event_builder = [this] ()
            {
                // Flush the event builder if it is used
                if (m_eventBuilder.isEnabledForAnyEvent())
                {
                    auto logger = mesytec::mvlc::get_logger("mvlc_stream_worker");
                    logger->info("flushing event builder");
                    m_eventBuilder.buildEvents(m_eventBuilderCallbacks, true);
                }
            };

            if (m_stopFlag == StopImmediately)
            {
                qDebug() << __PRETTY_FUNCTION__ << "immediate stop, buffers left in queue:" <<
                    filled.size();

                // Move the remaining buffers to the empty queue.
                while (auto buffer = filled.dequeue())
                    empty.enqueue(buffer);

                maybe_flush_event_builder();

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
            {
                maybe_flush_event_builder();
                break;
            }
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

namespace
{
    bool is_timetick_only_buffer(const nonstd::basic_string_view<const u32> &buffer)
    {
        using namespace mesytec::mvlc;

        if (buffer.empty())
            return false;

        u32 frameHeader = buffer.at(0);
        auto frameInfo =  extract_frame_info(frameHeader);

        if (frameInfo.type != frame_headers::SystemEvent)
            return false;

        u8 subtype = system_event::extract_subtype(frameHeader);

        if (subtype != system_event::subtype::UnixTimetick)
            return false;

        // Test if this is the only frame in the buffer
        return buffer.size() == frameInfo.len + 1u;
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
    const bool useLogThrottle = true;

    try
    {
        ParseResult pr = readout_parser::parse_readout_buffer(
            static_cast<ConnectionType>(buffer->type()),
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
                useLogThrottle);
        exceptionSeen = true;
    }
    catch (const std::exception &e)
    {
        logWarn(QSL("exception (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->bufferNumber()),
                useLogThrottle);
        exceptionSeen = true;
    }
    catch (...)
    {
        logWarn(QSL("unknown exception when parsing buffer #%1")
                .arg(buffer->bufferNumber()),
                useLogThrottle);
        exceptionSeen = true;
    }

    if (exceptionSeen)
        qDebug() << __PRETTY_FUNCTION__ << "exception seen";

    if (debugRequest == DebugInfoRequest::OnNextBuffer
        || (debugRequest == DebugInfoRequest::OnNextBufferIgnoreTimeticks
            && !is_timetick_only_buffer(bufferView))
        || (debugRequest == DebugInfoRequest::OnNextError
            && !processingOk))
    {
        m_debugInfoRequest = DebugInfoRequest::None;

        DataBuffer bufferCopy(bufferView.size() * sizeof(u32));

        std::copy(std::begin(bufferView), std::end(bufferView),
                  bufferCopy.asU32());

        bufferCopy.used = bufferView.size() * sizeof(u32);
        bufferCopy.tag = static_cast<int>(static_cast<ConnectionType>(buffer->type()) == ConnectionType::ETH
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
