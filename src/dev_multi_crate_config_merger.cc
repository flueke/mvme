#include <QApplication>
#include <iostream>
#include <fmt/format.h>
//#include <set>
//#include <vector>

#include "mvme_session.h"
#include "multi_crate.h"
#include "vme_config.h"
#include "vme_config_util.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    mvme_init("dev_multi_crate_config_merger");

    auto args = app.arguments();

    if (args.size() < 3)
        return 1;

    args.pop_front(); // pop off the exe name

    std::vector<std::unique_ptr<VMEConfig>> crateConfigs;

    for (auto filename: args)
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

    std::vector<VMEConfig *> rawCrateConfigs;

    for (auto &crateConf: crateConfigs)
        rawCrateConfigs.push_back(crateConf.get());

    std::set<int> crossCrateEvents = { 0 };

    auto mergeResult = multi_crate::make_merged_vme_config(rawCrateConfigs, crossCrateEvents);

    auto jsonDoc = mvme::vme_config::serialize_vme_config_to_json_document(*mergeResult.first);
    auto jsonText = jsonDoc.toJson();
    std::cout << jsonText.toStdString();

    return 0;
}
