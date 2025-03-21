/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include <QCoreApplication>
#include <QThread>

#include "analysis/a2/a2_data_filter.h"
#include "analysis/analysis_util.h"
#include "analysis/analysis_session.h"
#include "databuffer.h"
#include "mesytec-mvlc/mvlc_command_builders.h"
#include "mvlc_daq.h"
#include "mvme_workspace.h"
#include "vme_config_scripts.h"
#include "vme_analysis_common.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "vme_script.h"
#include "util/perf.h"

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
MVLC_StreamWorker::MVLC_StreamWorker(mesytec::mvlc::ReadoutBufferQueues &snoopQueues, QObject *parent)
    : StreamWorkerBase(parent)
    , m_snoopQueues(snoopQueues)
    , m_parserCounters({})
    , m_parserCountersSnapshot()
    , m_state(AnalysisWorkerState::Idle)
    , m_desiredState(AnalysisWorkerState::Idle)
    , m_startPaused(false)
    , m_stopFlag(StopWhenQueueEmpty)
    , m_debugInfoRequest(DebugInfoRequest::None)
    , m_eventBuilder(mesytec::mvlc::event_builder2::EventBuilder2())
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
    m_eventModuleIndexMaps = vme_analysis_common::make_module_index_mappings(*vmeConfig);

    qDebug().noquote() << __PRETTY_FUNCTION__
        << vme_analysis_common::debug_format_module_index_mappings(m_eventModuleIndexMaps, *vmeConfig);
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
        int crateIndex,
        int eventIndex,
        const mesytec::mvlc::readout_parser::ModuleData *moduleDataList,
        unsigned moduleCount)
    {
        static const char *lambdaName = "eventData_analysis"; (void) lambdaName;
        logger->trace("f={}, eventIndex={}, moduleData={}, moduleCount={}",
                      lambdaName, eventIndex, reinterpret_cast<const void *>(moduleDataList), moduleCount);

#ifndef NDEBUG
    for (size_t mi=0; mi<moduleCount; ++mi)
    {
        assert(mvlc::readout_parser::size_consistency_check(moduleDataList[mi]));
    }
#endif

        // beginEvent
        {
            this->blockIfPaused();
            this->doArtificalDelay();

            analysis->beginEvent(eventIndex);

            for (auto c: moduleConsumers())
                c->beginEvent(eventIndex); // TODO: not needed for consumers with the newer ModuleDataList interface

            if (m_state == WorkerState::SingleStepping)
                begin_event_record(m_singleStepEventRecord, eventIndex);

            if (m_diag)
                m_diag->beginEvent(eventIndex);
        }

        // eventData
        analysis->processModuleData(crateIndex, eventIndex, moduleDataList, moduleCount);

        for (unsigned parserModuleIndex=0; parserModuleIndex<moduleCount; ++parserModuleIndex)
        {
            auto &moduleData = moduleDataList[parserModuleIndex];
            int moduleIndex = m_eventModuleIndexMaps[eventIndex][parserModuleIndex];

            if (moduleData.data.size)
            {
                if (m_diag)
                    m_diag->processModuleData(
                        eventIndex, moduleIndex, moduleData.data.data, moduleData.data.size);

                UniqueLock guard(m_countersMutex);
                m_counters.moduleCounters[eventIndex][moduleIndex]++;
            }

            if (m_state == WorkerState::SingleStepping)
            {
                record_module_part(
                    m_singleStepEventRecord, moduleIndex,
                    moduleData.data.data, moduleData.data.size);
            }
        }

        // endEvent
        {
            analysis->endEvent(eventIndex);

            // Call processModuleData _after_ the analysis has fully processed
            // the event in the case the consumer wants to use analysis data
            // itself.
            for (auto c: moduleConsumers())
                c->processModuleData(crateIndex, eventIndex, moduleDataList, moduleCount);

            for (auto c: moduleConsumers())
                c->endEvent(eventIndex);

            if (m_diag)
                m_diag->endEvent(eventIndex);

            if (0 <= eventIndex && eventIndex < MaxVMEEvents)
            {
                UniqueLock guard(m_countersMutex);
                m_counters.totalEvents++;
                m_counters.eventCounters[eventIndex]++;
            }

            this->publishStateIfSingleStepping();
        }

        {
            // Is module index mapping required when writing out the data?
            //write_event_data(outputBuffer, crateIndex, ei, moduleDataList, moduleCount);
        }
    };

    // Last part of the systemEvent callback chain, calling into the analysis.
    auto systemEvent_analysis = [this, runInfo, analysis, logger](
        void *, int crateIndex, const u32 *header, u32 size)
    {
        static const char *lambdaName = "systemEvent_analysis"; (void) lambdaName;
        logger->trace("f={}, header={}, size={}", lambdaName,
                      reinterpret_cast<const void *>(header), size);

        if (!size)
            return;

        auto frameInfo = mvlc::extract_frame_info(*header);

        // The code assumes that a timestamp is added to the listfile every 1
        // second. Jitter is not taken into account and the actual timestamp
        // value is not used at the moment.

        // For replays the timeticks are contained in the incoming data
        // buffers.  For live DAQ runs timeticks are generated in start() using
        // a TimetickGenerator. This has to happen on the analysis side due to
        // the possibility of having internal buffer loss and thus potentially
        // missing timeticks.
        // TODO extract timestamp from the UnixTimetick event, calculate a
        // delta time and pass it to the analysis.
        if (runInfo.isReplay
            && frameInfo.sysEventSubType == mvlc::system_event::subtype::UnixTimetick
            && frameInfo.ctrl == 0) // only use timeticks from the primary crate
        {
            analysis->processTimetick();

            for (auto &c: moduleConsumers())
                c->processTimetick();
        }

        for (auto &c: moduleConsumers())
            c->processSystemEvent(crateIndex, header, size);
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

        m_eventBuilder.recordModuleData(ei, moduleDataList, moduleCount);
        size_t eventsFlushed = m_eventBuilder.flush(); (void) eventsFlushed; // count these somewhere?
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

        // Note: we could directly call systemEvent_analysis here as we are in
        // the same thread as the analysis. Otherwise the analysis would live
        // in the builder/analysis thread and flush() would be called in
        // that thread.
        m_eventBuilder.handleSystemEvent(header, size);
    };

    // event builder setup
    // if used the chain is event_builder -> analysis
    if (uses_event_builder(*vmeConfig, *analysis))
    {
        auto eventConfigs = vmeConfig->getEventConfigs();
        mesytec::mvlc::event_builder2::EventBuilderConfig ebCfg;
        ebCfg.outputCrateIndex = 0;

        for (auto eventIndex = 0; eventIndex < eventConfigs.size(); ++eventIndex)
        {
            auto eventConfig = eventConfigs.at(eventIndex);
            auto eventSettings = analysis->getVMEObjectSettings(eventConfig->getId());
            bool enabledForEvent = eventSettings["EventBuilderEnabled"].toBool();
            auto ebSettings = eventSettings["EventBuilderSettings"].toMap();

            mesytec::mvlc::event_builder2::EventConfig evCfg = {};
            evCfg.enabled = enabledForEvent;
            evCfg.name = eventConfig->objectName().toStdString();

            if (true)
            {
                auto moduleConfigs = eventConfig->getModuleConfigs();
                auto matchWindows = ebSettings["MatchWindows"].toMap();

                for (int moduleIndex = 0; moduleIndex < moduleConfigs.size(); ++moduleIndex)
                {
                    auto moduleConfig = moduleConfigs.at(moduleIndex);
                    auto windowSettings = matchWindows[moduleConfig->getId().toString()].toMap();

                    mesytec::mvlc::event_builder2::ModuleConfig modCfg = {};
                    modCfg.offset = windowSettings.value("offset", mesytec::mvlc::event_builder2::DefaultMatchOffset).toInt();
                    modCfg.window = windowSettings.value("width", mesytec::mvlc::event_builder2::DefaultMatchWindow).toInt();
                    modCfg.ignored = !windowSettings.value("enableModule", false).toBool();
                    modCfg.hasDynamic = m_parser.readoutStructure.at(eventIndex).at(moduleIndex).hasDynamic;
                    modCfg.prefixSize = m_parser.readoutStructure.at(eventIndex).at(moduleIndex).prefixLen;
                    modCfg.name = m_parser.readoutStructure.at(eventIndex).at(moduleIndex).name;

                    if (!modCfg.ignored)
                        modCfg.tsExtractor = mesytec::mvlc::event_builder2::make_mesytec_default_timestamp_extractor();
                    else
                        modCfg.tsExtractor = mesytec::mvlc::event_builder2::EmptyTimestampExtractor();

                    evCfg.moduleConfigs.push_back(modCfg);
                }

                // TODO/FIXME: these settings are not event specific but global to
                // the event builder. Sadly the analysis currently stores them in
                // vme object settings for each event. Bitrot is strong here.
                using namespace mvlc::event_builder2;
                auto histoSettings = ebSettings["Histograms"].toMap();
                ebCfg.dtHistoBinning.binCount = histoSettings.value("binCount", EventBuilderConfig::DefaultHistoBins).toInt();
                ebCfg.dtHistoBinning.minValue = histoSettings.value("minValue", EventBuilderConfig::DefaultHistoMin).toInt();
                ebCfg.dtHistoBinning.maxValue = histoSettings.value("maxValue", EventBuilderConfig::DefaultHistoMax).toInt();
            }

            ebCfg.eventConfigs.push_back(evCfg);
        }

        // event builder -> analysis
        m_eventBuilderCallbacks.eventData = eventData_analysis;
        m_eventBuilderCallbacks.systemEvent = systemEvent_analysis;
        m_eventBuilder = mesytec::mvlc::event_builder2::EventBuilder2(ebCfg, m_eventBuilderCallbacks);
    }
    else
    {
        m_eventBuilder = mesytec::mvlc::event_builder2::EventBuilder2();
    }

    bool multiEventSplitterEnabled = false;

    // multi event splitter setup
    // if used the chain is splitter [-> event_builder] -> analysis
    if (uses_multi_event_splitting(*vmeConfig, *analysis))
    {
        namespace multi_event_splitter = ::mvme::multi_event_splitter;

        auto filterStrings = collect_multi_event_splitter_filter_strings(
            *vmeConfig, *analysis);

        logInfo("enabling multi_event_splitter");

        std::error_code ec;
        std::tie(m_multiEventSplitter, ec) = multi_event_splitter::make_splitter(filterStrings);

        if (ec)
            throw std::runtime_error(fmt::format("multi_event_splitter: {}", ec.message()));

        multiEventSplitterEnabled = true;

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

    if (multiEventSplitterEnabled)
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

    const auto runInfo = getRunInfo();
    const auto vmeConfig = getVMEConfig();
    auto analysis = getAnalysis();

    {
        UniqueLock guard(m_countersMutex);
        m_counters = {};
        m_counters.startTime = QDateTime::currentDateTime();
    }

    try
    {
        auto logger = mesytec::mvlc::get_logger("mvlc_stream_worker");

        auto mvlcCrateConfig = mesytec::mvme::vmeconfig_to_crateconfig(vmeConfig);
        logger->trace("VmeConfig -> CrateConfig result:\n{}", to_yaml(mvlcCrateConfig));
        // Removes non-output-producing command groups from each of the readout
        // stacks. This is done because the converted CrateConfig contains
        // groups for the "Cycle Start" and "Cycle End" event scripts, which do
        // not produce any output. Having a Cycle Start script (called
        // "readout_start" in the CrateConfig) will confuse the readout parser
        // because the readout stack group indexes and the mvme module indexes
        // won't match up.

        auto sanitizedReadoutStacks = mvme_mvlc::sanitize_readout_stacks(
            mvlcCrateConfig.stacks);

        m_parser = mesytec::mvlc::readout_parser::make_readout_parser(sanitizedReadoutStacks);

        fillModuleIndexMaps(vmeConfig);
        setupParserCallbacks(runInfo, vmeConfig, analysis);

        if (logger->level() == spdlog::level::trace)
        {
            logger->trace("begin parser readout structure:");

            for (size_t ei=0; ei<m_parser.readoutStructure.size(); ++ei)
            {
                const auto &eventStructure = m_parser.readoutStructure[ei];

                for (size_t mi=0; mi<eventStructure.size(); ++mi)
                {
                    const auto &moduleStructure = eventStructure[mi];

                    logger->trace("  ei={}, mi={}: prefixLen={}, hasDynamic={}, suffixLen={}",
                                  ei, mi, moduleStructure.prefixLen,
                                  moduleStructure.hasDynamic, moduleStructure.suffixLen);
                }
            }

            logger->trace("end parser readout structure:");
        }

        // Reset the parser counters and the snapshot copy
        m_parserCounters = {};
        m_parserCountersSnapshot.access().ref() = {};
        logParserInfo(m_parser);
    }
    catch (const vme_script::ParseError &e)
    {
        logError(QSL("Error: '%1'")
                 .arg(e.toString()));
        emit stopped();
        return;
    }
    catch (const std::exception &e)
    {
        logError(QSL("Error: '%1'")
                 .arg(e.what()));
        emit stopped();
        return;
    }

    for (auto c: moduleConsumers())
        c->beginRun(runInfo, vmeConfig, analysis);

    for (auto c: bufferConsumers())
        c->beginRun(runInfo, vmeConfig, analysis);

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
                // Do this at some point in the future and pass the shared ptr
                // to threads and be happy until one thread outlives this stream
                // worker instance! :)
                //auto bufferPtr = std::shared_ptr<mesytec::mvlc::ReadoutBuffer>(
                //    buffer, [this](mesytec::mvlc::ReadoutBuffer *b)
                //    { m_snoopQueues.emptyBufferQueue().enqueue(b); });

                try
                {
                    processBuffer(buffer, vmeConfig, analysis);
                    empty.enqueue(buffer);
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
                    m_eventBuilder.flush(true);
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

                for (auto &c: moduleConsumers())
                    c->processTimetick();

                elapsedSeconds--;
            }
        }

        QCoreApplication::processEvents();
    }

    const auto daqStats = getDAQStats();

    for (auto c: moduleConsumers())
        c->endRun(daqStats);

    for (auto c: bufferConsumers())
        c->endRun(daqStats);

    analysis->endRun();

    {
        UniqueLock guard(m_countersMutex);
        m_counters.stopTime = QDateTime::currentDateTime();
    }

    // analysis session auto save
    auto sessionPath = make_workspace_settings(getWorkspaceDir())->value(QSL("SessionDirectory")).toString();

    if (!sessionPath.isEmpty())
    {
        auto filename = sessionPath + "/last_session" + analysis::SessionFileExtension;
        auto result   = save_analysis_session(filename, getAnalysis());

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

    if (!runInfo.runId.isEmpty())
    {
        std::map<u8, mesytec::mvlc::readout_parser::ReadoutParserCounters> parserCounters;
        std::map<u8, std::shared_ptr<analysis::Analysis>> analyses;

        parserCounters[0] = m_parserCounters;
        if (auto ana = getAnalysis())
            analyses[0] = ana->shared_from_this();
        auto filename = QString("logs/%1.json").arg(runInfo.runId);
        auto result   = analysis::save_run_statistics_to_json(runInfo, filename, parserCounters, analyses);

        if (!result.first)
        {
            logInfo(QString("Error saving analysis run statistics to %1: %2")
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

void MVLC_StreamWorker::doArtificalDelay()
{
    auto delay = m_artificialDelay.load();
    if (delay.count() > 0)
    {
        using DoubleMillis = std::chrono::duration<double, std::milli>;
        std::this_thread::sleep_for(delay);
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
    auto bufferView = buffer->viewU32();
    const bool useLogThrottle = true;
    m_multiEventSplitter.processingFlags = {};

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

        if (pr == ParseResult::Ok && !m_multiEventSplitter.processingFlags)
        {
            // No exception was thrown and the parse result for the buffer is
            // ok.
            processingOk = true;
        }
        //else
        //    qDebug() << __PRETTY_FUNCTION__ << (int)pr << get_parse_result_name(pr) << m_multiEventSplitter.processingFlags;
    }
    // TODO: check which of these can actually escape parse_readout_buffer().
    // Seems like code duplication between here and the parser.
    catch (const end_of_buffer &e)
    {
        logWarn(QSL("end_of_buffer (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->bufferNumber()),
                useLogThrottle);
    }
    catch (const std::exception &e)
    {
        logWarn(QSL("exception (%1) when parsing buffer #%2")
                .arg(e.what())
                .arg(buffer->bufferNumber()),
                useLogThrottle);
    }
    catch (...)
    {
        logWarn(QSL("unknown exception when parsing buffer #%1")
                .arg(buffer->bufferNumber()),
                useLogThrottle);
    }

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

    for (auto &c: bufferConsumers())
    {
        auto view = buffer->viewU32();
        c->processBuffer(buffer->type(), buffer->bufferNumber(), view.data(), view.size());
    }

    // Copy counters to the guarded member variables.
    m_parserCountersSnapshot.access().ref() = m_parserCounters;
    m_multiEventSplitterCounters.access().ref() = m_multiEventSplitter.counters;

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
    for (auto &c: moduleConsumers())
        c->startup();

    for (auto &c: bufferConsumers())
        c->startup();
}

void MVLC_StreamWorker::shutdownConsumers()
{
    for (auto &c: moduleConsumers())
        c->shutdown();

    for (auto &c: bufferConsumers())
        c->shutdown();
}
