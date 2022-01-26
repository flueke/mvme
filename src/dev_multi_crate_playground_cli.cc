#include "multi_crate.h"

using namespace multi_crate;

int main(int argc, char *argv[])
{
    MultiCrateConfig mcfg;

    mcfg.mainConfig = "main.vme";
    mcfg.secondaryConfigs = QStringList{ "secondary1.vme" };
    mcfg.crossCrateEventIds = { QUuid::fromString(QSL("{ac3e7e4a-b322-42ee-a203-59f862f109ea}")) };
    mcfg.mainModuleIds = { QUuid::fromString(QSL("{3e87cd9b-b2e9-448a-92d7-7a540a1bce35}")) };

    auto jdoc = to_json_document(mcfg);

    QFile outFile("multicrate.mmulticfg");

    if (!outFile.open(QIODevice::WriteOnly))
        return 1;

    outFile.write(jdoc.toJson());

    return 0;
}
