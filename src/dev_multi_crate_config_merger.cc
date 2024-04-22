#include <QApplication>
#include <iostream>
#include <lyra/lyra.hpp>
#include <set>
#include <vector>

#include "mvme_session.h"
#include "multi_crate.h"
#include "vme_config.h"
#include "vme_config_tree.h"
#include "vme_config_util.h"

using std::cerr;
using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    mvme_init("dev_multi_crate_config_merger");

    bool opt_showHelp = false;
    std::vector<int> opt_crossCrateEvents;
    std::vector<std::string> opt_crateConfigPaths;

    auto cli
        = lyra::help(opt_showHelp)

        | lyra::opt(opt_crossCrateEvents, "event index")
            .name("-e")
            ["--cross-event"]("zero based event indexes specifying cross crate events")
            .cardinality(0, 10)

        | lyra::arg(opt_crateConfigPaths, "crate vme config")
            .cardinality(1, 10)
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: "
            << cliParseResult.errorMessage() << endl;
        return 1;
    }

    if (opt_showHelp)
    {
        cout << cli << endl;

        cout << "write me!" << endl;
        return 0;
    }

    QStringList crateConfigPaths;

    for (auto path: opt_crateConfigPaths)
        crateConfigPaths.append(QString::fromStdString(path));

    std::set<int> crossCrateEvents;

    for (auto ei: opt_crossCrateEvents)
        crossCrateEvents.insert(ei);

    std::vector<std::unique_ptr<VMEConfig>> crateConfigs;

    for (auto filename: crateConfigPaths)
    {
        auto readResult = read_vme_config_from_file(filename);
        if (!readResult.second.isEmpty())
        {
            throw std::runtime_error(
                fmt::format("file={}, error={}",
                            filename.toStdString(),
                            readResult.second.toStdString()));
        }
        crateConfigs.emplace_back(std::move(readResult.first));
    }

    auto mergeResult = mesytec::mvme::multi_crate::make_merged_vme_config(crateConfigs, crossCrateEvents);

    for (auto &crateConfig: crateConfigs)
    {
        auto configTreeWidget = new VMEConfigTreeWidget;
        configTreeWidget->setWindowTitle("crate");
        configTreeWidget->resize(600, 800);
        configTreeWidget->setConfig(crateConfig.get());
        configTreeWidget->show();
    }

    //auto jsonDoc = mvme::vme_config::serialize_vme_config_to_json_document(*mergeResult.first);
    //auto jsonText = jsonDoc.toJson();
    //std::cout << jsonText.toStdString();

    VMEConfigTreeWidget configTreeWidget;
    configTreeWidget.setWindowTitle("merged");
    configTreeWidget.resize(600, 800);
    configTreeWidget.setConfig(mergeResult.first.get());
    configTreeWidget.show();

    return app.exec();
}
