#include <iostream>
#include <QCoreApplication>
#include <QDebug>

#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_session.h"
#include "vme_config.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    mvme_init("mvme_to_mvlc");

    if (argc < 2)
    {
        std::cerr << "Error: no input file specified." << std::endl;
        return 1;
    }

    QString inFilename(argv[1]);

    std::unique_ptr<VMEConfig> vmeConfig;
    QString message;

    std::tie(vmeConfig, message) = read_vme_config_from_file(inFilename);

    if (!vmeConfig || !message.isEmpty())
    {
        std::cerr << "Error loading mvme VME config: " << message.toStdString() << std::endl;
        return 1;
    }

    mesytec::mvlc::CrateConfig dstConfig = mesytec::mvme::vmeconfig_to_crateconfig(vmeConfig.get());

    std::cout << mesytec::mvlc::to_yaml(dstConfig) << std::endl;

    return 0;
}
