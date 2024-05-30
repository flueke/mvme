#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QApplication>
#include <QTimer>

#include "analysis/analysis_ui.h"
#include "analysis/analysis_util.h"
#include "multi_crate.h"
#include "mvlc_daq.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_session.h"
#include "util/qt_monospace_textedit.h"
#include "util/signal_handling.h"
#include "util/stopwatch.h"
#include "vme_config_util.h"
#include "vme_analysis_common.h"

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvme;
using namespace mesytec::mvme::multi_crate;

int main(int argc, char *argv[])
{
    std::string generalHelp = R"~(
        write the help text please!
)~";

    QApplication app(argc, argv);
    mvme_init("mvme_multicrate_replay", false);

    app.setWindowIcon(QIcon(":/window_icon.png"));

    spdlog::set_level(spdlog::level::warn);
    mesytec::mvlc::set_global_log_level(spdlog::level::warn);

    setup_signal_handlers();

    if (argc < 2)
    {
        std::cout << generalHelp;
        return 1;
    }

    argh::parser parser({"-h", "--help", "--log-level"});
    parser.add_params({"--analysis"});
    parser.parse(argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";
        else if (parser["--info"])
            logLevelName = "info";

        if (!logLevelName.empty())
            spdlog::set_level(spdlog::level::from_str(logLevelName));
    }

    trace_log_parser_info(parser, "mvme_multicrate_replay");

    if (parser.pos_args().size() <= 1)
    {
        std::cerr << "Error: no listfile filename given on command line\n";
        return 1;
    }

    std::string listfileFilename = parser.pos_args()[1];
    std::string analysisFilename;
    parser("--analysis") >> analysisFilename;

    mvlc::listfile::SplitZipReader zipReader;
    try
    {
        zipReader.openArchive(listfileFilename);
    }
    catch(const std::exception& e)
    {
        std::cerr << fmt::format("Could not open listfile archive {}: {}\n", listfileFilename, e.what());
        return 1;
    }

    auto listfileEntryName = zipReader.firstListfileEntryName();

    if (listfileEntryName.empty())
    {
        std::cerr << "Error: no listfile entry found in " << listfileFilename << "\n";
        return 1;
    }

    mvlc::listfile::ReadHandle *listfileReadHandle = {};

    try
    {
        listfileReadHandle = zipReader.openEntry(listfileEntryName);
    }
    catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error: could not open listfile entry {} for reading: {}\n", listfileEntryName, e.what());
        return 1;
    }

    auto readerHelper = mvlc::listfile::make_listfile_reader_helper(listfileReadHandle);
    std::vector<std::unique_ptr<VMEConfig>> vmeConfigs;

    for (auto &sysEvent: readerHelper.preamble.systemEvents)
    {
        if (sysEvent.type == system_event::subtype::MVMEConfig)
        {
            auto [vmeConfig, ec] = vme_config::read_vme_config_from_data(sysEvent.contents);

            if (ec)
            {
                std::cerr << fmt::format("Error reading VME config from listfile: {}\n", ec.message());
                return 1;
            }

            vmeConfigs.emplace_back(std::move(vmeConfig));
        }
    }

    fmt::print("Read {} vme configs from {}\n", vmeConfigs.size(), listfileFilename);

    std::vector<mvlc::CrateConfig> crateConfigs;

    for (auto &vmeConfig: vmeConfigs)
    {
        crateConfigs.emplace_back(vmeconfig_to_crateconfig(vmeConfig.get()));
    }

    // Prepare sockets for the processing cratePipelines.

    std::unordered_map<u8, nng::SocketPipeline> cratePipelines; // one processing pipeline per crate. key is crateId

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &crateConfig = crateConfigs[i];
        auto crateId = crateConfig.crateId;

        std::vector<nng::SocketPipeline::Link> links;

        const auto urlTemplates = { "inproc://crate{}_stage00_raw", "inproc://crate{}_stage01_parser", "inproc://crate{}_stage02_event_builder" };

        for (auto tmpl: urlTemplates)
        {
            auto url = fmt::format(tmpl, crateId);
            auto [link, res] = nng::make_pair_link(url);
            if (res)
            {
                nng::mesy_nng_error(fmt::format("make_pair_link {}", url), res);
                std::for_each(links.begin(), links.end(), nng::close_link);
                return 1;
            }
            links.emplace_back(link);
        }

        auto pipeline = nng::SocketPipeline::fromLinks(links);
        cratePipelines[crateId] = std::move(pipeline);
    }

    // An additional socket pair for parsed stage0 data from all crates.
    // Producers are usually event builders but could also be readout parsers or
    // multievent splitters if stage0 event building is not wanted.
    // Consumer is the stage1 event builder or directly the stage1 analysis.
    nng::SocketPipeline::Link stage0DataLink;

    {
        auto url = "inproc://stage0_data";
        auto [link, res] = nng::make_pair_link(url);
        //auto [stage0DataLink, res] = nng::make_pubsub_link(url);

        if (res)
        {
            nng::mesy_nng_error(fmt::format("make_pair_link {}", url), res);
            // TODO: close pipelines! RAII please!
            return 1;
        }

        stage0DataLink = link;
    }

    nng::SocketPipeline::Link stage1DataLink;

    {
        auto url = "inproc://stage1_data";
        auto [link, res] = nng::make_pair_link(url);

        if (res)
        {
            nng::mesy_nng_error(fmt::format("make_pair_link {}", url), res);
            // TODO: close pipelines! RAII please!
            return 1;
        }

        stage1DataLink = link;
    }

    size_t pipelineStep = 0;

    // Replay: one context with one output writer for each crate in the input listfile.

    MulticrateReplayContext replayContext;
    replayContext.quit = false;
    replayContext.lfh = readerHelper.readHandle;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &crateConfig = crateConfigs[i];
        auto crateId = crateConfig.crateId;
        const auto &pipeline = cratePipelines[crateId];
        assert(pipeline.elements().size() >= pipelineStep+1);

        auto writerSocket = pipeline.elements()[pipelineStep].outputSocket;
        auto writer = std::make_unique<nng::SocketOutputWriter>(writerSocket);
        writer->debugInfo = fmt::format("replay_loop (crateId={})", crateId);
        writer->retryPredicate = [ctx=&replayContext] { return !ctx->quit; };
        if (replayContext.writers.size() <= crateId)
            replayContext.writers.resize(crateId+1);
        replayContext.writers[crateId] = std::move(writer);
    }

    ++pipelineStep;

    // Parsers: one parser per crate
    std::vector<std::unique_ptr<ReadoutParserContext>> parserContexts;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &crateConfig = crateConfigs[i];
        auto crateId = crateConfig.crateId;
        const auto &pipeline = cratePipelines[crateId];
        assert(pipeline.elements().size() >= pipelineStep+1);

        auto inputReader = std::make_unique<nng::SocketInputReader>(pipeline.elements()[pipelineStep].inputSocket);
        auto outputWriter = std::make_unique<nng::SocketOutputWriter>(pipeline.elements()[pipelineStep].outputSocket);

        auto parserContext = make_readout_parser_context(crateConfig);
        parserContext->quit = false;
        outputWriter->debugInfo = fmt::format("parserOutput (crateId={})", crateId);
        outputWriter->retryPredicate = [ctx=parserContext.get()] { return !ctx->quit; };
        parserContext->inputReader = std::move(inputReader);
        parserContext->outputWriter = std::move(outputWriter);

        parserContexts.emplace_back(std::move(parserContext));
    }

    ++pipelineStep;

    // stage0 event builders: one per crate
    std::vector<std::unique_ptr<EventBuilderContext>> eventBuilderStage0Contexts;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &crateConfig = crateConfigs[i];
        auto crateId = crateConfig.crateId;
        const auto &pipeline = cratePipelines[crateId];
        assert(pipeline.elements().size() >= pipelineStep+1);

        auto stacks = mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks);
        auto readoutStructure = readout_parser::build_readout_structure(stacks);
        const size_t moduleCount = readoutStructure.at(0).size(); // modules in event 0

        EventSetup::CrateSetup crateSetup;

        for (size_t mi=0; mi<moduleCount; ++mi)
        {
            crateSetup.moduleTimestampExtractors.emplace_back(make_mesytec_default_timestamp_extractor());
            crateSetup.moduleMatchWindows.emplace_back(event_builder::DefaultMatchWindow);
        }

        EventSetup eventSetup;
        eventSetup.enabled = true;
        eventSetup.crateSetups.emplace_back(crateSetup);
        eventSetup.mainModule = std::make_pair(0, 0);

        EventBuilderConfig ebConfig;
        ebConfig.setups.emplace_back(eventSetup);

        auto ebContext = std::make_unique<EventBuilderContext>();
        ebContext->crateId = crateId;
        ebContext->quit = false;
        ebContext->eventBuilderConfig = ebConfig;
        ebContext->eventBuilder = std::make_unique<EventBuilder>(ebConfig, ebContext.get());

        auto inputReader = std::make_unique<nng::SocketInputReader>(pipeline.elements()[pipelineStep].inputSocket);
        auto outputWriter0 = std::make_unique<nng::SocketOutputWriter>(pipeline.elements()[pipelineStep].outputSocket);
        outputWriter0->debugInfo = fmt::format("eventBuilderOutput (crateId={})", crateId);
        outputWriter0->retryPredicate = [ctx=ebContext.get()] { return !ctx->quit; };

        auto outputWriter1 = std::make_unique<nng::SocketOutputWriter>(stage0DataLink.listener);
        outputWriter1->debugInfo = fmt::format("stage0Data (crateId={})", crateId);
        outputWriter1->retryPredicate = [ctx=ebContext.get()] { return !ctx->quit; };

        auto outputWriter = std::make_unique<nng::MultiOutputWriter>();
        outputWriter->addWriter(std::move(outputWriter0));
        outputWriter->addWriter(std::move(outputWriter1));

        ebContext->inputReader = std::move(inputReader);
        ebContext->outputWriter = std::move(outputWriter);
        // Rewrite data to make the eventbuilder work for a single crate with a non-zero crateid.
        if (crateConfig.crateId < ebContext->inputCrateMappings.size())
        {
            ebContext->inputCrateMappings[crateConfig.crateId] = 0;
            ebContext->outputCrateMappings[0] = crateConfig.crateId;
        }
        else
        {
            spdlog::error("event builder crate id out of range: crateId={}", crateConfig.crateId);
            return 1;
        }
        eventBuilderStage0Contexts.emplace_back(std::move(ebContext));
    }

    ++pipelineStep;

    // stage0 analysis : one per crate but each instances is loaded from the same file.
    std::vector<std::unique_ptr<AnalysisProcessingContext>> analysisStage0Contexts;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &crateConfig = crateConfigs[i];
        auto crateId = crateConfig.crateId;
        const auto &pipeline = cratePipelines[crateId];
        assert(pipeline.elements().size() >= pipelineStep+1);

        auto analysisContext = std::make_unique<AnalysisProcessingContext>();
        analysisContext->quit = false;
        analysisContext->inputReader = std::make_unique<nng::SocketInputReader>(pipeline.elements()[pipelineStep].inputSocket);
        analysisContext->crateId = crateId;
        analysisContext->isReplay = true;
        std::shared_ptr<analysis::Analysis> analysis;

        if (!analysisFilename.empty())
        {
            auto [ana, errorString] = analysis::read_analysis_config_from_file(analysisFilename.c_str());

            if (!ana)
            {
                std::cerr << fmt::format("Error reading mvme analysis config from '{}': {}\n",
                    analysisFilename, errorString.toStdString());
                return 1;
            }

            analysis::generate_new_object_ids(ana.get()); // FIXME: figure out object id handling
            analysis = ana;
        }
        else
        {
            analysis = std::make_shared<analysis::Analysis>();
        }

        analysisContext->analysis = analysis;
        auto asp = std::make_unique<multi_crate::MinimalAnalysisServiceProvider>();
        asp->vmeConfig_ = vmeConfigs[i].get();
        asp->analysis_ = analysis;
        auto widgetRegistry = std::make_shared<WidgetRegistry>();
        asp->widgetRegistry_ = widgetRegistry;
        analysisContext->asp = std::move(asp);
        analysis->beginRun(RunInfo{}, vmeConfigs[i].get());

        analysisStage0Contexts.emplace_back(std::move(analysisContext));
    }

    // stage1: event builder
    auto eventBuilderStage1Context = std::make_unique<EventBuilderContext>();

    // Make event0 a cross crate event. Assume each module in event0 from all
    // crates is a mesytec module and uses a large match window.
    {
        EventSetup eventSetup;
        eventSetup.enabled = true;
        // eventSetup.mainModule = std::make_pair(0, 0); FIXME: see below

        for (size_t i=0; i<vmeConfigs.size(); ++i)
        {
            const auto &crateConfig = crateConfigs[i];
            auto stacks = mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks);
            auto readoutStructure = readout_parser::build_readout_structure(stacks);
            const size_t moduleCount = readoutStructure.at(0).size(); // modules in event 0

            EventSetup::CrateSetup crateSetup;

            for (size_t mi=0; mi<moduleCount; ++mi)
            {
                crateSetup.moduleTimestampExtractors.emplace_back(make_mesytec_default_timestamp_extractor());
                crateSetup.moduleMatchWindows.emplace_back(std::make_pair(-100000, 100000));
            }

            eventSetup.mainModule = std::make_pair(crateConfig.crateId, 0); // FIXME: wrong and a workaround
            eventSetup.crateSetups.resize(std::max(eventSetup.crateSetups.size(), static_cast<size_t>(crateConfig.crateId+1)));
            eventSetup.crateSetups[crateConfig.crateId] = std::move(crateSetup);
        }

        EventBuilderConfig ebConfig;
        ebConfig.setups.emplace_back(eventSetup);

        auto &ebContext = eventBuilderStage1Context;
        ebContext->crateId = 0xffu; // XXX: special crateId to indicate combined data from all crates
        ebContext->quit = false;
        ebContext->eventBuilderConfig = ebConfig;
        ebContext->eventBuilder = std::make_unique<EventBuilder>(ebConfig, ebContext.get());
        ebContext->inputReader = std::make_unique<nng::SocketInputReader>(stage0DataLink.dialer);
        auto outputWriter = std::make_unique<nng::SocketOutputWriter>(stage1DataLink.listener);
        outputWriter->debugInfo = fmt::format("eventBuilderOutput (crateId={})", ebContext->crateId);
        outputWriter->retryPredicate = [ctx=ebContext.get()] { return !ctx->quit; };
        ebContext->outputWriter = std::move(outputWriter);
    }

    // stage1: analysis
    auto analysisStage1Context = std::make_unique<AnalysisProcessingContext>();
    auto mergedVmeConfig = make_merged_vme_config_keep_ids(vmeConfigs, std::set<int>{0}); // XXX: have to keep this alive. The minimal asp does not take ownership!

    {
        auto &ctx = analysisStage1Context;

        ctx->quit = false;
        ctx->inputReader = std::make_unique<nng::SocketInputReader>(stage1DataLink.dialer);
        ctx->crateId = 0xffu; // XXX: special crateId to indicate combined data from all crates
        ctx->isReplay = true;

        std::shared_ptr<analysis::Analysis> analysis;

        if (!analysisFilename.empty())
        {
            auto [ana, errorString] = analysis::read_analysis_config_from_file(analysisFilename.c_str());

            if (!ana)
            {
                std::cerr << fmt::format("Error reading mvme analysis config from '{}': {}\n",
                    analysisFilename, errorString.toStdString());
                return 1;
            }

            //analysis::generate_new_object_ids(ana.get()); // FIXME: figure out object id handling
            analysis = ana;
        }
        else
        {
            analysis = std::make_shared<analysis::Analysis>();
        }

        // FIXME: crate id index assignment issues
        vme_analysis_common::auto_assign_vme_modules(mergedVmeConfig.get(), analysis.get());

        ctx->analysis = analysis;

        auto asp = std::make_unique<multi_crate::MinimalAnalysisServiceProvider>();
        auto widgetRegistry = std::make_shared<WidgetRegistry>();
        asp->widgetRegistry_ = widgetRegistry;
        asp->vmeConfig_ = mergedVmeConfig.get();
        asp->analysis_ = analysis;
        ctx->asp = std::move(asp);
        analysis->beginRun(RunInfo{}, mergedVmeConfig.get());
    }

    auto log_counters = [&]
    {
        // stage0 replay
        {
            auto ca = replayContext.writerCounters.access();
            const auto &counters = ca.ref();
            for(size_t crateId=0; crateId<counters.size(); ++crateId)
            {
                log_socket_work_counters(counters[crateId], fmt::format("replay_loop (crateId={})", crateId));
            }
        }

        // stage0 parsers
        for (auto &ctx: parserContexts)
        {
            log_socket_work_counters(ctx->counters.access().ref(),
                fmt::format("readout_parser_loop (crateId={})", ctx->crateId));
        }

        // stage0 event builders
        for (auto &ctx: eventBuilderStage0Contexts)
        {
            log_socket_work_counters(ctx->counters.access().ref(),
                fmt::format("event_builder_loop (crateId={})", ctx->crateId));
        }

        // stage0 analyses
        for (auto &ctx: analysisStage0Contexts)
        {
            log_socket_work_counters(ctx->inputSocketCounters.access().ref(),
                fmt::format("analysis_loop (crateId={})", ctx->crateId));
        }

        // stage1 event builder
        {
            auto &ctx = eventBuilderStage1Context;
            log_socket_work_counters(ctx->counters.access().ref(), fmt::format("event_builder_loop (crateId={})", ctx->crateId));
        }

        // stage1 analysis
        {
            auto &ctx = analysisStage1Context;
            log_socket_work_counters(ctx->inputSocketCounters.access().ref(),
                fmt::format("analysis_loop (crateId={})", ctx->crateId));
        }
    };

    // Runtime of the single(!) replay_loop.
    std::unique_ptr<LoopRuntime> replayLoopRuntime;

    // Per crate runtimes of the processing stages.
    std::unordered_map<u8, std::vector<LoopRuntime>> cratePipelineRuntimes;
    // XXX: not sure how best to do this. Processing is a directed graph, not a simple pipeline.
    // For now keep the stage0 pipelines and this additional stage2 pipeline.
    // The bridge is after the stage0 event builder (or earlier if no event
    // building is done in stage0).
    PipelineRuntime stage1Runtimes;

    // stage0 replay_loop
    {
        std::vector<nng::OutputWriter *> writers;
        for (auto &w: replayContext.writers)
            writers.push_back(w.get());

        auto replayFuture = std::async(std::launch::async, replay_loop, std::ref(replayContext));
        auto rt = LoopRuntime{ replayContext.quit, std::move(replayFuture), writers, "replay_loop" };
        replayLoopRuntime = std::make_unique<LoopRuntime>(std::move(rt));
    }

    // stage0 parser_loop[crate]
    for (auto &parserContext: parserContexts)
    {
        std::vector<nng::OutputWriter *> writers { parserContext->outputWriter.get() };

        auto future = std::async(std::launch::async, readout_parser_loop, std::ref(*parserContext));
        auto rt = LoopRuntime{ parserContext->quit, std::move(future), writers, fmt::format("readout_parser_loop (crateId={})", parserContext->crateId) };
        cratePipelineRuntimes[parserContext->crateId].emplace_back(std::move(rt));
    }

    // stage0 event_builder_loop[crate]
    for (auto &ctx: eventBuilderStage0Contexts)
    {
        std::vector<nng::OutputWriter *> writers { ctx->outputWriter.get() };

        auto future = std::async(std::launch::async, event_builder_loop, std::ref(*ctx));
        auto rt = LoopRuntime{ ctx->quit, std::move(future), writers, fmt::format("event_builder_loop (crateId={})", ctx->crateId) };
        cratePipelineRuntimes[ctx->crateId].emplace_back(std::move(rt));
    }

    // stage0 analysis_loop[crate]
    for (auto &analysisContext: analysisStage0Contexts)
    {
        auto future = std::async(std::launch::async, analysis_loop, std::ref(*analysisContext));
        auto rt = LoopRuntime{ analysisContext->quit, std::move(future), {}, fmt::format("analysis_loop (crateId={})", analysisContext->crateId) };
        cratePipelineRuntimes[analysisContext->crateId].emplace_back(std::move(rt));
    }

    // stage1 event builder
    {
        auto &ctx = eventBuilderStage1Context;

        std::vector<nng::OutputWriter *> writers { ctx->outputWriter.get() };

        auto future = std::async(std::launch::async, event_builder_loop, std::ref(*ctx));
        auto rt = LoopRuntime{ ctx->quit, std::move(future), writers, fmt::format("event_builder_loop (crateId={})", ctx->crateId) };
        stage1Runtimes.emplace_back(std::move(rt));
    }

    // stage1 analysis
    {
        auto &ctx = analysisStage1Context;
        auto future = std::async(std::launch::async, analysis_loop, std::ref(*ctx));
        auto rt = LoopRuntime{ ctx->quit, std::move(future), {}, fmt::format("analysis_loop (crateId={})", ctx->crateId) };
        stage1Runtimes.emplace_back(std::move(rt));
    }

    // Widgets
    QTimer timer;

    for (size_t i=0; i<analysisStage0Contexts.size(); ++i)
    {
        auto &ctx = analysisStage0Contexts[i];
        auto asp = ctx->asp.get();
        auto widget = new analysis::ui::AnalysisWidget(asp);
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->setWindowTitle(fmt::format("Analysis (crate {})", ctx->crateId).c_str());
        widget->show();
    }

    {
        auto &ctx = analysisStage1Context;
        auto asp = ctx->asp.get();
        auto widget = new analysis::ui::AnalysisWidget(asp);
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->setWindowTitle(fmt::format("Analysis (crate {})", ctx->crateId).c_str());
        widget->show();
    }

    for (size_t i=0; i<eventBuilderStage0Contexts.size(); ++i)
    {
        auto &ctx = eventBuilderStage0Contexts[i];
        auto widget = mvme::util::make_monospace_plain_textedit().release();
        widget->setWindowTitle(fmt::format("EventBuilder Stage0 (crateId={})", ctx->crateId).c_str());
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->resize(1200, 200);
        widget->show();

        QObject::connect(&timer, &QTimer::timeout, widget, [widget, i, &eventBuilderStage0Contexts]
        {
            auto &context = eventBuilderStage0Contexts[i];
            auto counters = context->eventBuilder->getCounters();
            auto str = to_string(counters);
            widget->setPlainText(QString::fromStdString(str));
        });
    }

    {
        auto &ctx = eventBuilderStage1Context;
        auto widget = mvme::util::make_monospace_plain_textedit().release();
        widget->setWindowTitle(fmt::format("EventBuilder Stage1 (crateId={})", ctx->crateId).c_str());
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->resize(1200, 200);
        widget->show();

        QObject::connect(&timer, &QTimer::timeout, widget, [widget, &eventBuilderStage1Context]
        {
            auto &context = eventBuilderStage1Context;
            auto counters = context->eventBuilder->getCounters();
            auto str = to_string(counters);
            widget->setPlainText(QString::fromStdString(str));
        });
    }
    // Wait for things to finish.

    auto shutdown_if_replay_done = [&]
    {
        auto &replayFuture = replayLoopRuntime->resultFuture;

        if (replayFuture.valid())
        {
            spdlog::info("shutdown_if_replay_done: replayFuture is valid");
            if (replayFuture.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready)
            {
                spdlog::debug("shutdown_if_replay_done: replay done, leaving main loop");
                const auto ShutdownMaxWait = std::chrono::milliseconds(10 * 1000);

                replayContext.quit = true;
                auto replayLoopResult = shutdown_loop(*replayLoopRuntime, ShutdownMaxWait);

                spdlog::info("shutdown_if_replay_done: replay loop shutdown done, result={}", replayLoopResult.toString());

                for (auto &[crateId, rt]: cratePipelineRuntimes)
                {
                    auto results = shutdown_pipeline(rt, ShutdownMaxWait);
                    for (size_t j=0; j<results.size(); ++j)
                    {
                        spdlog::info("shutdown_if_replay_done: crate {}, step {} pipeline shutdown done, result={}", crateId, j, results[j].toString());
                    }
                }

                spdlog::debug("shutdown_if_replay_done: processing cratePipelines shutdown done, closing sockets");

                for (auto &[crateId, pipeline]: cratePipelines)
                {
                    if (int res = close_sockets(pipeline))
                    {
                        spdlog::warn("shutdown_if_replay_done: close_sockets failed for crate {}, res={}", crateId, res);
                    }
                }

                auto stage1Results = shutdown_pipeline(stage1Runtimes, ShutdownMaxWait);
                for (size_t j=0; j<stage1Results.size(); ++j)
                {
                    spdlog::info("shutdown: stage1, step {} pipeline shutdown done, result={}", j, stage1Results[j].toString());
                }

                spdlog::info("shutdown_if_replay_done: final counters:");
                log_counters();
            }
            else
            {
                spdlog::info("shutdown_if_replay_done: replay still in progress");
                log_counters();
            }
        }
    };

    timer.setInterval(1000);
    timer.start();

    QObject::connect(&timer, &QTimer::timeout, [&]{
        if (signal_received())
        {
            spdlog::warn("signal received, shutting down");
            app.quit();
        }

        shutdown_if_replay_done();
    });

    int ret = app.exec();
    spdlog::warn("after app.exec(), forcing shutdown");
    replayContext.quit = true;
    auto &replayFuture = replayLoopRuntime->resultFuture;
    if (replayFuture.valid())
    {
        const auto ShutdownMaxWait = std::chrono::milliseconds(100);
        auto replayLoopResult = shutdown_loop(*replayLoopRuntime, ShutdownMaxWait);
        spdlog::info("shutdown: replay loop shutdown done, result={}", replayLoopResult.toString());

        for (auto &[crateId, rt]: cratePipelineRuntimes)
        {
            auto results = shutdown_pipeline(rt, ShutdownMaxWait);
            for (size_t j=0; j<results.size(); ++j)
            {
                spdlog::info("shutdown: crate {}, step {} pipeline shutdown done, result={}", crateId, j, results[j].toString());
            }
        }

        auto stage1Results = shutdown_pipeline(stage1Runtimes, ShutdownMaxWait);
        for (size_t j=0; j<stage1Results.size(); ++j)
        {
            spdlog::info("shutdown: stage1, step {} pipeline shutdown done, result={}", j, stage1Results[j].toString());
        }
    }
    spdlog::warn("after shutdown_if_replay_done()");

    spdlog::debug("end of main reached");

    for (auto &ctx: analysisStage0Contexts)
    {
        // circular reference: asp has shared_ptr<Analysis>, context has shared_ptr<Analysis> and unique_ptr<AnalysisServiceProvider>!
        ctx->asp = {};
        ctx->analysis = {};
    }

    return ret;
}
