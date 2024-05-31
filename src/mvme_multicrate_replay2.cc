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

    struct VmeConfigs
    {
        std::unique_ptr<VMEConfig> vmeConfig;
        mvlc::CrateConfig crateConfig;
    };

    // VMEConfig and CrateConfig by crateId
    std::unordered_map<u8, VmeConfigs> vmeConfigs;

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

            auto crateConfig = vmeconfig_to_crateconfig(vmeConfig.get());

            if (vmeConfigs.find(crateConfig.crateId) != vmeConfigs.end())
            {
                std::cerr << fmt::format("Error: duplicate crateId {} in listfile\n", crateConfig.crateId);
                return 1;
            }

            vmeConfigs.emplace(crateConfig.crateId, VmeConfigs{std::move(vmeConfig), std::move(crateConfig)});
        }
    }

    fmt::print("Read {} vme configs from {}\n", vmeConfigs.size(), listfileFilename);

    int ret = 0;

    using SocketLink = nng::SocketPipeline::Link;

    std::unordered_map<u8, std::vector<SocketLink>> crateSocketLinks;

    auto close_links = [&crateSocketLinks] ()
    {
        for (auto & [crateId, links]: crateSocketLinks)
            std::for_each(links.begin(), links.end(), nng::close_link);
    };

    auto replayContext = std::make_shared<ReplayJobContext>();
    replayContext->setName("replay_loop");
    replayContext->lfh = listfileReadHandle;

    std::unordered_map<u8, std::shared_ptr<TestConsumerContext>> testConsumerContexts;

    for (const auto &[crateId, _]: vmeConfigs)
    {
        auto ctx = std::make_shared<TestConsumerContext>();
        ctx->setName(fmt::format("test_consumer_crate{}", crateId));
        testConsumerContexts.emplace(crateId, std::move(ctx));
    }

    std::vector<std::shared_ptr<nng::OutputWriter>> outputWriters;
    std::vector<std::shared_ptr<nng::InputReader>> inputReaders;

    for (const auto &[crateId, _]: vmeConfigs)
    {
        auto tmpl = "inproc://crate{0}_stage0_step0_raw_data";
        auto url = fmt::format(tmpl, crateId);
        auto [link, res] = nng::make_pair_link(url);
        if (res)
        {
            nng::mesy_nng_error(fmt::format("make_pair_link {}", url), res);
            close_links();
            return 1;
        }

        crateSocketLinks[crateId].push_back(link);

        auto writer = std::make_shared<nng::SocketOutputWriter>(link.listener);
        writer->debugInfo = fmt::format("replay_loop (crateId={})", crateId);
        writer->retryPredicate = [ctx=replayContext.get()] { return !ctx->shouldQuit(); };
        outputWriters.push_back(writer);
        replayContext->addOutputWriterForCrate(crateId, writer.get());

        auto reader = std::make_shared<nng::SocketInputReader>(link.dialer);
        reader->debugInfo = fmt::format("replay_loop (crateId={})", crateId);
        testConsumerContexts[crateId]->setInputReader(reader.get());
    }

    auto replayRuntime = start_job(*replayContext);

    std::unordered_map<u8, std::vector<JobRuntime>> crateJobRuntimes;

    for (auto &[crateId, ctx]: testConsumerContexts)
    {
        auto rt = start_job(*ctx);
        crateJobRuntimes[crateId].emplace_back(std::move(rt));
    }

    spdlog::info("waiting for replay to finish");

    while (replayRuntime.isRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto replayResult = replayRuntime.wait();

    spdlog::info("replay finished: {}", replayResult.toString());
    spdlog::info("sending shutdown messages");

    for (auto writer: replayRuntime.context->outputWriters())
    {
        send_shutdown_message(*writer);
    }

    spdlog::info("shutdown messages sent, waiting for jobs to finish");

    for (auto &[crateId, jobs]: crateJobRuntimes)
    {
        for (auto &job: jobs)
        {
            spdlog::info("waiting for job {} to finish", job.context->name());
            auto result = job.wait();
            spdlog::info("job {} finished: {}", job.context->name(), result.toString());
        }
    }

    auto log_counters = [&]
    {
        for (const auto &counters: replayContext->writerCounters().copy())
        {
            log_socket_work_counters(counters, "replay_loop");
        }

        for (const auto &[crateId, ctx]: testConsumerContexts)
        {
            log_socket_work_counters(ctx->readerCounters().copy(), fmt::format("test_consumer_crate{}", crateId));
        }
    };

    log_counters();

    return ret;
}
