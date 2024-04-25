#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/util/udp_sockets.h>
#include <QApplication>
#include <signal.h>

#ifndef __WIN32
#include <poll.h>
#else
#include <winsock2.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "analysis/analysis.h"
#include "mvme_session.h"
#include "vme_config.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"
#include "util/mesy_nng.h"
#include "util/mesy_nng_pipeline.h"
#include "multi_crate.h"

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvme;

static std::atomic<bool> signal_received = false;

void signal_handler(int signum)
{
    std::cerr << "signal " << signum << "\n";
    std::cerr.flush();
    signal_received = true;
}

void setup_signal_handlers()
{
#ifndef __WIN32
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
#endif
    // TODO: add signal handling for windows
}

#if 0
struct ProcessingContext
{
    std::atomic<bool> quit = false;
    std::vector<std::unique_ptr<VMEConfig>> vmeConfigs;
    std::vector<int> dataSockets;
    std::unique_ptr<listfile::WriteHandle> lfh;
};

void processing_loop(ProcessingContext &context)
{
    std::vector<struct pollfd> pollfds;

    for (auto sock: context.dataSockets)
    {
        struct pollfd pfd = {};
        pfd.fd = sock;
        pfd.events = POLLIN;
        pollfds.emplace_back(pfd);
    }

    auto num_open_fds = pollfds.size();

    std::array<u8, 1500> destBuffer;

    while (!context.quit && num_open_fds > 0 && !signal_received)
    {
#ifndef __WIN32
        if (poll(pollfds.data(), pollfds.size(), 100) < 0)
        {
            perror("poll");
        }
#else
        if (WSAPoll(pollfds.data(), pollfds.size(), 100) == SOCKET_ERROR)
        {
            spdlog::error("Error from WSAPoll: {}", WSAGetLastError());
        }
#endif

        spdlog::trace("poll() returned");

        for (auto &pfd: pollfds)
        {
            if (pfd.revents & POLLIN)
            {
                // TODO: read from the socket
                destBuffer.fill(0);
                size_t bytesTransferred = 0;
                if (auto ec = eth::receive_one_packet(pfd.fd, destBuffer.data(), destBuffer.size(), bytesTransferred, 500))
                {
                    spdlog::error("Error reading from socket: {}\n", ec.message());
                    pfd.fd = -1; // poll() ignored entries with negative fds
                    --num_open_fds;
                }
                else
                {
                    spdlog::info("Received {} bytes from socket {}\n", bytesTransferred, pfd.fd);

                    eth::PacketReadResult prr{};
                    prr.buffer = destBuffer.data();
                    prr.bytesTransferred = bytesTransferred;

                    if (prr.hasHeaders())
                    {
                        spdlog::info("socket {} -> packetNumber={}, crateId={}",
                            pfd.fd, prr.packetNumber(), prr.controllerId());
                    }
                    else
                    {
                        spdlog::info("socket {} -> no valid headers in received packet of size {}",
                            pfd.fd, prr.bytesTransferred);
                    }

                    if (context.lfh)
                    {
                        context.lfh->write(destBuffer.data(), bytesTransferred);
                    }
                }
            }
            else if (pfd.revents & (POLLERR | POLLHUP))
            {
                // TODO: close the socket here?
                pfd.fd = -1; // poll() ignored entries with negative fds
                --num_open_fds;
            }
        }
    }
}
#endif

struct ReadoutLoopContext
{
    std::atomic<bool> quit;

    // This is put into output ListfileBufferMessageHeader messages and passed
    // to ReadoutLoopPlugins.
    u8 crateId;

    // The MVLC data stream is read from this socket.
    int mvlcDataSocket;

    // Readout data is written to this socket. Expected to use a lossless,
    // blocking protocol, e.g. pair or push as this data goes to the output
    // listfile.
    nng_socket dataOutputSocket;

    // Readout data is also written to this socket. Should be a lossfull,
    // non-blocking protocol, e.g. pub.
    nng_socket snoopOutputSocket;
};

static const size_t DefaultOutputMessageReserve = mvlc::util::Megabytes(1) + sizeof(multi_crate::ListfileBufferMessageHeader);
static const std::chrono::milliseconds FlushBufferTimeout(500);

