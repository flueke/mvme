#include <array>
#include <chrono>
#include <fmt/format.h>
#include <memory>
#include <spdlog/spdlog.h>

#include <QApplication>

#include <mesytec-mvlc/event_builder.h>
#include <mesytec-mvlc/mvlc_constants.h>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include <mesytec-mvlc/mvlc_readout_parser_util.h>
#include <mesytec-mvlc/mvlc_readout_worker.h>
#include <mesytec-mvlc/readout_buffer_queues.h>

#include "analysis/a2/a2_data_filter.h"
#include "analysis/analysis.h"
#include "multi_crate.h"
#include "mvlc_daq.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc_readout_worker.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_session.h"
#include "vme_controller_factory.h"

using namespace mesytec;
using namespace multi_crate;

#if 0
// Writes a MultiCrateConfig to file. Uses hardcoded data.
int main(int argc, char *argv[])
{
    MultiCrateConfig mcfg;

    mcfg.mainConfig = "main.vme";
    mcfg.secondaryConfigs = QStringList{ "secondary1.vme" };
    mcfg.crossCrateEventIds = { QUuid::fromString(QSL("{ac3e7e4a-b322-42ee-a203-59f862f109ea}")) };
    mcfg.mainModuleIds = { QUuid::fromString(QSL("{3e87cd9b-b2e9-448a-92d7-7a540a1bce35}")) };

    auto jdoc = to_json_document(mcfg);

    QFile outFile("multicrate.multicratecfg");

    if (!outFile.open(QIODevice::WriteOnly))
        return 1;

    outFile.write(jdoc.toJson());

    return 0;
}
#endif

void global_logger(const QString &msg)
{
    spdlog::info(msg.toStdString());
}

void global_error_logger(const QString &msg)
{
    spdlog::error(msg.toStdString());
}

