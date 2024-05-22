#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QApplication>
#include <QTimer>

#include "util/signal_handling.h"
#include "mvme_session.h"
#include "multi_crate.h"
#include "vme_config_util.h"

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

    mvlc::listfile::ZipReader zipReader;
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
    std::vector<std::unique_ptr<VMEConfig>> vmeConfigs;

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

            vmeConfigs.emplace_back(std::move(vmeConfig));
        }
    }

    fmt::print("Read {} vme configs from {}\n", vmeConfigs.size(), listfileFilename);

    return 0;
}