void mvlc_eth_readout_loop(ReadoutLoopContext &context)
{
#ifdef __linux__
    prctl(PR_SET_NAME,"mvlc_eth_readout_loop",0,0,0);
#endif

    multi_crate::NngMsgWriteHandle lfh;
    u32 messageNumber = 1u;
    std::vector<u8> previousData;
    s32 lastPacketNumber = -1;

    auto new_output_message = [&] () -> nng_msg *
    {
        nng_msg *msg = {};

        if (auto res = nng::allocate_reserve_message(&msg, DefaultOutputMessageReserve))
        {
            spdlog::error("mvlc_eth_readout_loop: could not allocate nng output message: {}", nng_strerror(res));
            return nullptr;
        }

        lfh.setMessage(msg);

        multi_crate::ListfileBufferMessageHeader header
        {
            multi_crate::MessageType::ListfileBuffer,
            messageNumber++,
            context.crateId,
            static_cast<u32>(mvlc::ConnectionType::ETH)
        };

        nng_msg_append(msg, &header, sizeof(header));
        assert(nng_msg_len(msg) == sizeof(header));

        nng_msg_append(msg, previousData.data(), previousData.size());
        previousData.clear();

        return msg;
    };

    auto flush_output_message = [&] (nng_msg *msg) -> int
    {
        multi_crate::fixup_listfile_buffer_message(mvlc::ConnectionType::ETH, msg, previousData);
        nng_msg *msgClone = nullptr;

        if (auto res = nng_msg_dup(&msgClone, msg))
        {
            spdlog::error("mvlc_eth_readout_loop: could not allocate nng output message");
            return res;
        }

        if (auto res = nng::send_message_retry(context.dataOutputSocket, msg))
            return res;

        if (auto res = nng::send_message_retry(context.snoopOutputSocket, msgClone))
            return res;

        return 0;
    };

    ReadoutLoopPlugin::Arguments pluginArgs{};
    pluginArgs.crateId = context.crateId;
    pluginArgs.listfileHandle = &lfh;

    std::vector<std::unique_ptr<ReadoutLoopPlugin>> readoutLoopPlugins;
    readoutLoopPlugins.emplace_back(std::make_unique<TimetickPlugin>());

    auto msg = new_output_message();

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStart(pluginArgs);

    auto tLastFlush = std::chrono::steady_clock::now();

    while (!context.quit)
    {
        assert(nng::allocated_free_space(msg) >= eth::JumboFrameMaxSize);

        auto msgUsed = nng_msg_len(msg);

        // Note: should not alloc as we reserved space when the message was
        // created. This just increases the size of the message.
        nng_msg_realloc(msg, msgUsed + eth::JumboFrameMaxSize);

        size_t bytesTransferred = 0;
        auto ec = eth::receive_one_packet(
            context.mvlcDataSocket,
            reinterpret_cast<u8 *>(nng_msg_body(msg)) + msgUsed,
            eth::JumboFrameMaxSize, bytesTransferred, 100);

        if (ec && ec != std::errc::resource_unavailable_try_again)
        {
            spdlog::error("Error reading from mvlc data socket: {}", ec.message());
            break;
        }

        assert(bytesTransferred <= eth::JumboFrameMaxSize);

        if (bytesTransferred > 0)
        {
            eth::PacketReadResult prr{};
            prr.buffer = reinterpret_cast<u8 *>(nng_msg_body(msg)) + msgUsed;
            prr.bytesTransferred = bytesTransferred;

            if (prr.hasHeaders())
            {
                spdlog::debug("mvlc_eth_readout_loop (crate{}): incoming packet: packetNumber={}, crateId={}, size={} bytes",
                    context.crateId, prr.packetNumber(), prr.controllerId(), prr.bytesTransferred);
            }

            if (!prr.hasHeaders())
            {
                spdlog::warn("crate{}: no valid headers in received packet of size {}. Dropping the packet!",
                    context.crateId, prr.bytesTransferred);
            }
            else if (prr.controllerId() != context.crateId)
            {
                spdlog::warn("crate{}: incoming data packet has crateId={} set, excepted {}. Dropping the packet!",
                    context.crateId, prr.controllerId(), context.crateId);
            }
            else
            {
                if (lastPacketNumber >= 0)
                {
                    if (auto loss = eth::calc_packet_loss(
                        lastPacketNumber, prr.packetNumber()))
                    {
                        spdlog::warn("crate{}: lost {} incoming data packets!",
                            context.crateId, loss);
                    }
                }

                // Update the message size. This should not alloc as we can only shrink
                // the message here.
                nng_msg_realloc(msg, msgUsed + bytesTransferred);

                // Cross check size here.
                assert(nng_msg_len(msg) == msgUsed + bytesTransferred);
            }
        }

        // Run plugins (currently only timetick generation here).
        for (const auto &plugin: readoutLoopPlugins)
        {
            plugin->operator()(pluginArgs);
        }

        // Check if either the flush timeout elapsed or there is no more space
        // for packets in the output message.
        if (auto elapsed = std::chrono::steady_clock::now() - tLastFlush;
            elapsed >= FlushBufferTimeout || nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
        {
            if (nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
                spdlog::trace("crate{}: flushing full output message #{}", context.crateId, messageNumber - 1);
            else
                spdlog::trace("crate{}: flushing output message #{} due to timeout", context.crateId, messageNumber - 1);


            if (flush_output_message(msg) != 0)
                return;

            msg = new_output_message();

            if (!msg)
                return;

            tLastFlush = std::chrono::steady_clock::now();
        }
    }

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStop(pluginArgs);
}

struct ListfileWriterContext
{
    std::atomic<bool> quit;

    // Readout data in the form of ListfileBufferMessageHeader messages is read
    // from this socket.
    nng_socket dataInputSocket;

    // This is where readout data is written to if non-null.
    std::unique_ptr<listfile::WriteHandle> lfh;
};

void listfile_writer_loop(ListfileWriterContext &context)
{
#ifdef __linux__
    prctl(PR_SET_NAME,"listfile_writer_loop",0,0,0);
#endif

    // Last received message number per crate.
    std::array<u32, frame_headers::CtrlIdMask+1> lastMessageNumbers;
    lastMessageNumbers.fill(0);

    while (!context.quit)
    {
        nng_msg *msg = nullptr;

        if (auto res = nng::receive_message(context.dataInputSocket, &msg))
        {
            if (res != NNG_ETIMEDOUT)
                spdlog::warn("listfile_writer_loop: Error reading from data input socket: {}", nng_strerror(res));
            continue;
        }

        assert(msg != nullptr);

        if (nng_msg_len(msg) < sizeof(multi_crate::ListfileBufferMessageHeader))
        {
            spdlog::warn("listfile_writer_loop: Incoming message is too short!");
            // TODO: count this error (should not happen)
            nng_msg_free(msg);
            continue;
        }

        auto header = *reinterpret_cast<multi_crate::ListfileBufferMessageHeader *>(nng_msg_body(msg));

        if (header.crateId > frame_headers::CtrlIdMask)
        {
            spdlog::warn("listfile_writer_loop: Invalid crateId={} in incoming data packet!", header.crateId);
            nng_msg_free(msg);
            continue;
        }

        if (auto loss = readout_parser::calc_buffer_loss(header.messageNumber, lastMessageNumbers[header.crateId]))
            spdlog::warn("listfile_writer_loop: lost {} messages from crate{}!", loss, header.crateId);

        lastMessageNumbers[header.crateId] = header.messageNumber;

        // Trim off the header from the front of the message. The rest of the
        // message is pure readout data and added system event frames.
        nng_msg_trim(msg, sizeof(multi_crate::ListfileBufferMessageHeader));

        auto dataPtr = reinterpret_cast<const u32 *>(nng_msg_body(msg));
        size_t dataSize = nng_msg_len(msg) / sizeof(u32);
        std::basic_string_view<u32> dataView(dataPtr, dataSize);

        if (context.lfh)
        {
            try
            {
                context.lfh->write(reinterpret_cast<const u8 *>(nng_msg_body(msg)), nng_msg_len(msg));
            }
            catch(const std::exception& e)
            {
                spdlog::warn("listfile_writer_loop: Error writing to output listfile: {}", e.what());
            }
        }

        nng_msg_free(msg);
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
    std::shared_ptr<analysis::Analysis> analysisConfig;

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

    std::string str;

    if (parser("--analysis") >> str)
    {
        auto filename = QString::fromStdString(str);
        auto [ana, errorString] = analysis::read_analysis_config_from_file(filename);

        if (!ana)
        {
            std::cerr << fmt::format("Error reading mvme analysis config from '{}': {}\n",
                filename.toStdString(), errorString.toStdString());
            return 1;
        }

        analysisConfig = ana;
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

    // All readout threads write messages of type
    // multi_crate::MessageType::ListfileBuffer to the readoutProducerSocket.
    nng_socket readoutProducerSocket = nng::make_push_socket();

    // Readout threads also publish their ListfileBuffer messages through this
    // socket. During readout this should be a lossfull pup socket, when
    // replaying from file this should instead be a push or pair socket that
    // does not lose messages.
    nng_socket readoutProducerSnoopSocket = nng::make_pub_socket();

    // A single thread reads ListfileBuffer messages from this socket and writes
    // a listfile.
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

    if (int res = nng_listen(readoutProducerSnoopSocket, "inproc://readoutSnoop", nullptr, 0))
    {
        nng::mesy_nng_error("nng_listen readoutSnoop", res);
        return 1;
    }

    if (int res = nng_listen(readoutProducerSnoopSocket, "tcp://*:42666", nullptr, 0))
    {
        nng::mesy_nng_error("nng_listen readoutSnoop", res);
        return 1;
    }

    std::vector<std::unique_ptr<ReadoutLoopContext>> readoutContexts;

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto ctx = std::make_unique<ReadoutLoopContext>();
        ctx->quit = false;
        ctx->mvlcDataSocket = mvlcDataSockets[i];
        ctx->dataOutputSocket = readoutProducerSocket;
        ctx->snoopOutputSocket = readoutProducerSnoopSocket;
        ctx->crateId = i;
        readoutContexts.emplace_back(std::move(ctx));
    }

    ListfileWriterContext listfileWriterContext{};
    listfileWriterContext.quit = false;
    listfileWriterContext.dataInputSocket = listfileConsumerSocket;


    // Open the output listfile if one should be written.
    std::unique_ptr<listfile::ZipCreator> listfileCreator;

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

    std::thread listfileWriterThread(listfile_writer_loop, std::ref(listfileWriterContext));
    std::vector<std::thread> readoutThreads;

    for (auto &readoutContext: readoutContexts)
    {
        std::thread readoutThread(mvlc_eth_readout_loop, std::ref(*readoutContext));
        readoutThreads.emplace_back(std::move(readoutThread));
    }

    do
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (!signal_received);


    // TODO: use empty messages to tell downstream consumers to also quit.
    // Inject empty messages into readoutProducerSocket. This way the listfile
    // writer will consume all pending messages before terminating itself.
    for (auto &readoutContext: readoutContexts)
        readoutContext->quit = true;

    listfileWriterContext.quit = true;

    for (auto &readoutThread: readoutThreads)
        if (readoutThread.joinable())
            readoutThread.join();

    if (listfileWriterThread.joinable())
        listfileWriterThread.join();


    #if 0
    ProcessingContext context{};
    context.quit = false;
    context.vmeConfigs = std::move(vmeConfigs);
    context.dataSockets = mvlcDataSockets;
    context.lfh = std::move(lfh);

    processing_loop(context);

    if (context.lfh)
    {
        spdlog::info("Writing EndRun and EndOfFile sections");
        for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
        {
            listfile::listfile_write_timestamp_section(
                *context.lfh, crateId, system_event::subtype::EndRun);
        }

        for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
        {
            listfile_write_system_event(*context.lfh, crateId, system_event::subtype::EndOfFile);
        }
    }
    #endif

    //int ret = app.exec();
    int ret = 0;
    mvme_shutdown();
    return ret;
}
