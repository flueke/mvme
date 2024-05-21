#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/util/udp_sockets.h>
#include <QApplication>
#include <QTimer>
#include <signal.h>
#include <map>

#ifndef __WIN32
#include <poll.h>
#else
#include <winsock2.h>
#endif

#include "analysis/analysis.h"
#include "analysis/analysis_ui.h"
#include "multi_crate.h"
#include "mvlc_daq.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"
#include "mvme_session.h"
#include "util/mesy_nng.h"
#include "util/mesy_nng_pipeline.h"
#include "util/stopwatch.h"
#include "util/qt_monospace_textedit.h"
#include "vme_config.h"
#include "vme_config_tree.h"

#ifdef MVME_ENABLE_PROMETHEUS
#include "mvme_prometheus.h"
#endif

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvme;
using namespace mesytec::mvme::multi_crate;

static std::atomic<bool> signal_received = false;

#ifndef __WIN32
void signal_handler(int signum)
{
    std::cerr << "signal " << signum << "\n";
    std::cerr.flush();
    signal_received = true;
}

void setup_signal_handlers()
{
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
}
#else
BOOL CtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
        printf("\n\nCTRL-C pressed, exiting.\n\n");
        signal_received = true;
        return (TRUE);
    default:
        return (FALSE);
    }
}

void setup_signal_handlers()
{
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
    {
        throw std::runtime_error("Error setting Console-Ctrl Handler\n");
    }
}
#endif

#if 0
struct ParsedDataStatsContext
{
    std::atomic<bool> quit;
    nng_socket inputSocket;
    std::string info;
};

void parsed_data_stats_loop(ParsedDataStatsContext &context)
{
    set_thread_name("parsed_stats_loop");

    while (!context.quit)
    {
        nng_msg *inputMsg = nullptr;

        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("parsed_data_stats ({}) - receive_message: {}",
                    context.info, nng_strerror(res));
                break;
            }
            spdlog::trace("parsed_data_stats ({}) - receive_message: timeout", context.info);
            continue;
        }
    }
}
#endif

