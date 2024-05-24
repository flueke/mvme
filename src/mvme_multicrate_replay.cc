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

    std::vector<std::pair<nng_socket, nng_socket>> readoutSnoopSockets; // readout writes, parser reads
    std::vector<std::pair<nng_socket, nng_socket>> parsedDataSockets; // parser writes, eventbuilder reads

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        auto uri = fmt::format("inproc://readoutDataSnoop{}", i);
        nng_socket writerSocket = nng::make_pair_socket();
        nng_socket readerSocket = nng::make_pair_socket();

        if (int res = nng::marry_listen_dial(writerSocket, readerSocket, uri.c_str()))
        {
            nng::mesy_nng_error("marry_listen_dial readoutDataSnoop", res);
            return 1;
        }

        readoutSnoopSockets.emplace_back(std::make_pair(writerSocket, readerSocket));
    }

    // parsers -> eventbuilders stage 1
    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        auto uri = fmt::format("inproc://parsedData{}", i);

        nng_socket outputSocket = nng::make_pub_socket();
        nng_socket inputSocket = nng::make_sub_socket();
        //nng_socket outputSocket = nng::make_pair_socket();
        //nng_socket inputSocket = nng::make_pair_socket();

        if (int res = nng::marry_listen_dial(outputSocket, inputSocket, uri.c_str()))
        {
            nng::mesy_nng_error("marry_listen_dial parsedData", res);
            return 1;
        }

        parsedDataSockets.emplace_back(std::make_pair(outputSocket, inputSocket));
    }

    MulticrateReplayContext replayContext;
    replayContext.quit = false;
    replayContext.lfh = readerHelper.readHandle;

    {
        unsigned crateId = 0;

        for (const auto &[writerSocket, _]: readoutSnoopSockets)
        {
            auto writer = std::make_unique<nng::SocketOutputWriter>(writerSocket);
            writer->debugInfo = fmt::format("replayOutput (crateId={})", crateId++);
            replayContext.writers.emplace_back(std::move(writer));
        }
    }

    std::vector<std::unique_ptr<ReadoutParserNngContext>> parserContexts;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        auto crateConfig = vmeconfig_to_crateconfig(vmeConfigs[i].get());
        crateConfig.crateId = i; // FIXME: vmeconfig_to_crateconfig should set the crateId correctly
        auto parserContext = make_readout_parser_nng_context(crateConfig);
        parserContext->quit = false;
        parserContext->inputReader = std::make_unique<nng::SocketInputReader>(readoutSnoopSockets[i].second);
        parserContext->outputWriter = std::make_unique<nng::SocketOutputWriter>(parsedDataSockets[i].first);
        parserContexts.emplace_back(std::move(parserContext));
    }

    auto log_counters = [&]
    {
        for (auto &ctx: parserContexts)
        {
            log_socket_work_counters(ctx->counters.access().ref(),
                fmt::format("readout_parser_loop (crateId={})", ctx->crateId));
        }

        {
            auto ca = replayContext.writerCounters.access();
            const auto &counters = ca.ref();
            for(size_t crateId=0; crateId<counters.size(); ++crateId)
            {
                log_socket_work_counters(counters[crateId], fmt::format("replay_loop ouputWriter (crateId={})", crateId));
            }
        }
    };

    PipelineRuntime pipeline;

    {
        std::vector<nng::OutputWriter *> writers;

        for (auto &w: replayContext.writers)
            writers.push_back(w.get());

        auto replayFuture = std::async(std::launch::async, replay_loop, std::ref(replayContext));
        LoopRuntime runtime { replayContext.quit, std::move(replayFuture), writers };
        pipeline.emplace_back(std::move(runtime));
    }

    std::vector<std::future<void>> parserFutures;

    for (auto &parserContext: parserContexts)
    {
        parserFutures.emplace_back(std::async(std::launch::async, [&parserContext](){
            readout_parser_loop(*parserContext);
        }));
    }

    #if 1
    StopWatch reportStopwatch;
    reportStopwatch.start();

    while (!signal_received())
    {
        auto &replayFuture = pipeline[0].resultFuture;
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

    replayContext.quit = true;

    if (auto &replayFuture = pipeline[0].resultFuture; replayFuture.valid())
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

    spdlog::debug("sending shutdown messages through replay output writers");
    #if 0
    for (auto &writer: replayContext.writers)
    {
        if (writer)
            send_shutdown_message(*writer);
    }
    #else
    auto results = shutdown_pipeline(pipeline);
    #endif

    spdlog::info("final counters:");
    log_counters();

    #else
    QTimer periodicTimer;
    periodicTimer.setInterval(1000);
    periodicTimer.start();

    QObject::connect(&periodicTimer, &QTimer::timeout, [&]
    {
        if (signal_received())
            app.quit();
    }


    int ret = app.exec();
    #endif

    spdlog::debug("end of main reached");

    return 0;
}
