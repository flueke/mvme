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
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"
#include "mvme_session.h"
#include "util/mesy_nng.h"
#include "util/mesy_nng_pipeline.h"
#include "util/stopwatch.h"
#include "vme_config.h"

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
// Calls recordEventData and recordSystemEvent with data read from inputSocket.
// on the event builder. Event builder output is written to the outputSocket and
// duplicated on the snoop output.
void event_builder_loop(EventBuilderContext &context)
{
    set_thread_name("event_builder_loop");

    spdlog::info("entering event_builder_loop");

    while (!context.quit)
    {
        nng_msg *inputMsg = nullptr;

        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::warn("event_builder_loop - receive message: {}", nng_strerror(res));
            }
            spdlog::trace("event_builder_loop - receive_message: timeout");
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg);

        if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
        {
            auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(inputMsg));
            if (header.messageType == multi_crate::MessageType::GracefulShutdown)
            {
                spdlog::warn("event_builder_loop: Received shutdown message, leaving loop");
                nng_msg_free(inputMsg);
                break;
            }
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("event_builder_loop - incoming message too short (len={})", msgLen);
            nng_msg_free(inputMsg);
            continue;
        }



    }

    spdlog::info("Leaving event_builder_loop");
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

int send_shutdown_message(nng_socket socket)
{
    multi_crate::BaseMessageHeader header{};
    header.messageType = multi_crate::MessageType::GracefulShutdown;
    auto msg = nng::alloc_message(sizeof(header));
    std::memcpy(nng_msg_body(msg), &header, sizeof(header));
    if (int res = nng::send_message_retry(socket, msg))
    {
        nng_msg_free(msg);
        return res;
    }

    return 0;
}

