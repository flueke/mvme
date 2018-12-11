#include "analysis/analysis.h"
#include "data_export/data_export_util.h"
#include "vme_config.h"

#include <iostream>
#include <QCoreApplication>
#include <QJsonDocument>

using std::cerr;
using std::cout;
using std::endl;
using namespace analysis;

// Generate code for ROOT tree based input/output. Uses the combined
// information from mvme VME and Analysis config files
//
// Input:
//   Path to workspace
//   or directly vme and analysis config files plus name for the project
//   output directory
//
// Output:
//  Header and Implementation file for the defined events and data sources.

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc != 4)
    {
        cout << "Usage: " << argv[0] << " <VMEConfig> <AnalysisConfig> <ProjectName>" << endl;
        return 1;
    }

    int argno = 1;
    QString vmeConfigFilename = argv[argno++];
    QString analysisConfigFilename = argv[argno++];
    QString projectName = argv[argno++];

    std::unique_ptr<VMEConfig> vmeConfig;

    {
        auto readResult = read_vme_config_from_file(vmeConfigFilename);

        if (!readResult.first)
        {
            cout << "Error reading VME config: " << readResult.second.toStdString() << endl;
            return 1;
        }

        vmeConfig = std::move(readResult.first);
    }

    std::unique_ptr<Analysis> analysis;

    {
        auto readResult = read_analysis_config_from_file(analysisConfigFilename, vmeConfig.get());

        if (!readResult.first)
        {
            cout << "Error reading Analysis config: " << readResult.second.toStdString() << endl;
            return 1;
        }

        analysis = std::move(readResult.first);
    }

    auto outputInfo = make_output_data_description(vmeConfig.get(), analysis.get());
    QJsonDocument outputInfoDoc(outputInfo);

    cout << "Output info generated from " << vmeConfigFilename.toStdString()
        << " and " << analysisConfigFilename.toStdString() << ":" << endl
        << outputInfoDoc.toJson().toStdString();

    return 0;
}
