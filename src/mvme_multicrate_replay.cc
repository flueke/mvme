#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QApplication>
#include <QTimer>

#include "mvlc/vmeconfig_to_crateconfig.h"
#include "util/signal_handling.h"
#include "mvme_session.h"
#include "multi_crate.h"
#include "vme_config_util.h"
#include "util/stopwatch.h"

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

    // Prepare sockets for the processing pipelines.

    std::vector<nng::SocketPipeline> pipelines; // one processing pipeline per crate

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        std::vector<nng::SocketPipeline::Link> links;

        const auto urlTemplates = { "inproc://crate{}_stage00_raw", "inproc://crate{}_stage01_parser" };

        for (auto tmpl: urlTemplates)
        {
            auto url = fmt::format(tmpl, i);
            auto [link, res] = nng::make_pair_link(url);
            if (res)
            {
                nng::mesy_nng_error(fmt::format("make_pair_link {}", url), res);
                std::for_each(links.begin(), links.end(), [](auto &link){
                    nng_close(link.dialer);
                    nng_close(link.listener);
                });
                return 1;
            }
            links.emplace_back(link);
        }

        auto pipeline = nng::SocketPipeline::fromLinks(links);
        pipelines.emplace_back(std::move(pipeline));
    }

    // Replay: one context with one output writer for each crate in the input listfile.

    MulticrateReplayContext replayContext;
    replayContext.quit = false;
    replayContext.lfh = readerHelper.readHandle;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &pipeline = pipelines[i];
        assert(pipeline.elements().size() >= 1);
        auto writerSocket = pipeline.elements()[0].outputSocket;
        auto writer = std::make_unique<nng::SocketOutputWriter>(writerSocket);
        writer->debugInfo = fmt::format("replayOutput (crateId={})", i);
        replayContext.writers.emplace_back(std::move(writer));
    }

    // Parsers: one parser per crate
    std::vector<std::unique_ptr<ReadoutParserNngContext>> parserContexts;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &pipeline = pipelines[i];
        assert(pipeline.elements().size() >= 2);
        auto crateConfig = vmeconfig_to_crateconfig(vmeConfigs[i].get());
        crateConfig.crateId = i; // FIXME: vmeconfig_to_crateconfig should set the crateId correctly

        auto inputReader = std::make_unique<nng::SocketInputReader>(pipeline.elements()[1].inputSocket);
        auto outputWriter = std::make_unique<nng::SocketOutputWriter>(pipeline.elements()[1].outputSocket);
        outputWriter->debugInfo = fmt::format("parserOutput (crateId={})", i);

        auto parserContext = make_readout_parser_nng_context(crateConfig);
        parserContext->quit = false;
        parserContext->inputReader = std::move(inputReader);
        parserContext->outputWriter = std::move(outputWriter);

        parserContexts.emplace_back(std::move(parserContext));
    }

    // Analysis: one per crate
    std::vector<std::unique_ptr<AnalysisProcessingContext>> analysisStage1Contexts;
    auto widgetRegistry = std::make_shared<WidgetRegistry>();

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        const auto &pipeline = pipelines[i];
        assert(pipeline.elements().size() >= 3);

        auto analysisContext = std::make_unique<AnalysisProcessingContext>();
        analysisContext->quit = false;
        analysisContext->inputReader = std::make_unique<nng::SocketInputReader>(pipeline.elements()[2].inputSocket);
        analysisContext->crateId = i;
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
        asp->widgetRegistry_ = widgetRegistry;
        analysisContext->asp = std::move(asp);
        analysis->beginRun(RunInfo{}, vmeConfigs[i].get());

        analysisStage1Contexts.emplace_back(std::move(analysisContext));
    }

    auto log_counters = [&]
    {
        // replay
        {
            auto ca = replayContext.writerCounters.access();
            const auto &counters = ca.ref();
            for(size_t crateId=0; crateId<counters.size(); ++crateId)
            {
                log_socket_work_counters(counters[crateId], fmt::format("replay_loop ouputWriter (crateId={})", crateId));
            }
        }

        // parsers
        for (auto &ctx: parserContexts)
        {
            log_socket_work_counters(ctx->counters.access().ref(),
                fmt::format("readout_parser_loop (crateId={})", ctx->crateId));
        }

        // analyses
        for (auto &ctx: analysisStage1Contexts)
        {
            log_socket_work_counters(ctx->inputSocketCounters.access().ref(),
                fmt::format("analysis_loop (crateId={})", ctx->crateId));
        }
    };

    // Runtime of the single(!) replay_loop.
    std::unique_ptr<LoopRuntime> replayLoopRuntime;

    // Per crate runtimes of the processing stages.
    std::vector<PipelineRuntime> cratePipelineRuntimes(vmeConfigs.size());

    // replay_loop
    {
        std::vector<nng::OutputWriter *> writers;
        for (auto &w: replayContext.writers)
            writers.push_back(w.get());

        auto replayFuture = std::async(std::launch::async, replay_loop, std::ref(replayContext));
        auto rt = LoopRuntime{ replayContext.quit, std::move(replayFuture), writers, "replay_loop" };
        replayLoopRuntime = std::make_unique<LoopRuntime>(std::move(rt));
    }

    // parser_loop[crate]
    for (auto &parserContext: parserContexts)
    {
        std::vector<nng::OutputWriter *> writers { parserContext->outputWriter.get() };

        auto future = std::async(std::launch::async, readout_parser_loop, std::ref(*parserContext));
        auto rt = LoopRuntime{ parserContext->quit, std::move(future), writers, fmt::format("readout_parser_loop (crateId={})", parserContext->crateId) };
        cratePipelineRuntimes[parserContext->crateId].emplace_back(std::move(rt));
    }

    // analysis_loop[crate]
    for (auto &analysisContext: analysisStage1Contexts)
    {
        auto future = std::async(std::launch::async, analysis_loop, std::ref(*analysisContext));
        auto rt = LoopRuntime{ analysisContext->quit, std::move(future), {}, fmt::format("analysis_loop (crateId={})", analysisContext->crateId) };
        cratePipelineRuntimes[analysisContext->crateId].emplace_back(std::move(rt));
    }

    StopWatch reportStopwatch;
    reportStopwatch.start();

    while (!signal_received())
    {
        auto &replayFuture = replayLoopRuntime->resultFuture;
        if (replayFuture.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready)
        {
            spdlog::debug("replay done, leaving main loop");
            break;
        }

        if (reportStopwatch.get_interval() >= std::chrono::milliseconds(1000))
        {
            log_counters();
            reportStopwatch.interval();
        }
    }

    if (signal_received())
    {
        spdlog::warn("signal received, shutting down");
    }
    else
    {
        spdlog::debug("replay finished, shutting down");
    }

    const auto ShutdownMaxWait = std::chrono::milliseconds(3 * 1000);

    replayContext.quit = true;
    auto replayLoopResult = shutdown_loop(*replayLoopRuntime, ShutdownMaxWait);

    assert(!replayLoopResult.hasError()); // XXX: debugging

    spdlog::debug("replay loop shutdown done");

    for (auto &pipelineRuntime: cratePipelineRuntimes)
    {
        auto loopResults = shutdown_pipeline(pipelineRuntime, ShutdownMaxWait);
    }

    spdlog::debug("crate processing pipelines shutdown done");

    // TODO: walk and show results

    #if 0
    if (auto &replayFuture = replayLoopRuntime->resultFuture; replayFuture.valid())
    {
        auto result = replayFuture.get();
        if (result.hasError())
        {
            if (result.ec)
            {
                auto ec = result.ec;
                spdlog::warn("result from replay loop: error_code={} ({})", ec.message(), ec.category().name());
            }
            else if (result.exception)
            {
                try
                {
                    std::rethrow_exception(result.exception);
                }
                catch(const std::runtime_error& e)
                {
                    spdlog::error("error from replay loop: {}", e.what());
                }
                catch(...)
                {
                    spdlog::error("unknown exception thrown from replay loop");
                }
            }
        }
    }
    #endif

    spdlog::info("final counters:");
    log_counters();

    spdlog::debug("end of main reached");

    return 0;
}