void send_shutdown_messages(std::initializer_list<nng_socket> sockets)
{
    for (auto socket: sockets)
    {
        send_shutdown_message(socket);
    }
}

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

    std::vector<std::shared_ptr<analysis::Analysis>> analysisConfigs;

    {
        auto logger = [] (const QString &msg)
        {
            spdlog::error("analysis: {}", msg.toStdString());
        };

        size_t crateIndex = 0;

        for (auto &param: parser.params("--analysis"))
        {
            auto filename = QString::fromStdString(param.second);
            auto [ana, errorString] = analysis::read_analysis_config_from_file(filename);

            if (!ana)
            {
                std::cerr << fmt::format("Error reading mvme analysis config from '{}': {}\n",
                    filename.toStdString(), errorString.toStdString());
                return 1;
            }

            if (crateIndex < vmeConfigs.size())
            {
                RunInfo runInfo{};
                ana->beginRun(runInfo, vmeConfigs[crateIndex].get());
            }

            analysisConfigs.emplace_back(std::move(ana));
            ++crateIndex;
        }
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

    // All readout threads write messages of type
    // multi_crate::MessageType::ReadoutData to the readoutProducerSocket.
    nng_socket readoutProducerSocket = nng::make_push_socket();

    // A single(!) thread reads ReadoutData messages from this socket and
    // writes a listfile. Do not connect multiple pull sockets to the
    // readoutProducerSocket as that will round-robin distribute the messages!
    nng_socket listfileConsumerSocket = nng::make_pull_socket();

    // TODO: check for socket validity via nng_socket_id() and/or add another
    // way of communicating errors from the socket creation functions.
    if (int res = nng_listen(listfileConsumerSocket, "inproc://readoutData", nullptr, 0))
    {
        nng::mesy_nng_error("nng_listen readoutData", res);
        return 1;
    }

    if (int res = nng_dial(readoutProducerSocket, "inproc://readoutData", nullptr, 0))
    {
        nng::mesy_nng_error("nng_dial readoutData", res);
        return 1;
    }

    // Readout data producers also publish their data on this socket. One snoop
    // output socket per crate.
    std::vector<nng_socket> readoutProducerSnoopSockets;
    // Connected to the respective producer snoop socket. Readout parsers read from one of these.
    std::vector<nng_socket> readoutConsumerInputSockets;

    std::vector<nng_socket> readoutParserOutputSockets;
    std::vector<nng_socket> parsedDataConsumerSockets;

    u16 readoutDataSnoopPort = 42666;

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://readoutDataSnoop{}", i);

        nng_socket pubSocket = nng::make_pub_socket();

        if (int res = nng_listen(pubSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_listen {}", uri), res);
            return 1;
        }

        auto snoopTcpUri = fmt::format("tcp://*:{}", readoutDataSnoopPort++);

        if (int res = nng_listen(pubSocket, snoopTcpUri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_listen {}", snoopTcpUri), res);
            return 1;
        }

        readoutProducerSnoopSockets.emplace_back(pubSocket);

        nng_socket subSocket = nng::make_sub_socket();

        // This subscription does receive empty messages.
        if (int res = nng_socket_set(subSocket, NNG_OPT_SUB_SUBSCRIBE, nullptr, 0))
        {
            nng::mesy_nng_error("readout consumer socket subscribe", res);
            return 1;
        }

        if (int res = nng_dial(subSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_dial {}", uri), res);
            return 1;
        }

        readoutConsumerInputSockets.emplace_back(subSocket);
    }

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://parsedData{}", i);

        nng_socket parserOutputSocket = nng::make_pair_socket();

        if (int res = nng_listen(parserOutputSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_listen {}", uri), res);
            return 1;
        }

        readoutParserOutputSockets.emplace_back(parserOutputSocket);

        nng_socket analysisInputSocket = nng::make_pair_socket();

        if (int res = nng_dial(analysisInputSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_dial {}", uri), res);
            return 1;
        }

        parsedDataConsumerSockets.emplace_back(analysisInputSocket);
    }

    std::vector<std::unique_ptr<MvlcEthReadoutLoopContext>> readoutContexts;

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto ctx = std::make_unique<MvlcEthReadoutLoopContext>();
        ctx->quit = false;
        ctx->mvlcDataSocket = mvlcDataSockets[i];
        ctx->dataOutputSocket = readoutProducerSocket;
        ctx->snoopOutputSocket = readoutProducerSnoopSockets[i];
        ctx->crateId = i;
        readoutContexts.emplace_back(std::move(ctx));
    }

    ListfileWriterContext listfileWriterContext{};
    listfileWriterContext.quit = false;
    listfileWriterContext.dataInputSocket = listfileConsumerSocket;

    // Open the output listfile if one should be written.
    std::unique_ptr<listfile::ZipCreator> listfileCreator;

    std::string str;
    if (parser("--listfile") >> str)
    {
        try
        {
            listfileCreator = std::make_unique<listfile::ZipCreator>();
            listfileCreator->createArchive(str);
            listfileWriterContext.lfh = listfileCreator->createZIPEntry("listfile.mvlclst");
            spdlog::info("Opened output listfile {}", str);
            auto &lfh = listfileWriterContext.lfh;
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
            spdlog::error("Error opening output listfile {}: {}", str, e.what());
            return 1;
        }
    }

    std::vector<std::unique_ptr<ReadoutParserNngContext>> parserContexts;

    for (size_t i=0; i<crateConfigs.size(); ++i)
    {
        assert(i == crateConfigs[i].crateId);

        auto parserContext = make_readout_parser_nng_context(crateConfigs[i]);
        parserContext->quit = false;
        parserContext->inputSocket = readoutConsumerInputSockets[i];
        parserContext->outputSocket = readoutParserOutputSockets[i];

        parserContexts.emplace_back(std::move(parserContext));

        std::string stacksYaml;
        for (const auto &stack: crateConfigs[i].stacks)
            stacksYaml += to_yaml(stack);

        spdlog::info("crateId={}: readout stacks:\n{}", crateConfigs[i].crateId, stacksYaml);
    }

    std::vector<std::unique_ptr<AnalysisProcessingContext>> analysisContexts;

    for (size_t i=0; i<parsedDataConsumerSockets.size(); ++i)
    {
        auto analysisContext = std::make_unique<AnalysisProcessingContext>();
        analysisContext->quit = false;
        analysisContext->inputSocket = parsedDataConsumerSockets[i];
        analysisContext->crateId = i;

        if (i < analysisConfigs.size())
            analysisContext->analysis = analysisConfigs[i];

        if (i < asps.size())
            analysisContext->asp = asps[i].get();

        analysisContexts.emplace_back(std::move(analysisContext));
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

    // Thread creation starts here.
    std::thread listfileWriterThread(listfile_writer_loop, std::ref(listfileWriterContext));
    std::vector<std::thread> readoutThreads;
    std::vector<std::thread> parserThreads;
    std::vector<std::thread> analysisThreads;

    for (auto &readoutContext: readoutContexts)
    {
        std::thread readoutThread(mvlc_eth_readout_loop, std::ref(*readoutContext));
        readoutThreads.emplace_back(std::move(readoutThread));
    }

    assert(readoutConsumerInputSockets.size() == vmeConfigs.size());

    for (size_t i=0; i<readoutConsumerInputSockets.size(); ++i)
    {
        std::thread parserThread(readout_parser_loop, std::ref(*parserContexts[i]));
        parserThreads.emplace_back(std::move(parserThread));
    }

    for (size_t i=0; i<analysisContexts.size(); ++i)
    {
        std::thread analysisThread(analysis_loop, std::ref(*analysisContexts[i]));
        analysisThreads.emplace_back(std::move(analysisThread));
    }

#if 1 // GUI
    QTimer periodicTimer;

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

        for (auto &ctx: parserContexts)
        {
            log_socket_work_counters(ctx->counters.access().ref(),
                fmt::format("readout_parser_loop (crateId={})", ctx->crateId));
        }

        for (auto &ctx: analysisContexts)
        {
            log_socket_work_counters(ctx->inputSocketCounters.access().ref(),
                fmt::format("analysis_loop (crateId={})", ctx->crateId));
        }
    });

    // TODO: add stats and/or prometheus metrics and udpate them here.
#ifdef MVME_ENABLE_PROMETHEUS
    //QObject::connect(&periodicTimer, &QTimer::timeout, [&] { metrics->update(); });
#endif
    periodicTimer.setInterval(1000);
    periodicTimer.start();

    for (size_t i=0; i<asps.size(); ++i)
    {
        auto asp = asps[i].get();
        auto widget = new analysis::ui::AnalysisWidget(asp);
        widget->setAttribute(Qt::WA_DeleteOnClose, true);
        widget->show();
    }

    int ret = app.exec();

#else // non-GUI
    // Loop until we get interrupted. TODO: tell loops to quit somehow, e.g. by
    // injecting an empty message into the processing chains. This way ensures
    // that each loop processes all its input packets before quitting.
    spdlog::info("entering wait_for_signal loop!");

    do
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (!signal_received);

#endif

    spdlog::info("trying to stop all the things!");


    // TODO: use empty messages to tell downstream consumers to also quit.
    // Inject empty messages into readoutProducerSocket. This way the listfile
    // writer will consume all pending messages before terminating itself.

    // After waiting for a certain time for things to stop, set the quit flag
    // and start joining threads. Each processing loop should react to the quit
    // flag and immediately shut down.

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
        #if 0
        multi_crate::BaseMessageHeader header{};
        header.messageType = multi_crate::MessageType::GracefulShutdown;
        // XXX: messageNumber is garbage here. Do not know the last message
        // number output by the readout loop. So basically it's not used for
        // GracefulShutdown
        for (auto socket: { readoutContext->dataOutputSocket, readoutContext->snoopOutputSocket })
        {
            auto msg = nng::alloc_message(sizeof(header));
            std::memcpy(nng_msg_body(msg), &header, sizeof(header));
            if (int res = nng::send_message_retry(socket, msg))
            {
                spdlog::warn("crate{}: error sending shutdown to readout output socket: {}",
                    readoutContext->crateId, nng_strerror(res));
            }
        }
        #else
        send_shutdown_messages({ readoutContext->dataOutputSocket, readoutContext->snoopOutputSocket });
        #endif
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
    {
        send_shutdown_message(parserContext->outputSocket);
    }

    for (auto &t: analysisThreads)
        if (t.joinable())
            t.join();

    spdlog::debug("analysis threads stopped");

    // TODO: force shutdowns after giving threads time to stop

    //listfileWriterContext.quit = true;
    //for (auto &parserContext: parserContexts)
    //    parserContext->quit = true;
    //for (auto &analysisContext: analysisContexts)
    //    analysisContext->quit = true;

    //for (auto &t: readoutThreads)
    //    if (t.joinable())
    //        t.join();








    mvme_shutdown();
    spdlog::debug("returning from main()");
    return ret;
}