int main(int argc, char *argv[])
{
    std::string generalHelp = R"~(
        write the help text please!
)~";

    QApplication app(argc, argv);
    mvme_init("mvme_multicrate_collector", false);
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
    }

    trace_log_parser_info(parser, "mvlc-cli");

    if (parser.pos_args().size() <= 1)
    {
        std::cerr << "Error: no vme configs given on command line\n";
        return 1;
    }

    // Read vme and analysis configs

    std::vector<std::unique_ptr<VMEConfig>> vmeConfigs;

    for (size_t i=1; i<parser.pos_args().size(); ++i)
    {
        auto filename = QString::fromStdString(parser.pos_args().at(i));
        auto [vmeConfig, errorString ] = read_vme_config_from_file(filename);
        if (!vmeConfig)
        {
            std::cerr << fmt::format("Error reading mvme vme config from '{}': {}\n",
                filename.toStdString(), errorString.toStdString());
            return 1;
        }

        vmeConfigs.emplace_back(std::move(vmeConfig));
    }

    // Read in the single analyis config file and create an analysis instance from it.
    // This will be cloned for each crate and for the multi crate stage2 analysis.
    std::string analysisConfigFilename;
    parser("--analysis") >> analysisConfigFilename;

    std::vector<std::shared_ptr<analysis::Analysis>> analysisConfigs;
    for (size_t i=0; i<=vmeConfigs.size(); ++i) // one more for the multi crate stage2 analysis
    {
        auto [ana, errorString] = analysis::read_analysis_config_from_file(analysisConfigFilename.c_str());

        if (!ana)
        {
            std::cerr << fmt::format("Error reading mvme analysis config from '{}': {}\n",
                analysisConfigFilename, errorString.toStdString());
            return 1;
        }

        analysisConfigs.emplace_back(std::move(ana));
    }

    auto widgetRegistry = std::make_shared<WidgetRegistry>();
    std::vector<std::unique_ptr<multi_crate::MinimalAnalysisServiceProvider>> asps;

    for (size_t i=0; i<std::min(vmeConfigs.size(), analysisConfigs.size()); ++i)
    {
        auto asp = std::make_unique<multi_crate::MinimalAnalysisServiceProvider>();
        asp->vmeConfig_ = vmeConfigs[i].get();
        asp->analysis_ = analysisConfigs[i];
        asp->widgetRegistry_ = widgetRegistry;

        asps.emplace_back(std::move(asp));
    }

    // Create sockets for the mvlc data pipes.
    std::vector<int> mvlcDataSockets;

    for (auto &vmeConfig: vmeConfigs)
    {
        auto settings = vmeConfig->getControllerSettings();
        if (!settings.contains("mvlc_hostname"))
        {
            std::cerr << fmt::format("non ETH mvlc controller found in vme config!\n");
            return 1;
        }

        auto host = settings.value("mvlc_hostname").toString().toStdString();
        auto port = eth::DataPort;
        std::error_code ec;
        auto sock = eth::connect_udp_socket(host, port, &ec);

        if (ec)
        {
            std::cerr << fmt::format("Error connecting to '{}': {}\n", host, ec.message());
            return 1;
        }

        mvlcDataSockets.emplace_back(sock);

        std::cout << fmt::format("Created data pipe socket for MVLC {}\n", host);
    }

    // Redirect the data streams to our sockets
    for (auto sock: mvlcDataSockets)
    {
        static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
        size_t bytesTransferred = 0u;

        if (auto ec = eth::write_to_socket(sock,
            reinterpret_cast<const u8 *>(EmptyRequest.data()),
            EmptyRequest.size() * sizeof(u32),
            bytesTransferred))
        {
            std::cerr << fmt::format("Error redirecting MVLC ETH data stream: {}\n",
                ec.message());
            return 1;
        }
    }

    std::vector<mvlc::CrateConfig> crateConfigs;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        auto crateConfig = vmeconfig_to_crateconfig(vmeConfigs[i].get());
        crateConfig.crateId = i; // FIXME: vmeconfig_to_crateconfig should set the crateId correctly

        crateConfigs.emplace_back(std::move(crateConfig));
    }

    // Open the output listfile if one should be written. // TODO: move this out of the context creation stuff.
    std::unique_ptr<listfile::ZipCreator> listfileCreator;
    std::unique_ptr<listfile::WriteHandle> lfh;

    std::string outputListfilename;
    if (parser("--listfile") >> outputListfilename)
    {
        try
        {
            listfileCreator = std::make_unique<listfile::ZipCreator>();
            listfileCreator->createArchive(outputListfilename);
            lfh = listfileCreator->createZIPEntry("listfile.mvlclst");
            spdlog::info("Opened output listfile {}", outputListfilename);
            listfile::listfile_write_magic(*lfh, ConnectionType::ETH);
            listfile::listfile_write_endian_marker(*lfh, 0);

            for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
            {
                auto crateConfig = vmeconfig_to_crateconfig(vmeConfigs[crateId].get());
                crateConfig.crateId = crateId;
                listfile::listfile_write_crate_config(*lfh, crateConfig);
                mvme_mvlc::listfile_write_mvme_config(*lfh, crateId, *vmeConfigs[crateId]);
            }
        }
        catch(const std::exception& e)
        {
            spdlog::error("Error opening output listfile {}: {}", outputListfilename, e.what());
            return 1;
        }
    }

