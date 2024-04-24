#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/util/udp_sockets.h>
#include <QApplication>
#include <spdlog/spdlog.h>
#include <signal.h>

#ifndef __WIN32
#include <poll.h>
#else
#include <winsock2.h>
#endif

#include "analysis/analysis.h"
#include "mvme_session.h"
#include "vme_config.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"

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

    // Create socktes for the mvlc data pipes.
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

    std::unique_ptr<listfile::ZipCreator> listfileCreator;
    std::unique_ptr<listfile::WriteHandle> lfh;

    // Open the output listfile
    if (parser("--listfile") >> str)
    {
        try
        {
            listfileCreator = std::make_unique<listfile::ZipCreator>();
            listfileCreator->createArchive(str);
            lfh = listfileCreator->createZIPEntry("listfile.mvlclst");
            spdlog::info("Opened output listfile {}", str);
            listfile::listfile_write_magic(*lfh, ConnectionType::ETH);
            listfile::listfile_write_endian_marker(*lfh, 0);

            for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
            {
                auto crateConfig = vmeconfig_to_crateconfig(vmeConfigs[crateId].get());
                crateConfig.crateId = crateId;
                listfile::listfile_write_crate_config(*lfh, crateConfig);
                mvme_mvlc::listfile_write_mvme_config(*lfh, crateId, *vmeConfigs[crateId]);
            }

            for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
            {
                listfile::listfile_write_timestamp_section(
                    *lfh, crateId, system_event::subtype::BeginRun);
            }
        }
        catch(const std::exception& e)
        {
            spdlog::error("Error opening output listfile {}: {}", str, e.what());
            return 1;
        }
    }

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

    //int ret = app.exec();
    int ret = 0;
    mvme_shutdown();
    return ret;
}