int main(int argc, char *argv[])
{
    mvlc::ReadoutBuffer buffer;

    mvlc::listfile::write_module_data(buffer, 0, 0, nullptr, 0);

    QApplication app(argc, argv);
    mvme_init("dev_multi_crate_playground_cli");

    auto vmeConfigFiles =
    {
        "main.vme",
        "secondary1.vme",
    };

    std::set<int> crossCrateEvents = { 0 };

    assert(!crossCrateEvents.empty()); // FIXME: allow this too? pointless to have no cross crate event at all...

    // FIXME: find a better way to specify the main module (use id or
    // (eventIndex, moduleIndex) referencing the merged config.
#if 0
    // Specifier for the main/reference module in cross crate events.
    // Format: crateIndex, eventIndex, moduleIndex
    using MainModuleSpec = std::array<int, 3>;

    // One MainModuleSpec per cross crate event is required.
    std::vector<MainModuleSpec> mainModules =
    {
        { 0, 0, 0 }
    };
#endif

    // Read in the vmeconfigs

    std::vector<std::unique_ptr<VMEConfig>> crateVMEConfigs;

    for (const auto &filename: vmeConfigFiles)
    {
        auto readResult = read_vme_config_from_file(filename);

        if (!readResult.first)
        {
            throw std::runtime_error(fmt::format(
                    "Error reading vme config {}: {}",
                    filename, readResult.second.toStdString()));
        }
        crateVMEConfigs.emplace_back(std::move(readResult.first));
    }

    // Create the merged config and module id mappings.

    std::unique_ptr<VMEConfig> mergedVMEConfig;
    MultiCrateModuleMappings moduleIdMappings;

    std::tie(mergedVMEConfig, moduleIdMappings) = make_merged_vme_config(
        crateVMEConfigs, crossCrateEvents);

    // MultiCrate readout instance
    MultiCrateReadout mcrdo;

    // Create an empty analysis. For a first test it's going to run in the
    // EventBuilder thread.

    auto analysis_logger = [] (const QString &msg)
    {
        spdlog::info("analysis: {}", msg.toStdString());
    };

    RunInfo runInfo;
    runInfo.runId = "foobar";
    auto analysis = std::make_unique<analysis::Analysis>();
    analysis->beginRun(runInfo, mergedVMEConfig.get(), analysis_logger);

    // EventBuilder setup
    mvlc::EventBuilderConfig ebSetup;

    {
        int maxCrossCrateEventIndex = *std::max_element(
            std::begin(crossCrateEvents), std::end(crossCrateEvents));
        std::vector<mvlc::EventSetup> eventSetups(maxCrossCrateEventIndex + 1);

        for (size_t ei=0; ei<eventSetups.size(); ++ei)
        {
            auto &eventSetup = eventSetups[ei];
            eventSetup.enabled = crossCrateEvents.count(ei);
            eventSetup.mainModule = std::make_pair(0, 0); // first crate, first module (FIXME)

            if (eventSetup.enabled)
            {
                for (size_t ci=0; ci<crateVMEConfigs.size(); ++ci)
                {
                    mvlc::EventSetup::CrateSetup crateSetup;
                    auto eventConfig = crateVMEConfigs[ci]->getEventConfig(ei);

                    for (int mi=0; mi<eventConfig->getModuleConfigs().size(); ++mi)
                    {
                        // FIXME: these values must be configurable
                        // TODO: check if the module should be ignored from
                        // event builder timestamp matching and use the
                        // InvalidTimestampExtractor in that case.
                        crateSetup.moduleTimestampExtractors.emplace_back(
                            mvlc::IndexedTimestampFilterExtractor(
                                a2::data_filter::make_filter("11DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"), -1, 'D'));
                        //auto matchWindow = std::make_pair(-1000, 1000);
                        auto matchWindow = mvlc::event_builder::DefaultMatchWindow;
                        crateSetup.moduleMatchWindows.push_back(matchWindow);
                    }

                    eventSetup.crateSetups.emplace_back(crateSetup);
                }
            }
        }

        ebSetup.setups = std::move(eventSetups);
    }

    mcrdo.eventBuilder = std::make_unique<mvlc::EventBuilder>(ebSetup);

    for (auto ei: crossCrateEvents)
        assert(mcrdo.eventBuilder->isEnabledFor(ei));

    spdlog::info("Linear module index for module (ci=0, ei=0, mi=0): {}",
                 mcrdo.eventBuilder->getLinearModuleIndex(0, 0, 0));
    spdlog::info("Linear module index for module (ci=1, ei=0, mi=0): {}",
                 mcrdo.eventBuilder->getLinearModuleIndex(1, 0, 0));

    std::map<int, std::map<int, std::map<int, size_t>>> ebOutputCounts;

    mcrdo.eventBuilderQuit = std::make_unique<std::atomic<bool>>(false);
    mcrdo.eventBuilderSnoopOutputQueues = std::make_unique<mvlc::ReadoutBufferQueues>();

    mcrdo.eventBuilderCallbacks.eventData = [&ebOutputCounts, &analysis] (
        void *userContext, int ci, int ei,
        const mvlc::readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
    {
        (void) userContext;

        analysis->beginEvent(ei);
        for (unsigned mi=0; mi<moduleCount;++mi)
        {
            auto &moduleData = moduleDataList[mi].data;

            if (moduleData.size)
            {
                ++ebOutputCounts[ci][ei][mi];
                analysis->processModuleData(ei, mi, moduleData.data, moduleData.size);
            }
        }
        analysis->endEvent(ei);

        //spdlog::info("eb.eventData: ci={}, ei={}, moduleCount={}",
        //             ci, ei, moduleCount);
    };

    mcrdo.eventBuilderCallbacks.systemEvent = [&analysis] (
        void *userContext, int ci, const u32 *header, u32 size)
    {
        // TODO: need to generate analysis timeticks. If it's a replay use the
        // timeticks from the main crate (ci=0).
        // If it's a live run use a TimetickGenerator.
        (void) userContext;
        //spdlog::info("eb.systemEvent: ci={}, size={}",
        //             ci, size);
    };

    auto run_event_builder = [] (
        mvlc::EventBuilder &eventBuilder,
        mvlc::readout_parser::ReadoutParserCallbacks &callbacks,
        std::atomic<bool> &quit)
    {
        spdlog::info("run_event_builder thread starting");

        while (!quit)
            eventBuilder.buildEvents(callbacks);

        // flush
        eventBuilder.buildEvents(callbacks, true);

        spdlog::info("run_event_builder thread done");
    };

    mcrdo.eventBuilderThread = std::thread(
        run_event_builder,
        std::ref(*mcrdo.eventBuilder),
        std::ref(mcrdo.eventBuilderCallbacks),
        std::ref(*mcrdo.eventBuilderQuit));

    // Build a CrateReadout structure for each crate

    // FIXME: reserving to avoid reallocations during the loop so that the refs
    // passed to the run_readout_parser thread stay valid. Find a better to
    // deal with this.
    mcrdo.crateReadouts.reserve(crateVMEConfigs.size());

    for (size_t ci=0; ci<crateVMEConfigs.size(); ++ci)
    {
        const auto &conf = crateVMEConfigs[ci];
        mcrdo.crateReadouts.emplace_back(CrateReadout());
        auto &crdo = mcrdo.crateReadouts.back();

        VMEControllerFactory cf(conf->getControllerType());

        // ugly as can be
        crdo.mvlcController = std::unique_ptr<mvme_mvlc::MVLC_VMEController>(
            qobject_cast<mvme_mvlc::MVLC_VMEController *>(
                cf.makeController(conf->getControllerSettings())));
        crdo.mvlc = crdo.mvlcController->getMVLC();
        // unused but required for the ReadoutWorker. 0 buffers of size 0..
        crdo.readoutSnoopQueues = std::make_unique<mvlc::ReadoutBufferQueues>(0, 0);

        // StackTriggers
        std::vector<u32> stackTriggers;
        std::error_code ec;
        std::tie(stackTriggers, ec) = mvme_mvlc::get_trigger_values(*conf, global_logger);

        // ReadoutWorker -> ReadoutParser
        crdo.readoutBufferQueues = std::make_unique<mvlc::ReadoutBufferQueues>();
        crdo.readoutWriteHandle = std::make_unique<BlockingBufferQueuesWriteHandle>(
            *crdo.readoutBufferQueues, crdo.mvlc.connectionType());

        // mvlc::CrateConfig
        auto crateConfig = mvme::vmeconfig_to_crateconfig(conf.get());

        // mvlc::ReadoutWorker
        crdo.readoutWorker = std::make_unique<mvlc::ReadoutWorker>(
            crdo.mvlc,
            stackTriggers,
            *crdo.readoutSnoopQueues,
            crdo.readoutWriteHandle.get());

        crdo.readoutWorker->setMcstDaqStartCommands(crateConfig.mcstDaqStart);
        crdo.readoutWorker->setMcstDaqStopCommands(crateConfig.mcstDaqStop);

        // ReadoutParser pushing data into the EventBuilder
        crdo.parserState = mvlc::readout_parser::make_readout_parser(
            mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks), ci);
        crdo.parserCounters = std::make_unique<CrateReadout::ProtectedParserCounters>();

        crdo.parserCallbacks.eventData = [&mcrdo] (
            void *userContext, int ci, int ei,
            const mvlc::readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
        {
            (void) userContext;
            //spdlog::info("parser.eventData: ci={}, ei={}, moduleCount={}",
            //             ci, ei, moduleCount);
            mcrdo.eventBuilder->recordEventData(ci, ei, moduleDataList, moduleCount);
        };

        crdo.parserCallbacks.systemEvent = [&mcrdo] (
            void *userContext, int ci, const u32 *header, u32 size)
        {
            (void) userContext;
            //spdlog::info("parser.systemEvent: ci={}, size={}",
            //             ci, size);
            mcrdo.eventBuilder->recordSystemEvent(ci, header, size);
        };

        crdo.parserQuit = std::make_unique<std::atomic<bool>>(false);
        // XXX: careful with the refs (see above)!
        crdo.parserThread = std::thread(
            mvlc::readout_parser::run_readout_parser,
            std::ref(crdo.parserState),
            std::ref(*crdo.parserCounters),
            std::ref(*crdo.readoutBufferQueues),
            std::ref(crdo.parserCallbacks),
            std::ref(*crdo.parserQuit)
            );
    }

    assert(crateVMEConfigs.size() == mcrdo.crateReadouts.size());

    // Run the mvme daq start sequence for each crate. Abort if one fails.
    // This can be done in crate order as no triggers should be enabled after
    // the start sequence.

    for (size_t ci=0; ci<crateVMEConfigs.size(); ++ci)
    {
        auto &conf = *crateVMEConfigs[ci];
        auto &crdo = mcrdo.crateReadouts[ci];

        auto logger = [ci] (const QString &msg)
        {
            spdlog::info("crate{}: {}", ci, msg.toStdString());
        };

        auto error_logger = [ci] (const QString &msg)
        {
            spdlog::error("crate{}: {}", ci, msg.toStdString());
        };

        crdo.mvlcController->getMVLCObject()->setDisableTriggersOnConnect(true);
        auto err = crdo.mvlcController->open();

        if (err.isError())
        {
            throw std::runtime_error(
                fmt::format("crate{}: error connecting to mvlc: {}",
                            ci, err.toString().toStdString()));
        }

        if (!run_daq_start_sequence(
            crdo.mvlcController.get(),
            conf,
            false, // ignoreStartupErrors
            logger,
            error_logger))
        {
            throw std::runtime_error(
                fmt::format("crate{}: error running daq start sequence", ci));
        }
    }

    // Start each readoutWorker. Do this in reverse crate order so that the
    // main crate is started last.

    for (auto it=mcrdo.crateReadouts.rbegin(); it!=mcrdo.crateReadouts.rend(); ++it)
    {
        auto ci = mcrdo.crateReadouts.rend() - it - 1;
        spdlog::info("Starting readout for crate {}", ci);

        auto f = it->readoutWorker->start();
        auto ec = f.get();

        if (ec)
        {
            throw std::runtime_error(
                fmt::format("Error starting readout for crate{}: {}",
                            ci, ec.message()));
        }
    }

    const auto timeToRun = std::chrono::seconds(10);

    spdlog::info("Running DAQ for {} seconds", timeToRun.count());

    std::this_thread::sleep_for(timeToRun);

    spdlog::info("Stopping DAQ");

    // Stop the crate readouts in ascending crate order so that the master
    // event DAQ stop scripts are run before the slave readouts are stopped.
    // Otherwise the slaves will not execute their stacks in response to slave
    // triggers.
    for (auto it=mcrdo.crateReadouts.begin(); it!=mcrdo.crateReadouts.end(); ++it)
    {
        auto ci = it - mcrdo.crateReadouts.begin();
        spdlog::info("Stopping readout for crate{}", ci);

        if (auto ec = it->readoutWorker->stop())
        {
            spdlog::warn("Error stopping readout for crate{}: {}",
                         ci, ec.message());
        }
        else
        {
            while (it->readoutWorker->state() != mvlc::ReadoutWorker::State::Idle)
            {
                it->readoutWorker->waitableState().wait_for(
                    std::chrono::milliseconds(100),
                    [] (const mvlc::ReadoutWorker::State &state)
                    {
                        return state == mvlc::ReadoutWorker::State::Idle;
                    });
            }

            spdlog::info("Stopped readout for crate{}", ci);
        }

        spdlog::info("Running DAQ stop sequence for crate{}", ci);

        auto logger = [ci] (const QString &msg)
        {
            spdlog::info("crate{}: {}", ci, msg.toStdString());
        };

        auto error_logger = [ci] (const QString &msg)
        {
            spdlog::error("crate{}: {}", ci, msg.toStdString());
        };

        auto stopResults = mvlc_daq_shutdown(
            crateVMEConfigs[ci].get(),
            it->mvlcController.get(),
            logger, error_logger);

        spdlog::info("Stopping readout parser for crate{}", ci);
        *it->parserQuit = true;

        if (it->parserThread.joinable())
            it->parserThread.join();

        spdlog::info("Stopped readout parser for crate{}", ci);
    }

    spdlog::info("Stopping EventBuilder");

    *mcrdo.eventBuilderQuit = true;
    if (mcrdo.eventBuilderThread.joinable())
        mcrdo.eventBuilderThread.join();

    spdlog::info("Stopped EventBuilder");

    auto ebCounters = mcrdo.eventBuilder->getCounters();

    spdlog::info("EventBuilder max mem usage: {}", ebCounters.maxMemoryUsage);

    for (size_t ei=0; ei<ebCounters.eventCounters.size(); ++ei)
    {
        auto &modCounters = ebCounters.eventCounters.at(ei);
        for (size_t mi=0; mi<modCounters.discardedEvents.size(); ++mi)
        {
            spdlog::info(
                "  ei={}, mi={}, totalHits={}, discarded={}, empties={}, invScoreSum={}",
                ei, mi,
                modCounters.totalHits.at(mi),
                modCounters.discardedEvents.at(mi),
                modCounters.emptyEvents.at(mi),
                modCounters.invScoreSums.at(mi)
                );
        }
    }

    analysis->endRun();

    return 0;
}
