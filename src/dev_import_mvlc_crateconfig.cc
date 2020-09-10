// Dev tool for loading a mesytec-mvlc CrateConfig YAML file and converting it
// to a mvme VMEConfig.

#include <QCoreApplication>
#include <iostream>
#include <QFile>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QJsonDocument>
#include "mvlc/vmeconfig_from_crateconfig.h"
#include "vme_config_util.h"

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec;
using namespace mesytec::mvme;

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cerr << "Invalid arguments given." << endl;
        cerr << "Usage: " << argv[0] << " <input_mvlc_crateconfig.yaml> <output_mvme_vmeconfig.vme>" << endl;
        return 1;
    }

    QString inputFilename(argv[1]);
    QString outputFilename(argv[2]);

    QFile inputFile(inputFilename);

    if (!inputFile.open(QIODevice::ReadOnly))
    {
        cerr << "Error opening input file " << inputFilename.toStdString() << " for reading" << endl;
        return 1;
    }


    // read crateconfig from yaml file
    auto inputData = inputFile.readAll().toStdString();
    mvlc::CrateConfig crateConfig;

    try
    {
        crateConfig = mvlc::crate_config_from_yaml(inputData);
    }
    catch (const std::runtime_error &e)
    {
        cerr << "Error parsing input file: " << e.what() << endl;
        return 1;
    }

    // do the import/conversion in a sane way, filling the VMEConfig
    // then serialize and write out the VMEConfig
    try
    {
        auto vmeConfig = vmeconfig_from_crateconfig(crateConfig);

        QFile outFile(outputFilename);
        if (!outFile.open(QIODevice::WriteOnly))
        {
            cerr << "Error opening output file " << outputFilename.toStdString() << " for writing" << endl;
            return 1;
        }

        if (!::mvme::vme_config::serialize_vme_config_to_device(outFile, *vmeConfig))
        {
            cerr << "Error writing to output file " << outputFilename.toStdString() << ": "
                << outFile.errorString().toStdString() << endl;
            return 1;
        }
    }
    catch (...)
    {
        throw "TODO! handle exceptions here";
    }
}