#ifdef MVME_ENABLE_PROMETHEUS
    // This variable is here to keep the prom context alive in main! This is to
    // avoid a hang when the internal civetweb instance is destroyed from within
    // a DLL (https://github.com/civetweb/civetweb/issues/264). By having this
    // variable on the stack the destructor is called from mvme.exe, not from
    // within libmvme.dll.
    std::shared_ptr<mesytec::mvme::PrometheusContext> prom;
    try
    {
        auto promBindAddress = QSettings().value("PrometheusBindAddress", "0.0.0.0:13803").toString().toStdString();
        prom = std::make_shared<mesytec::mvme::PrometheusContext>();
        prom->start(promBindAddress);
        std::cout << "Prometheus server listening on port " << prom->exposer()->GetListeningPorts().front() << "\n";
        mesytec::mvme::set_prometheus_instance(prom); // Register the prom object globally.
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error creating prometheus context: " << e.what() << ". Prometheus metrics not available!\n";
    }

    // TODO: design and add actual metrics here
#endif
    // All readout threads write messages of type
    // multi_crate::MessageType::ReadoutData to the readoutProducerSocket.
    nng_socket readoutProducerSocket = nng::make_pair_socket();

    // A single(!) thread reads ReadoutData messages from this socket and
    // writes a listfile. Do not connect multiple pull sockets to the
    // readoutProducerSocket as that will round-robin distribute the messages!
    nng_socket listfileConsumerSocket = nng::make_pair_socket();

    if (int res = nng::marry_listen_dial(readoutProducerSocket, listfileConsumerSocket, "inproc://readoutData"))
    {
        nng::mesy_nng_error("marry_listen_dial readoutData", res);
        return 1;
    }

    // Per crate. Output data is written to the first socket. Input data is read
    // from the second socket.
    std::vector<std::pair<nng_socket, nng_socket>> readoutSnoopSockets; // readout writes, parser reads
    std::vector<std::pair<nng_socket, nng_socket>> parsedDataSockets; // parser writes, eventbuilder reads
    std::vector<std::pair<nng_socket, nng_socket>> stage1DataSockets; // eventbuilder writes, yduplicator reads
    std::vector<std::pair<nng_socket, nng_socket>> stage1AnalysisSockets; // yduplicator writes, stage1 analysis reads

    // stage1 data from all crates is written to the first socket by a
    // yduplicator instance. A stage2 eventbuilder reads from the second socket.
    std::pair<nng_socket, nng_socket> stage1SharedDataSockets;

    // stage2 eventbuilder writes, stage2 analysis reads
    std::pair<nng_socket, nng_socket> stage2DataSockets;

    // Readout data is published on consecutive ports starting from this one.
    u16 readoutDataSnoopPort = 42666;

    // Per crate readout snoop. This is the start of the per crate processing
    // chain. This first stage will drop data if the chain is too slow to keep
    // up.
    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://readoutDataSnoop{}", i);

        nng_socket pubSocket = nng::make_pub_socket();
        nng_socket subSocket = nng::make_sub_socket();

        // This subscription does receive empty messages.
        if (int res = nng_socket_set(subSocket, NNG_OPT_SUB_SUBSCRIBE, nullptr, 0))
        {
            nng::mesy_nng_error("readout consumer socket subscribe", res);
            return 1;
        }

        if (int res = nng::marry_listen_dial(pubSocket, subSocket, uri.c_str()))
        {
            nng::mesy_nng_error("marry_listen_dial readoutDataSnoop", res);
            return 1;
        }

        auto snoopTcpUri = fmt::format("tcp://*:{}", readoutDataSnoopPort++);

        if (int res = nng_listen(pubSocket, snoopTcpUri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_listen {}", snoopTcpUri), res);
            return 1;
        }

        readoutSnoopSockets.emplace_back(std::make_pair(pubSocket, subSocket));
    }

    // parsers -> eventbuilders stage 1
    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://parsedData{}", i);

        nng_socket outputSocket = nng::make_pair_socket();
        nng_socket inputSocket = nng::make_pair_socket();

        if (int res = nng::marry_listen_dial(outputSocket, inputSocket, uri.c_str()))
        {
            nng::mesy_nng_error("marry_listen_dial parsedData", res);
            return 1;
        }

        parsedDataSockets.emplace_back(std::make_pair(outputSocket, inputSocket));
    }

    // eb stage 1 -> y duplicator
    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://eventBuilderData{}", i);

        nng_socket outputSocket = nng::make_pair_socket();
        nng_socket inputSocket = nng::make_pair_socket();

        if (int res = nng::marry_listen_dial(outputSocket, inputSocket, uri.c_str()))
        {
            nng::mesy_nng_error(fmt::format("marry_listen_dial {}", uri), res);
            return 1;
        }

        stage1DataSockets.emplace_back(std::make_pair(outputSocket, inputSocket));
    }

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://stage1Data{}", i);

        nng_socket outputSocket = nng::make_pair_socket();
        nng_socket inputSocket = nng::make_pair_socket();

        if (int res = nng::marry_listen_dial(outputSocket, inputSocket, uri.c_str()))
        {
            nng::mesy_nng_error(fmt::format("marry_listen_dial {}", uri), res);
            return 1;
        }

        stage1AnalysisSockets.emplace_back(std::make_pair(outputSocket, inputSocket));
    }

    stage1SharedDataSockets = std::make_pair(nng::make_pair_socket(), nng::make_pair_socket());

    {
        auto uri = fmt::format("inproc://stage1CommonData");

        if (int res = nng::marry_listen_dial(stage1SharedDataSockets.first, stage1SharedDataSockets.second, uri.c_str()))
        {
            nng::mesy_nng_error("marry_listen_dial stage1CommonData", res);
            return 1;
        }
    }

    stage2DataSockets = std::make_pair(nng::make_pair_socket(), nng::make_pair_socket());

    {
        auto uri = fmt::format("inproc://stage2Data");

        if (int res = nng::marry_listen_dial(stage2DataSockets.first, stage2DataSockets.second, uri.c_str()))
        {
            nng::mesy_nng_error("marry_listen_dial stage2Data", res);
            return 1;
        }
    }

    std::vector<std::unique_ptr<MvlcEthReadoutLoopContext>> readoutContexts;

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto ctx = std::make_unique<MvlcEthReadoutLoopContext>();
        ctx->quit = false;
        ctx->mvlcDataSocket = mvlcDataSockets[i];
        ctx->dataOutputSocket = readoutProducerSocket;
        ctx->snoopOutputSocket = readoutSnoopSockets[i].first;
        ctx->crateId = i;
        readoutContexts.emplace_back(std::move(ctx));
    }

    ListfileWriterContext listfileWriterContext{};
    listfileWriterContext.quit = false;
    listfileWriterContext.dataInputSocket = listfileConsumerSocket;
    listfileWriterContext.lfh = std::move(lfh);

    std::vector<std::unique_ptr<ReadoutParserNngContext>> parserContexts;

    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        assert(i == crateConfigs[i].crateId);

        auto parserContext = make_readout_parser_nng_context(crateConfigs[i]);
        parserContext->quit = false;
        parserContext->inputSocket = readoutSnoopSockets[i].second;
        parserContext->outputSocket = parsedDataSockets[i].first;

        parserContexts.emplace_back(std::move(parserContext));

        std::string stacksYaml;
        for (const auto &stack: crateConfigs[i].stacks)
            stacksYaml += to_yaml(stack);

        std::string sanitizedStacksYaml;

        for (const auto &stack: mvme_mvlc::sanitize_readout_stacks(crateConfigs[i].stacks))
            sanitizedStacksYaml += to_yaml(stack);

        auto crateId = crateConfigs[i].crateId;

        spdlog::info("crateId={}: readout stacks:\n{}", crateId, stacksYaml);
        spdlog::info("crateId={}: sanitized readout stacks:\n{}", crateId, sanitizedStacksYaml);
    }

    std::vector<std::unique_ptr<EventBuilderContext>> eventBuilderStage1Contexts;

    // First stage event builder setup: event building for a single crate.
    // Assumption: only event0 of each crate needs timestamp matching, other
    // events can be directly forwarded.
    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        auto crateConfig = crateConfigs.at(i);
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
        ebContext->crateId = i;
        ebContext->quit = false;
        ebContext->eventBuilderConfig = ebConfig;
        ebContext->eventBuilder = std::make_unique<EventBuilder>(ebConfig, ebContext.get());
        ebContext->inputSocket = parsedDataSockets[i].second;
        ebContext->outputSocket = stage1DataSockets[i].first;
        // Rewrite data to make the eventbuilder work for a single crate with a non-zero crateid.
        ebContext->inputCrateMappings[i] = 0;
        ebContext->outputCrateMappings[0] = i;
        eventBuilderStage1Contexts.emplace_back(std::move(ebContext));
    }

    std::vector<std::unique_ptr<nng::YDuplicatorContext>> stage1DuplicatorContexts;

    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        auto ydContext = std::make_unique<nng::YDuplicatorContext>();
        ydContext->quit = false;
        ydContext->inputSocket = stage1DataSockets[i].second;
        ydContext->outputSockets = { stage1AnalysisSockets[i].first, stage1SharedDataSockets.first };
        ydContext->isShutdownMessage = is_shutdown_message;
        stage1DuplicatorContexts.emplace_back(std::move(ydContext));
    }

    std::vector<std::unique_ptr<AnalysisProcessingContext>> analysisStage1Contexts;

    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        auto analysisContext = std::make_unique<AnalysisProcessingContext>();
        analysisContext->quit = false;
        analysisContext->inputSocket = stage1AnalysisSockets[i].second;
        analysisContext->crateId = i;

        if (i < analysisConfigs.size())
            analysisContext->analysis = analysisConfigs[i];

        if (i < asps.size())
            analysisContext->asp = asps[i].get();

        analysisStage1Contexts.emplace_back(std::move(analysisContext));
    }


    auto stage2TestConsumerContext = std::make_unique<ParsedDataConsumerContext>();
    stage2TestConsumerContext->quit = false;
    stage2TestConsumerContext->inputSocket = stage1SharedDataSockets.second;

    auto stage2EventBuilderContext = std::make_unique<EventBuilderContext>();

    // Make event0 a cross crate event. Assume each module in event0 from all
    // crates is a mesytec module and uses a large match window.
    {
        EventSetup eventSetup;
        eventSetup.enabled = true;
        eventSetup.mainModule = std::make_pair(0, 0);

        for (size_t i=0; i<vmeConfigs.size(); ++i)
        {
            auto crateConfig = crateConfigs.at(i);
            auto stacks = mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks);
            auto readoutStructure = readout_parser::build_readout_structure(stacks);
            const size_t moduleCount = readoutStructure.at(0).size(); // modules in event 0

            EventSetup::CrateSetup crateSetup;

            for (size_t mi=0; mi<moduleCount; ++mi)
            {
                crateSetup.moduleTimestampExtractors.emplace_back(make_mesytec_default_timestamp_extractor());
                crateSetup.moduleMatchWindows.emplace_back(std::make_pair(-100000, 100000));
            }

            eventSetup.crateSetups.emplace_back(crateSetup);
        }

        EventBuilderConfig ebConfig;
        ebConfig.setups.emplace_back(eventSetup);

        auto &ebContext = stage2EventBuilderContext;
        ebContext->crateId = 0xffu; // XXX: crateId
        ebContext->quit = false;
        ebContext->eventBuilderConfig = ebConfig;
        ebContext->eventBuilder = std::make_unique<EventBuilder>(ebConfig, ebContext.get());
        ebContext->inputSocket = stage1SharedDataSockets.second;
        ebContext->outputSocket = stage2DataSockets.first;
    }

    auto stage2AnalysisContext = std::make_unique<AnalysisProcessingContext>();
    stage2AnalysisContext->quit = false;
    stage2AnalysisContext->inputSocket = stage2DataSockets.second;
    // Use this special value to indicate that this is processing data from
    // multiple crates.
    stage2AnalysisContext->crateId = 0xffu; // XXX: crateId
    auto stage2AnalysisAsp = std::make_unique<multi_crate::MinimalAnalysisServiceProvider>();
    stage2AnalysisAsp->widgetRegistry_ = widgetRegistry;
    auto mergedVmeConfig = make_merged_vme_config_keep_ids(vmeConfigs, std::set<int>{0});
    stage2AnalysisAsp->vmeConfig_ = mergedVmeConfig.get();
    stage2AnalysisContext->asp = stage2AnalysisAsp.get();

    // Prepare the single crate analysis instances.
    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        if (i < analysisConfigs.size())
        {
            RunInfo runInfo{};
            auto &ana = analysisConfigs[i];
            //ana->beginRun(runInfo, vmeConfigs[i].get());
        }
    }


    fmt::print(">>>>>>>>>>>>>>>>>>>>>> stage2 analysis build start <<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");

    // Prepare the merged stage2 analysis instance.
    if (analysisConfigs.size() >= vmeConfigs.size())
    {
        auto &ana = analysisConfigs.back();
        ana->beginRun(RunInfo{}, mergedVmeConfig.get());
        stage2AnalysisContext->analysis = ana;
        stage2AnalysisAsp->analysis_ = ana;
    }

    fmt::print(">>>>>>>>>>>>>>>>>>>>>> stage2 analysis build done <<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");

    // Thread creation starts here.
    std::thread listfileWriterThread(listfile_writer_loop, std::ref(listfileWriterContext));
    std::vector<std::thread> readoutThreads;
    std::vector<std::thread> parserThreads;
    std::vector<std::thread> eventBuilderStage1RecorderThreads;
    std::vector<std::thread> eventBuilderStage1BuilderThreads;
    std::vector<std::thread> analysisStage1Threads;
    std::vector<std::thread> stage1DuplicatorThreads;

    for (auto &readoutContext: readoutContexts)
    {
        std::thread readoutThread(mvlc_eth_readout_loop, std::ref(*readoutContext));
        readoutThreads.emplace_back(std::move(readoutThread));
    }

    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        std::thread parserThread(readout_parser_loop, std::ref(*parserContexts[i]));
        parserThreads.emplace_back(std::move(parserThread));
    }

    #if 0
    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        std::thread ebStage1Thread(event_builder_record_loop, std::ref(*eventBuilderStage1Contexts[i]));
        eventBuilderStage1RecorderThreads.emplace_back(std::move(ebStage1Thread));
    }

    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        std::thread t(event_builder_build_loop, std::ref(*eventBuilderStage1Contexts[i]));
        eventBuilderStage1BuilderThreads.emplace_back(std::move(t));
    }
    #else
    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        std::thread t(event_builder_combined_loop, std::ref(*eventBuilderStage1Contexts[i]));
        eventBuilderStage1RecorderThreads.emplace_back(std::move(t));
    }
    #endif

    // y duplicator piece consuming stage1 data. output goes to the single crate
    // analysis instances and to the stage2 event builder.
    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        std::thread t(nng::duplicator_loop, std::ref(*stage1DuplicatorContexts[i]));
        stage1DuplicatorThreads.emplace_back(std::move(t));
    }

    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        std::thread analysisThread(analysis_loop, std::ref(*analysisStage1Contexts[i]));
        analysisStage1Threads.emplace_back(std::move(analysisThread));
    }

    std::thread stage2EventBuilderThread(event_builder_combined_loop, std::ref(*stage2EventBuilderContext));

    //std::thread stage2CommonConsumerThread(parsed_data_test_consumer_loop, std::ref(*stage2TestConsumerContext));
    std::thread stage2AnalysisThread(analysis_loop, std::ref(*stage2AnalysisContext));

    // GUI stuff starts here

    // TODO: add stats and/or prometheus metrics and udpate them here.
