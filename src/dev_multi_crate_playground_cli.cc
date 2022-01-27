#include "multi_crate.h"

#include <array>
#include <fmt/format.h>

using namespace multi_crate;

#if 0
// Writes a MultiCrateConfig to file. Uses hardcoded data.
int main(int argc, char *argv[])
{
    MultiCrateConfig mcfg;

    mcfg.mainConfig = "main.vme";
    mcfg.secondaryConfigs = QStringList{ "secondary1.vme" };
    mcfg.crossCrateEventIds = { QUuid::fromString(QSL("{ac3e7e4a-b322-42ee-a203-59f862f109ea}")) };
    mcfg.mainModuleIds = { QUuid::fromString(QSL("{3e87cd9b-b2e9-448a-92d7-7a540a1bce35}")) };

    auto jdoc = to_json_document(mcfg);

    QFile outFile("multicrate.multicratecfg");

    if (!outFile.open(QIODevice::WriteOnly))
        return 1;

    outFile.write(jdoc.toJson());

    return 0;
}
#endif

int main(int argc, char *argv[])
{
    auto vmeConfigFiles =
    {
        "crate0.vme",
        "crate1.vme",
    };

    std::set<int> crossCrateEvents = { 0 };

    // Specifier for the main/reference module in cross crate events.
    // Format: crateIndex, eventIndex, moduleIndex
    using MainModuleSpec = std::array<int, 3>;

    // One MainModuleSpec per cross crate event is required.
    std::vector<MainModuleSpec> mainModules =
    {
        { 0, 0, 0 }
    };

    // Read in the vmeconfigs

    std::vector<std::unique_ptr<VMEConfig>> crateVMEConfigs;

    for (const auto &filename: vmeConfigFiles)
    {
        auto readResult = read_vme_config_from_file(filename);

        if (!readResult.first)
            throw std::runtime_error(fmt::format("Error reading vme config {}: {}",
                                                 filename, readResult.second.toStdString()));
        crateVMEConfigs.emplace_back(std::move(readResult.first));
    }

    // Create the merged configs and module id mappings.

    std::unique_ptr<VMEConfig> mergedVMEConfig;
    MultiCrateModuleMappings moduleIdMappings;

    std::tie(mergedVMEConfig, moduleIdMappings) = make_merged_vme_config(
        crateVMEConfigs, crossCrateEvents);

    // XXX leftoff here
}
