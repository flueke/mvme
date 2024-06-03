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
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_session.h"
#include "qt_util.h"
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