#ifdef MVME_ENABLE_PROMETHEUS
    //QObject::connect(&periodicTimer, &QTimer::timeout, [&] { metrics->update(); });
#endif
    for (size_t i=0; i<asps.size(); ++i)
    {
        auto asp = asps[i].get();
        auto widget = new analysis::ui::AnalysisWidget(asp);
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->show();
    }

    auto stage2AnalysisWidget = new analysis::ui::AnalysisWidget(stage2AnalysisAsp.get());
    stage2AnalysisWidget->setAttribute(Qt::WA_DeleteOnClose, true);
    stage2AnalysisWidget->show();

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        auto widget = new VMEConfigTreeWidget;
        widget->setWindowTitle(fmt::format("crate{}", i).c_str());
        widget->resize(600, 800);
        widget->setConfig(vmeConfigs[i].get());
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->show();
    }

    {
        auto widget = new VMEConfigTreeWidget;
        widget->setWindowTitle("merged");
        widget->resize(600, 800);
        widget->setConfig(mergedVmeConfig.get());
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->show();
    }

    QTimer periodicTimer;
    periodicTimer.setInterval(1000);
    periodicTimer.start();

    QObject::connect(&periodicTimer, &QTimer::timeout, [&]
    {
        if (signal_received)
            app.quit();

        for (auto &ctx: readoutContexts)
        {
            log_socket_work_counters(ctx->dataOutputCounters.access().ref(),
                fmt::format("eth_readout (crate={}, data output)", ctx->crateId));
            log_socket_work_counters(ctx->snoopOutputCounters.access().ref(),
                fmt::format("eth_readout (crate={}, snoop output)", ctx->crateId));
        }

        {
            auto listfileWriterCounters = listfileWriterContext.dataInputCounters.copy();
            for (size_t i=0; i<listfileWriterCounters.size(); ++i)
            {
                auto &counters = listfileWriterCounters[i];
                if (counters.messagesReceived || counters.messagesLost)
                    log_socket_work_counters(counters, fmt::format("listfile_writer (crateId={})", i));
            }
        }

        for (auto &ctx: parserContexts)
        {
            log_socket_work_counters(ctx->counters.access().ref(),
                fmt::format("readout_parser_loop (crateId={})", ctx->crateId));
        }

        for (auto &ctx: eventBuilderStage1Contexts)
        {
            log_socket_work_counters(ctx->counters.access().ref(),
                fmt::format("event_builder_stage1 (crateId={})", ctx->crateId));
        }

        for (auto &ctx: analysisStage1Contexts)
        {
            log_socket_work_counters(ctx->inputSocketCounters.access().ref(),
                fmt::format("analysis_loop (crateId={})", ctx->crateId));
        }

        log_socket_work_counters(stage2TestConsumerContext->counters.access().ref(),
            "parsed_data_consumer (stage2)");

        log_socket_work_counters(stage2EventBuilderContext->counters.access().ref(),
            "stage2_event_builder");

        log_socket_work_counters(stage2AnalysisContext->inputSocketCounters.access().ref(),
            "stage2_analysis_consumer");

        {
            auto readoutEventCounts = stage2TestConsumerContext->readoutEventCounts.copy();
            auto systemEventCounts  = stage2TestConsumerContext->systemEventCounts.copy();

            for (size_t i=0; i<readoutEventCounts.size(); ++i)
            {
                if (readoutEventCounts.at(i))
                    spdlog::info("stage2 consumer: readout events from crate{}: {}", i, readoutEventCounts.at(i));
            }

            for (size_t i=0; i<systemEventCounts.size(); ++i)
            {
                if (systemEventCounts.at(i))
                    spdlog::info("stage2 consumer: system events from crate{}: {}", i, systemEventCounts.at(i));
            }
        }
    });

    for (size_t i=0; i<eventBuilderStage1Contexts.size(); ++i)
    {
        auto widget = mvme::util::make_monospace_plain_textedit().release();
        widget->setWindowTitle(fmt::format("EventBuilder Stage1 (crateId={})", i).c_str());
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->resize(800, 200);
        widget->show();

        QObject::connect(&periodicTimer, &QTimer::timeout, widget, [widget, i, &eventBuilderStage1Contexts]
        {
            auto &context = eventBuilderStage1Contexts[i];
            auto counters = context->eventBuilder->getCounters();
            auto str = to_string(counters);
            widget->setPlainText(QString::fromStdString(str));
        });
    }

    {
        auto widget = mvme::util::make_monospace_plain_textedit().release();
        widget->setWindowTitle(fmt::format("EventBuilder Stage2").c_str());
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->resize(800, 200);
        widget->show();

        QObject::connect(&periodicTimer, &QTimer::timeout, widget, [widget, &stage2EventBuilderContext]
        {
            auto &context = stage2EventBuilderContext;
            auto counters = context->eventBuilder->getCounters();
            auto str = to_string(counters);
            widget->setPlainText(QString::fromStdString(str));
        });
    }

    int ret = app.exec();

    spdlog::info("trying to stop all the things!");


    // After waiting for a certain time for things to stop, set the quit flag
    // and start joining threads. Each processing loop should react to the quit
    // flag and immediately shut down.

    // Stop the start of the processing chains.
    for (auto &readoutContext: readoutContexts)
        readoutContext->quit = true;

    for (auto &t: readoutThreads)
        if (t.joinable())
            t.join();

    spdlog::debug("readout threads stopped");

    // Now send shutdown messages through the data output and snoop sockets.
    for (auto &readoutContext: readoutContexts)
    {
        spdlog::warn("crate{} readout: sending shutdown messages", readoutContext->crateId);
        send_shutdown_messages({ readoutContext->dataOutputSocket, readoutContext->snoopOutputSocket });
    }

    spdlog::debug("shutdown sent through data and snoop output sockets for all crates");

    // TODO: replace the threads with std::async as the returned futures can be waited on, unlike thread.join()

    if (listfileWriterThread.joinable())
        listfileWriterThread.join();

    spdlog::debug("listfile writer stopped");

    for (auto &t: parserThreads)
        if (t.joinable())
            t.join();

    spdlog::debug("parsers stopped");

    for (auto &parserContext: parserContexts)
        send_shutdown_message(parserContext->outputSocket);

    for (auto &t: eventBuilderStage1RecorderThreads)
        if (t.joinable())
            t.join();

    // The builder threads do not receive shutdown messages as they do not read
    // from a socket => manually tell them to stop.
    for (auto &ebContext: eventBuilderStage1Contexts)
        ebContext->quit = true;

    for (auto &t: eventBuilderStage1BuilderThreads)
        if (t.joinable())
            t.join();

    spdlog::debug("event builders stopped");

    for (auto &ebContext: eventBuilderStage1Contexts)
        send_shutdown_message(ebContext->outputSocket);

    for (auto &t: stage1DuplicatorThreads)
        if (t.joinable())
            t.join();

    spdlog::debug("stage 1 duplicators stopped");

    for (auto &ctx: stage1DuplicatorContexts)
        send_shutdown_messages({ ctx->outputSockets.first, ctx->outputSockets.second });

    for (auto &t: analysisStage1Threads)
        if (t.joinable())
            t.join();

    spdlog::debug("analysis stage 1 threads stopped");

    #if 0
    if (stage2CommonConsumerThread.joinable())
        stage2CommonConsumerThread.join();
    #else
    if (stage2EventBuilderThread.joinable())
        stage2EventBuilderThread.join();

    spdlog::debug("stage2 event builder stopped");

    send_shutdown_message(stage2EventBuilderContext->outputSocket);

    if (stage2AnalysisThread.joinable())
        stage2AnalysisThread.join();
    #endif

    spdlog::debug("stage2 analysis stopped");


    // TODO: force shutdowns after giving threads time to stop

    //listfileWriterContext.quit = true;
    //for (auto &parserContext: parserContexts)
    //    parserContext->quit = true;
    //for (auto &analysisContext: analysisStage1Contexts)
    //    analysisContext->quit = true;

    //for (auto &t: readoutThreads)
    //    if (t.joinable())
    //        t.join();

    mvme_shutdown();
    spdlog::debug("returning from main()");
    return ret;
}
