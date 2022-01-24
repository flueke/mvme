#include <gtest/gtest.h>
#include <qnamespace.h>

#include "multi_crate.h"

TEST(multi_crate, MultiCrateConfigJson)
{
    using namespace multi_crate;

    MultiCrateConfig mcfg;
    mcfg.mainConfig = "crate0.vme";
    mcfg.secondaryConfigs = QStringList{ "crate1.vme", "crate2.vme", };
    mcfg.crossCrateEventIds = std::set<QUuid>{ QUuid::createUuid(), QUuid::createUuid() };

    auto jdoc = to_json_document(mcfg);
    auto mcfg2 = load_multi_crate_config(jdoc);

    ASSERT_EQ(mcfg, mcfg2);
}
