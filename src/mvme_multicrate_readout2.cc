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
    parser.add_params({"--analysis", "--listfile"});
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
        else
            spdlog::set_level(spdlog::level::info);
    }

    trace_log_parser_info(parser, "mvme_multicrate_reaodut2");

    if (parser.pos_args().empty())
    {
        std::cerr << "Error: no vme configs given on command line\n";
        return 1;
    }

    std::string analysisFilename;
    parser("--analysis") >> analysisFilename;
    std::string outputListfilename;
    parser("--listfile") >> outputListfilename;

    struct VmeConfigs
    {
        std::unique_ptr<VMEConfig> vmeConfig;
        mvlc::CrateConfig crateConfig;
    };

    struct ReadoutApp
    {
        // VMEConfig and CrateConfig by crateId
        std::unordered_map<u8, VmeConfigs> vmeConfigs;
        std::unordered_map<u8, std::shared_ptr<mvme_mvlc::MVLC_VMEController>> mvlcs;
        std::unique_ptr<listfile::ZipCreator> listfileCreator;
        std::shared_ptr<ListfileWriterContext> listfileWriterContext;

        // standard crate processing contexts and pipelines
        std::unordered_map<u8, std::shared_ptr<MvlcInstanceReadoutContext>> readoutContexts;
        std::unordered_map<u8, std::shared_ptr<AnalysisProcessingContext>> analysisContexts;
        std::unordered_map<u8, std::vector<CratePipelineStep>> cratePipelines;
        CratePipeline listfileWriterPipeline;
    };

    ReadoutApp mesyApp;

    // For each pos arg:
    //   read vme config from arg
    //   convert to crateconfig
    //   add both to VmeConfigs

    // Read vme configs
    for (auto it = std::begin(parser.pos_args()) + 1; it<std::end(parser.pos_args()); ++it)
    {
        auto filename = *it;
        auto bytes = read_binary_file(QString::fromStdString(filename));
        auto [vmeConfig, ec] = vme_config::read_vme_config_from_data(bytes);

        if (!vmeConfig || ec)
        {
            std::cerr << fmt::format("Error reading vme config from {}: {}\n", filename, ec ? ec.message() : std::string("unknown error"));
            return 1;
        }

        auto crateConfig = vmeconfig_to_crateconfig(vmeConfig.get());

        if (mesyApp.vmeConfigs.find(crateConfig.crateId) != mesyApp.vmeConfigs.end())
        {
            std::cerr << fmt::format("Error: duplicate crateId {} in vme config from {}\n", crateConfig.crateId, filename);
            return 1;
        }

        mesyApp.vmeConfigs.emplace(crateConfig.crateId, VmeConfigs{std::move(vmeConfig), std::move(crateConfig)});
    }

    // Create MVLC instances
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

        auto mvlc = mvmeMvlc->getMVLC();
        mvlc.setDisableTriggersOnConnect(true);

        spdlog::info("crateId {}: MVLC={}", crateId, mvlc.connectionInfo());

        if (auto err = mvmeMvlc->open(); err.isError())
        {
            std::cerr << fmt::format("Error connecting to MVLC instance {} for crate {}: {}\n",
                mvmeMvlc->getMVLC().connectionInfo(), crateId, err.getStdErrorCode().message());
            return 1;
        }

        mesyApp.mvlcs.emplace(crateId, std::move(mvmeMvlc));
    }

    // Create the processing pipelines


    // readout -> pubsub -> parser
    for (auto &[crateId, configs]: mesyApp.vmeConfigs)
    {
        auto ctx = std::make_shared<MvlcInstanceReadoutContext>();
        ctx->crateId = crateId;
        ctx->mvlc = mesyApp.mvlcs[crateId]->getMVLC();
        auto tmpl = "inproc://crate{0}_stage0_step0_raw_data";
        auto url = fmt::format(tmpl, crateId);
        auto [outputLink, res] = nng::make_pubsub_link(url);
        if (res)
        {
            spdlog::error("Error creating outputlink {} for {}: {}", url, ctx->name(), nng_strerror(res));
            return 1;
        }
        auto step = make_readout_step(ctx, outputLink);
        step.context->setName(fmt::format("readout_loop{}", crateId));
        mesyApp.cratePipelines[crateId].emplace_back(std::move(step));
        mesyApp.readoutContexts[crateId] = ctx;
    }

    // readout parsers
    for (const auto &[crateId, configs]: mesyApp.vmeConfigs)
    {
        auto ctx = std::shared_ptr<ReadoutParserContext>(make_readout_parser_context(configs.crateConfig));
        ctx->setName(fmt::format("readout_parser_crate{}", crateId));
        auto tmpl = "inproc://crate{0}_stage0_step1_parsed_data";
        auto url = fmt::format(tmpl, ctx->crateId);
        auto [outputLink, res] = nng::make_pair_link(url);
        if (res)
        {
            spdlog::error("Error creating outputlink {} for {}: {}", url, ctx->name(), nng_strerror(res));
            return 1;
        }
        auto step = make_readout_parser_step(ctx, mesyApp.cratePipelines[crateId].back().outputLink, outputLink);
        mesyApp.cratePipelines[crateId].emplace_back(std::move(step));
    }

    if (!analysisFilename.empty())
    {
        // analysis consumers
        for (const auto &[crateId, configs]: mesyApp.vmeConfigs)
        {
            auto ctx = std::shared_ptr<AnalysisProcessingContext>(make_analysis_context(analysisFilename, configs.vmeConfig.get()));
            assert(ctx);
            if (ctx && ctx->analysis)
            {
                ctx->setName(fmt::format("analysis_crate{}", crateId));
                // TODO analysis object id handling / rewriting before beginRun()
                ctx->crateId = configs.crateConfig.crateId;
                ctx->isReplay = false;
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
        for (const auto &[crateId, _]: mesyApp.vmeConfigs)
        {
            auto ctx = std::make_shared<TestConsumerContext>();
            ctx->setName(fmt::format("test_consumer_crate{}", crateId));
            auto step = make_test_consumer_step(ctx, mesyApp.cratePipelines[crateId].back().outputLink);
            mesyApp.cratePipelines[crateId].emplace_back(std::move(step));
        }
    }

    // listfile: output file creation, pipeline link setup and writer configuration
    if (!outputListfilename.empty() && !mesyApp.mvlcs.empty())
    {
        // Want to write an output listfile. Create a new lossless link between
        // readouts and listfile writer.
        auto url = "inproc://listfile_writer";
        auto [link, res] = nng::make_pair_link(url);
        if (res)
        {
            spdlog::error("Error creating link for listfile writer (url={}): {}", url, nng_strerror(res));
            return 1;
        }

        try
        {
            mesyApp.listfileCreator = std::make_unique<listfile::ZipCreator>();
            mesyApp.listfileCreator->createArchive(outputListfilename);
            auto lfh = mesyApp.listfileCreator->createZIPEntry("listfile.mvlclst");
            spdlog::info("Opened output listfile {}", outputListfilename);
            // XXX: a single connectiontype at the start of the listfile
            // doesn't really make sense anymore. On the other hand we have to
            // keep it for existing code to be able to read the file. Just use
            // the first mvlc connectiontype for now.
            listfile::listfile_write_magic(*lfh, mesyApp.mvlcs.begin()->second->getMVLC().connectionType());
            listfile::listfile_write_endian_marker(*lfh, 0);

            for (auto &[crateId, configs]: mesyApp.vmeConfigs)
            {
                listfile::listfile_write_crate_config(*lfh, configs.crateConfig);
                mvme_mvlc::listfile_write_mvme_config(*lfh, crateId, *configs.vmeConfig);
            }

            auto ctx = std::make_shared<ListfileWriterContext>();
            ctx->setName("listfile_writer");
            ctx->lfh = std::move(lfh);
            mesyApp.listfileWriterContext = ctx;
        }
        catch(const std::exception& e)
        {
            spdlog::error("Error opening output listfile {}: {}", outputListfilename, e.what());
            return 1;
        }

        // add the link as an output to each readout instance
        for (auto &[crateId, pipeline]: mesyApp.cratePipelines)
        {
            auto writer = std::make_unique<nng::SocketOutputWriter>(link.listener);
            writer->debugInfo = fmt::format("crate{}_to_listfile_writer", crateId);
            pipeline[0].writer->addWriter(std::move(writer));
        }

        auto ctx = mesyApp.listfileWriterContext;
        auto step = make_listfile_writer_step(ctx, link);
        mesyApp.listfileWriterPipeline = { step };
    }

    // widgets

    for (auto &[crateId, ctx]: mesyApp.analysisContexts)
    {
        auto asp = ctx->asp.get();
        auto widget = new analysis::ui::AnalysisWidget(asp);
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->setWindowTitle(fmt::format("Analysis (crate {})", ctx->crateId).c_str());
        widget->show();
        add_widget_close_action(widget);
    }

    CratePipelineMonitorWidget monitorWidget;
    add_widget_close_action(&monitorWidget);

    for (auto &[crateId, steps]: mesyApp.cratePipelines)
    {
        monitorWidget.addPipeline(fmt::format("crate{}", crateId), steps);
    }

    if (!mesyApp.listfileWriterPipeline.empty())
    {
        monitorWidget.addPipeline("listfile_writer", mesyApp.listfileWriterPipeline);
    }

    monitorWidget.resize(1000, 400);
    monitorWidget.show();

    QWidget controlsWidget;
    add_widget_close_action(&controlsWidget);
    auto pbStart = new QPushButton("Start");
    auto pbStop = new QPushButton("Stop");
    pbStart->setEnabled(true);
    pbStop->setEnabled(false);
    {
        auto l = make_vbox(&controlsWidget);
        l->addWidget(pbStart);
        l->addWidget(pbStop);
    }

    auto log_counters = [&]
    {
        for (const auto &[crateId, steps]: mesyApp.cratePipelines)
        {
            for (const auto &step: steps)
            {
                if (step.reader)
                    log_socket_work_counters(step.context->readerCounters().copy(), fmt::format("step={}, reader", step.context->name()));
                if (step.writer)
                    log_socket_work_counters(step.context->writerCounters().copy(), fmt::format("step={}, writer", step.context->name()));
            }
        }

        for (const auto &step: mesyApp.listfileWriterPipeline)
        {
            if (step.reader)
                log_socket_work_counters(step.context->readerCounters().copy(), fmt::format("step={}, reader", step.context->name()));
            if (step.writer)
                log_socket_work_counters(step.context->writerCounters().copy(), fmt::format("step={}, writer", step.context->name()));
        }
    };

    auto start_readout = [&]
    {
        // init readouts
        for (auto &[crateId, configs]: mesyApp.vmeConfigs)
        {
            auto &mvlc = mesyApp.mvlcs[crateId];

            bool ignoreStartupErrors = false;
            // TODO: redirect logs to somewhere. Make available from the GUI.
            auto logger = [crateId=crateId] (const QString &msg) { spdlog::info("crate {}: {}", crateId, msg.toStdString()); };
            auto errorLogger = [crateId=crateId] (const QString &msg) { spdlog::error("crate {}: {}", crateId, msg.toStdString()); };

            auto b = mvme_mvlc::run_daq_start_sequence(mvlc.get(), *configs.vmeConfig, ignoreStartupErrors, logger, errorLogger);

            if (!b)
            {
                spdlog::error("Error starting DAQ for crate {}", crateId);
                return;
            }
        }

        // start the processing pipelines
        for (auto &[crateId, steps]: mesyApp.cratePipelines)
        {
            for (auto &step: steps)
            {
                if (!step.context->jobRuntime().isRunning())
                {
                    step.context->clearLastResult();
                    auto rt = start_job(*step.context);
                    spdlog::info("started job {}", step.context->name());
                    step.context->setJobRuntime(std::move(rt));
                }
                else
                {
                    spdlog::info("job {} was already running", step.context->name());
                }
            }
        }

        if (mesyApp.listfileWriterContext)
        {
            auto &step = mesyApp.listfileWriterPipeline[0];
            if (!step.context->jobRuntime().isRunning())
            {
                step.context->clearLastResult();
                auto rt = start_job(*step.context);
                spdlog::info("started job {}", step.context->name());
                step.context->setJobRuntime(std::move(rt));
            }
            else
            {
                spdlog::info("job {} was already running", step.context->name());
            }
        }

        // start the daq
        for (auto &[crateId, configs]: mesyApp.vmeConfigs)
        {
            auto mvlc = mesyApp.mvlcs[crateId]->getMVLC();

            if (auto ec = setup_readout_triggers(mvlc, configs.crateConfig.triggers))
            {
                spdlog::error("crate {}: error setting up readout triggers: {}", crateId, ec.message());
                return;
            }

            if (auto ec = enable_daq_mode(mvlc))
            {
                spdlog::error("crate {}: error enabling DAQ mode: {}", crateId, ec.message());
                return;
            }

            if (!configs.crateConfig.mcstDaqStart.empty())
            {
                auto results = run_commands(mvlc, configs.crateConfig.mcstDaqStart);
                for (const auto &result: results)
                {
                    spdlog::info("  crate{}: {} -> {}", crateId, to_string(result.cmd), result.ec.message());
                }

                if (auto ec = get_first_error(results))
                {
                    spdlog::error("crate {}: error running mcstDaqStart commands: {}", crateId, ec.message());
                    return;
                }
            }
        }
    };

    auto stop_readout = [&]
    {
        // TODO: send mcst daq stop sequence to all mvlcs
        // TODO: properly finish and close the output listfile

        // terminate pipelines
        for (auto &[crateId, steps]: mesyApp.cratePipelines)
        {
            if (!steps.empty())
                steps[0].context->quit(); // quit the readout_loop
            shutdown_pipeline(steps);
        }
        if (mesyApp.listfileWriterContext)
        {
            shutdown_pipeline(mesyApp.listfileWriterPipeline);
        }

        log_counters();
    };

    QObject::connect(pbStart, &QPushButton::clicked, &controlsWidget, [&] { start_readout(); pbStart->setEnabled(false); pbStop->setEnabled(true); });
    QObject::connect(pbStop, &QPushButton::clicked, &controlsWidget, [&] { stop_readout(); pbStop->setEnabled(false); });

    controlsWidget.show();

    QTimer timer;

    timer.setInterval(100);
    timer.start();

    QObject::connect(&timer, &QTimer::timeout, [&]
    {
        if (signal_received())
        {
            spdlog::warn("signal received, shutting down");
            app.quit();
        }

        //pbStart->setEnabled(!replayContext->jobRuntime().isRunning());
        //pbStop->setEnabled(!pbStart->isEnabled());
    });

    int ret = app.exec();

    stop_readout();

    return ret;
}
