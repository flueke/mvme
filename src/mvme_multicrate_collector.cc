#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QApplication>
#include <spdlog/spdlog.h>

#include "analysis/analysis.h"
#include "mvme_session.h"
#include "vme_config.h"

using namespace mesytec::mvlc;
//using namespace mesytec::mvme;

std::atomic<bool> quit = false;

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
        struct sockaddr_in addr{};

        if (auto ec = eth::lookup(host, port, addr))
        {
            std::cerr << fmt::format("Error looking up host '{}': {}\n",
                host, ec.message());
            return 1;
        }

        auto sock = socket(AF_INET, SOCK_DGRAM, 0);

        if (sock < 0)
        {
            auto ec = std::error_code(errno, std::system_category());
            std::cerr << fmt::format("Error creating socket for MVLC data pipe: {}\n", ec.message());
            return 1;
        }

        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(sock, reinterpret_cast<struct sockaddr *>(&localAddr), sizeof(localAddr)))
        {
            auto ec = std::error_code(errno, std::system_category());
            std::cerr << fmt::format("Error binding socket for MVLC data pipe: {}\n", ec.message());
            return 1;
        }

        if (::connect(sock, reinterpret_cast<struct sockaddr *>(&addr),
                                sizeof(addr)))
        {
            auto ec = std::error_code(errno, std::system_category());
            std::cerr << fmt::format("connect() failed for data socket: {}\n", ec.message().c_str());
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

    while (!quit)
    {
        // TODO: use select to read from whatever socket is ready first instead of running into read timeouts here.
        // TODO2: use one thread per mvlc
        for (auto sock: mvlcDataSockets)
        {
            std::array<u8, 1500> destBuffer;
            destBuffer.fill(0);
            u16 bytesTransferred = 0;

            if (auto ec = eth::receive_one_packet(sock, destBuffer.data(), destBuffer.size(), bytesTransferred, 500))
            {
            }
        }
    }

    //int ret = app.exec();
    int ret = 0;
    mvme_shutdown();
    return ret;
}
