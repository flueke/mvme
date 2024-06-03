#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>

#include "analysis/analysis_ui.h"
#include "analysis/analysis_util.h"
#include "multi_crate.h"
#include "multi_crate_nng.h"
#include "multi_crate_nng_gui.h"
#include "mvlc_daq.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_session.h"
#include "qt_util.h"
#include "util/qt_fs.h"
#include "util/qt_monospace_textedit.h"
#include "util/signal_handling.h"
#include "util/stopwatch.h"
#include "vme_config_util.h"
#include "vme_controller_factory.h"
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

    trace_log_parser_info(parser, "mvme_multicrate_reaodut2");

    if (parser.pos_args().empty())
    {
        std::cerr << "Error: no vme configs given on command line\n";
        return 1;
    }

    std::string analysisFilename;
    parser("--analysis") >> analysisFilename;

    struct VmeConfigs
    {
        std::unique_ptr<VMEConfig> vmeConfig;
        mvlc::CrateConfig crateConfig;
    };

    // VMEConfig and CrateConfig by crateId
    std::unordered_map<u8, VmeConfigs> vmeConfigs;

    // For each pos arg:
    //   read vme config from arg
    //   convert to crateconfig
    //   add both to VmeConfigs

    for (const auto &filename: parser.pos_args())
    {
        auto bytes = read_binary_file(QString::fromStdString(filename));
        auto [vmeConfig, ec] = vme_config::read_vme_config_from_data(bytes);

        if (!vmeConfig || ec)
        {
            std::cerr << fmt::format("Error reading vme config from {}: {}\n", filename, ec ? ec.message() : std::string("unknown error"));
            return 1;
        }

        auto crateConfig = vmeconfig_to_crateconfig(vmeConfig.get());

        if (vmeConfigs.find(crateConfig.crateId) != vmeConfigs.end())
        {
            std::cerr << fmt::format("Error: duplicate crateId {} in vme config from {}\n", crateConfig.crateId, filename);
            return 1;
        }

        vmeConfigs.emplace(crateConfig.crateId, VmeConfigs{std::move(vmeConfig), std::move(crateConfig)});
    }

    auto make_readout_step = [] (const std::shared_ptr<MvlcInstanceReadoutContext> &ctx)
    {
        auto tmpl = "inproc://crate{0}_stage0_step0_raw_data";
        auto url = fmt::format(tmpl, ctx->crateId);
        auto [link, res] = nng::make_pair_link(url);
        CratePipelineStep result;
        result.outputLink = link;
        result.nngError = res;
        if (res)
            return result;

        auto writer = std::make_unique<nng::SocketOutputWriter>(link.listener);
        writer->debugInfo = fmt::format("readout_loop (crateId={})", ctx->crateId);
        writer->retryPredicate = [ctx=ctx.get()] { return !ctx->shouldQuit(); };

        auto writerWrapper = std::make_shared<nng::MultiOutputWriter>();
        writerWrapper->addWriter(std::move(writer));

        result.writer = writerWrapper;
        result.context = ctx;
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

    struct ReadoutApp
    {
        std::unordered_map<u8, VmeConfigs> vmeConfigs;
        std::unordered_map<u8, std::shared_ptr<mvme_mvlc::MVLC_VMEController>> mvlcs;
        std::unordered_map<u8, std::shared_ptr<MvlcInstanceReadoutContext>> readoutContexts;
        std::unordered_map<u8, std::vector<CratePipelineStep>> cratePipelineSteps;
    };

    ReadoutApp mesyApp;
    mesyApp.vmeConfigs = std::move(vmeConfigs);

    for (auto &[crateId, configs]: mesyApp.vmeConfigs)
    {
        VMEControllerFactory factory(configs.vmeConfig->getControllerType());
        auto controller = std::unique_ptr<VMEController>(factory.makeController(configs.vmeConfig->getControllerSettings()));
        auto mvlc = std::unique_ptr<mvme_mvlc::MVLC_VMEController>(qobject_cast<mvme_mvlc::MVLC_VMEController *>(controller.get()));

        if (!mvlc)
        {
            std::cerr << fmt::format("Error creating MVLC instance crate {}\n", crateId);
            return 1;
        }

        controller.release();
        mesyApp.mvlcs.emplace(crateId, std::move(mvlc));
    }

    for (auto &[crateId, configs]: mesyApp.vmeConfigs)
    {
        auto ctx = std::make_shared<MvlcInstanceReadoutContext>();
        ctx->crateId = crateId;
        ctx->mvlc = mesyApp.mvlcs[crateId]->getMVLC();
        auto step = make_readout_step(ctx);
        step.context->setName(fmt::format("readout_loop{}", crateId));
        mesyApp.cratePipelineSteps[crateId].emplace_back(std::move(step));
    }

    // build pipeline for each crate:
    //  readout -> listfile
    //          -> parser -> analysis

    // For each crateconfig:
    //   create mvlc instance
    //   initialize readout
    //   start readout


    int ret = 0;
    return ret;
}
