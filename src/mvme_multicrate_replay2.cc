#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QApplication>
#include <QTimer>

#include "analysis/analysis_ui.h"
#include "analysis/analysis_util.h"
#include "multi_crate.h"
#include "multi_crate_nng.h"
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

    struct CratePipelineStep
    {
        SocketLink inputLink;
        SocketLink outputLink;
        int nngError = 0;
        std::shared_ptr<nng::InputReader> reader;
        std::shared_ptr<nng::MultiOutputWriter> writer;
        std::shared_ptr<JobContextInterface> context;
    };

    auto make_replay_step = [](const std::shared_ptr<ReplayJobContext> &replayContext, u8 crateId)
    {

        auto tmpl = "inproc://crate{0}_stage0_step0_raw_data";
        auto url = fmt::format(tmpl, crateId);
        auto [link, res] = nng::make_pair_link(url);
        CratePipelineStep result;
        result.outputLink = link;
        result.nngError = res;
        if (res)
            return result;

        auto writer = std::make_unique<nng::SocketOutputWriter>(link.listener);
        writer->debugInfo = fmt::format("replay_loop (crateId={})", crateId);
        writer->retryPredicate = [ctx=replayContext.get()] { return !ctx->shouldQuit(); };

        auto writerWrapper = std::make_shared<nng::MultiOutputWriter>();
        writerWrapper->addWriter(std::move(writer));

        replayContext->setOutputWriterForCrate(crateId, writerWrapper.get());

        result.writer = writerWrapper;
        result.context = replayContext;
        return result;
    };

    auto make_readout_parser_step = [](const std::shared_ptr<ReadoutParserContext> &context, SocketLink inputLink)
    {
        auto tmpl = "inproc://crate{0}_stage0_step1_parsed_data";
        auto url = fmt::format(tmpl, context->crateId);
        auto [outputLink, res] = nng::make_pair_link(url);
        CratePipelineStep result;
        result.outputLink = outputLink;
        result.nngError = res;
        if (res)
            return result;

        auto reader = std::make_shared<nng::SocketInputReader>(inputLink.dialer);
        reader->debugInfo = context->name();
        context->setInputReader(reader.get());

        auto writer = std::make_unique<nng::SocketOutputWriter>(outputLink.listener);
        writer->debugInfo = context->name();

        auto writerWrapper = std::make_shared<nng::MultiOutputWriter>();
        writerWrapper->addWriter(std::move(writer));

        context->setOutputWriter(writerWrapper.get());

        result.inputLink = inputLink;
        result.outputLink = outputLink;
        result.reader = reader;
        result.writer = writerWrapper;
        result.context = context;
        return result;
    };

    auto make_analysis_step = [] (const std::shared_ptr<AnalysisProcessingContext> &context, SocketLink inputLink)
    {
        auto reader = std::make_shared<nng::SocketInputReader>(inputLink.dialer);
        reader->debugInfo = context->name();
        context->setInputReader(reader.get());

        CratePipelineStep result;
        result.inputLink = inputLink;
        result.reader = reader;
        result.context = context;
        return result;
    };

    auto make_test_consumer_step = [] (const std::shared_ptr<TestConsumerContext> &context, SocketLink inputLink)
    {
        auto reader = std::make_shared<nng::SocketInputReader>(inputLink.dialer);
        reader->debugInfo = context->name();
        context->setInputReader(reader.get());

        CratePipelineStep result;
        result.inputLink = inputLink;
        result.reader = reader;
        result.context = context;
        return result;
    };

    auto replayContext = std::make_shared<ReplayJobContext>();
    replayContext->setName("replay_loop");
    replayContext->lfh = listfileReadHandle;

    std::unordered_map<u8, std::vector<CratePipelineStep>> cratePipelineSteps;

    // producer steps
    for (const auto &[crateId, _]: vmeConfigs)
    {
        auto step = make_replay_step(replayContext, crateId);
        cratePipelineSteps[crateId].emplace_back(step);
    }

    // readout parsers
    for (const auto &[crateId, configs]: vmeConfigs)
    {
        auto ctx = std::shared_ptr<ReadoutParserContext>(make_readout_parser_context(configs.crateConfig));
        ctx->setName(fmt::format("readout_parser_crate{}", crateId));
        auto step = make_readout_parser_step(ctx, cratePipelineSteps[crateId].back().outputLink);
        cratePipelineSteps[crateId].emplace_back(step);
    }

    std::vector<std::shared_ptr<AnalysisProcessingContext>> analysisContexts;

    if (!analysisFilename.empty())
    {
        // analysis consumers
        for (const auto &[crateId, configs]: vmeConfigs)
        {
            auto ctx = std::shared_ptr<AnalysisProcessingContext>(make_analysis_context(analysisFilename, configs.vmeConfig.get()));
            assert(ctx);
            if (ctx && ctx->analysis)
            {
                ctx->setName(fmt::format("analysis_crate{}", crateId));
                // TODO analysis object id handling / rewriting before beginRun()
                ctx->crateId = configs.crateConfig.crateId;
                ctx->isReplay = true;
                ctx->analysis->beginRun(RunInfo{}, ctx->asp->vmeConfig_);
                auto step = make_analysis_step(ctx, cratePipelineSteps[crateId].back().outputLink);
                cratePipelineSteps[crateId].emplace_back(step);
                analysisContexts.emplace_back(ctx);
            }
        }
    }
    else
    {
        // test consumers
        for (const auto &[crateId, _]: vmeConfigs)
        {
            auto ctx = std::make_shared<TestConsumerContext>();
            ctx->setName(fmt::format("test_consumer_crate{}", crateId));
            auto step = make_test_consumer_step(ctx, cratePipelineSteps[crateId].back().outputLink);
            cratePipelineSteps[crateId].emplace_back(step);
        }
    }

    for (auto &[crateId, steps]: cratePipelineSteps)
    {
        for (auto &step: steps)
        {
            if (!step.context->jobRuntime().isRunning())
            {
                auto rt = start_job(*step.context);
                spdlog::info("started job {}", step.context->name());
                step.context->setJobRuntime(std::move(rt));
            }
        }
    }

    spdlog::info("waiting for replay to finish");

    while (replayContext->jobRuntime().isRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("replay finished, starting shutdown");

    for (auto &[crateId, steps]: cratePipelineSteps)
    {
        for (auto &step: steps)
        {
            spdlog::info("waiting for job {} to finish", step.context->name());
            auto result = step.context->jobRuntime().wait();
            spdlog::info("job {} finished: {}", step.context->name(), result.toString());
            if (step.writer)
                send_shutdown_message(*step.writer);
            spdlog::info("shutdown messages sent from {}, waiting for jobs to finish", step.context->name());
        }
    }

    auto log_counters = [&]
    {
        for (size_t i=0; i<replayContext->outputWritersByCrate.size(); ++i)
        {
            if (replayContext->outputWritersByCrate[i])
            {
                log_socket_work_counters(replayContext->writerCountersByCrate.access().ref()[i], fmt::format("replay_output (crateId={})", i));
            }
        }

        for (const auto &[crateId, steps]: cratePipelineSteps)
        {
            for (const auto &step: steps)
            {
                if (step.reader)
                    log_socket_work_counters(step.context->readerCounters().copy(), fmt::format("step={}, reader", step.context->name()));
                if (step.writer)
                    log_socket_work_counters(step.context->writerCounters().copy(), fmt::format("step={}, writer", step.context->name()));
            }
        }
    };

    for (auto &ctx: analysisContexts)
    {
        auto asp = ctx->asp.get();
        auto widget = new analysis::ui::AnalysisWidget(asp);
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->setWindowTitle(fmt::format("Analysis (crate {})", ctx->crateId).c_str());
        widget->show();
    }

    log_counters();

    if (!analysisContexts.empty())
        ret = app.exec();

    return ret;
}
