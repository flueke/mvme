#include <gtest/gtest.h>

#include "multi_crate.h"
#include "mvme_session.h"

#if 0
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
#endif

TEST(multi_crate, MulticrateVMEConfigJson)
{
    register_mvme_qt_metatypes();

    auto conf0 = new VMEConfig;
    auto e0 = new EventConfig;
    auto m0 = new ModuleConfig;
    e0->addModuleConfig(m0);
    conf0->addEventConfig(e0);

    auto conf1 = new VMEConfig;
    auto e1 = new EventConfig;
    auto m1 = new ModuleConfig;
    e1->addModuleConfig(m1);
    conf1->addEventConfig(e1);

    using namespace multi_crate;

    MulticrateVMEConfig srcCfg;
    srcCfg.addCrateConfig(conf0);
    srcCfg.addCrateConfig(conf1);
    srcCfg.setIsCrossCrateEvent(0, true);
    srcCfg.setCrossCrateEventMainModuleId(0, m0->getId());

    ASSERT_TRUE(srcCfg.isCrossCrateEvent(0));
    ASSERT_FALSE(srcCfg.isCrossCrateEvent(1));
    ASSERT_EQ(srcCfg.getCrossCrateEventMainModuleId(0), m0->getId());
    ASSERT_TRUE(srcCfg.getCrossCrateEventMainModuleId(1).isNull());

    QJsonObject json;
    srcCfg.write(json);

    MulticrateVMEConfig dstCfg;
    dstCfg.read(json);

    ASSERT_EQ(srcCfg.getId(), dstCfg.getId());

    ASSERT_EQ(srcCfg.getCrateConfigs()[0]->getId(),
              dstCfg.getCrateConfigs()[0]->getId());

    ASSERT_EQ(srcCfg.getCrateConfigs()[0]->getEventConfigs()[0]->getId(),
              dstCfg.getCrateConfigs()[0]->getEventConfigs()[0]->getId());

    ASSERT_TRUE(dstCfg.isCrossCrateEvent(0));
    ASSERT_FALSE(dstCfg.isCrossCrateEvent(1));
    ASSERT_EQ(dstCfg.getCrossCrateEventMainModuleId(0), m0->getId());
    ASSERT_TRUE(dstCfg.getCrossCrateEventMainModuleId(1).isNull());
}
