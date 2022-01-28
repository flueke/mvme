#include <array>
#include <fmt/format.h>
#include <memory>
#include <spdlog/spdlog.h>

#include <QApplication>

#include "analysis/a2/a2_data_filter.h"
#include "mesytec-mvlc/event_builder.h"
#include "mesytec-mvlc/mvlc_readout_parser.h"
#include "mesytec-mvlc/mvlc_readout_parser_util.h"
#include "mesytec-mvlc/mvlc_readout_worker.h"
#include "mesytec-mvlc/readout_buffer_queues.h"
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

void logger(const QString &msg)
{
    spdlog::info(msg.toStdString());
}

int main(int argc, char *argv[])
{
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
                        // TODO: check if the module should be ignored from event builder timestamp matching
                        crateSetup.moduleTimestampExtractors.emplace_back(
                            mvlc::IndexedTimestampFilterExtractor(
                                a2::data_filter::make_filter("11DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"), -1, 'D'));
                        crateSetup.moduleMatchWindows.push_back(
                            mvlc::event_builder::DefaultMatchWindow);
                    }

                    eventSetup.crateSetups.emplace_back(crateSetup);
                }
            }
        }

        ebSetup.setups = std::move(eventSetups);
    }

    auto run_event_builder = [] (
        mvlc::EventBuilder &eventBuilder,
        mvlc::readout_parser::ReadoutParserCallbacks &callbacks,
        std::atomic<bool> &quit)
    {
        while (!quit)
            eventBuilder.buildEvents(callbacks);

        // flush
        eventBuilder.buildEvents(callbacks, true);
    };

    mcrdo.eventBuilder = std::make_unique<mvlc::EventBuilder>(ebSetup);
    mcrdo.eventBuilderQuit = std::make_unique<std::atomic<bool>>(false);
    mcrdo.eventBuilderThread = std::thread(
        run_event_builder,
        std::ref(*mcrdo.eventBuilder),
        std::ref(mcrdo.eventBuilderCallbacks),
        std::ref(*mcrdo.eventBuilderQuit));

    // Build a CrateReadout structure for each crate

    for (const auto &conf: crateVMEConfigs)
    {
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
        std::tie(stackTriggers, ec) = mvme_mvlc::get_trigger_values(*conf, logger);

        // ReadoutWorker -> ReadoutParser
        crdo.readoutBufferQueues = std::make_unique<mvlc::ReadoutBufferQueues>();
        crdo.readoutWriteHandle = std::make_unique<BlockingBufferQueuesWriteHandle>(
            *crdo.readoutSnoopQueues, crdo.mvlc.connectionType());

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

        // ReadoutParser (TODO: setup the callbacks calling into the event builder)
        crdo.parserState = mvlc::readout_parser::make_readout_parser(
            mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks));
        crdo.parserCounters = std::make_unique<CrateReadout::ProtectedParserCounters>();
        crdo.parserQuit = std::make_unique<std::atomic<bool>>(false);
        crdo.parserThread = std::thread(
            mvlc::readout_parser::run_readout_parser,
            // FIXME: does the ref stay valid when moving crdo? use a unique_ptr here too?
            std::ref(crdo.parserState),
            std::ref(*crdo.parserCounters),
            std::ref(*crdo.readoutBufferQueues),
            std::ref(crdo.parserCallbacks),
            std::ref(*crdo.parserQuit)
            );
    }

    assert(crateVMEConfigs.size() == mcrdo.crateReadouts.size());

    // Run the mvme daq start sequence for each crate. Abort if one fails.

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

    return 0;
}
