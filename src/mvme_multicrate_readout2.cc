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

    struct ReadoutApp
    {
        std::unordered_map<u8, VmeConfigs> vmeConfigs;
        std::unordered_map<u8, std::shared_ptr<mvme_mvlc::MVLC_VMEController>> mvlcs;
        std::unordered_map<u8, std::shared_ptr<MvlcInstanceReadoutContext>> readoutContexts;
        std::unordered_map<u8, std::shared_ptr<AnalysisProcessingContext>> analysisContexts;
        std::unordered_map<u8, std::vector<CratePipelineStep>> cratePipelines;
    };

    ReadoutApp mesyApp;
    mesyApp.vmeConfigs = std::move(vmeConfigs);

    for (auto &[crateId, configs]: mesyApp.vmeConfigs)
    {
        // build the mvme VMEController subclass for the mvlc. It's needed for the mvme vme init sequence.
        VMEControllerFactory factory(configs.vmeConfig->getControllerType());
        auto controller = std::unique_ptr<VMEController>(factory.makeController(configs.vmeConfig->getControllerSettings()));
        auto mvmeMvlc = std::unique_ptr<mvme_mvlc::MVLC_VMEController>(qobject_cast<mvme_mvlc::MVLC_VMEController *>(controller.get()));

        if (!mvmeMvlc)
        {
            std::cerr << fmt::format("Error creating MVLC instance crate {}\n", crateId);
            return 1;
        }

        controller.release(); // ownvership went to mvmeMvlc

        if (auto err = mvmeMvlc->open(); err.isError())
        {
            std::cerr << fmt::format("Error connecting to MVLC instance {} for crate {}: {}\n",
                mvmeMvlc->getMVLC().connectionInfo(), crateId, err.getStdErrorCode().message());
            return 1;
        }

        mesyApp.mvlcs.emplace(crateId, std::move(mvmeMvlc));
    }

    for (auto &[crateId, configs]: mesyApp.vmeConfigs)
    {
        auto ctx = std::make_shared<MvlcInstanceReadoutContext>();
        ctx->crateId = crateId;
        ctx->mvlc = mesyApp.mvlcs[crateId]->getMVLC();
        auto tmpl = "inproc://crate{0}_stage0_step0_raw_data";
        auto url = fmt::format(tmpl, crateId);
        auto step = make_readout_step(ctx, url);
        step.context->setName(fmt::format("readout_loop{}", crateId));
        mesyApp.cratePipelines[crateId].emplace_back(std::move(step));
    }

    // readout parsers
    for (const auto &[crateId, configs]: vmeConfigs)
    {
        auto ctx = std::shared_ptr<ReadoutParserContext>(make_readout_parser_context(configs.crateConfig));
        ctx->setName(fmt::format("readout_parser_crate{}", crateId));
        auto tmpl = "inproc://crate{0}_stage0_step1_parsed_data";
        auto url = fmt::format(tmpl, ctx->crateId);
        auto step = make_readout_parser_step(ctx, mesyApp.cratePipelines[crateId].back().outputLink, url);
        mesyApp.cratePipelines[crateId].emplace_back(std::move(step));
    }

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
                auto step = make_analysis_step(ctx, mesyApp.cratePipelines[crateId].back().outputLink);
                mesyApp.cratePipelines[crateId].emplace_back(std::move(step));
                mesyApp.analysisContexts.emplace(crateId, ctx);
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
            auto step = make_test_consumer_step(ctx, mesyApp.cratePipelines[crateId].back().outputLink);
            mesyApp.cratePipelines[crateId].emplace_back(std::move(step));
        }
    }

    // TODO: init readouts
    // TODO: start the pipelines

    int ret = 0;
    return ret;
}
