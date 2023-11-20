#include <gtest/gtest.h>

#include <QCoreApplication>
#include "multi_crate.h"
#include "mvme_session.h"

using namespace mesytec::mvme::multi_crate;

// Providing a main() so that a QCoreApplication can be crated before any tests
// run. Needed to silence warnings from the vme template system which is used
// by the vme config code.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTimer::singleShot(0, [&]()
    {
        ::testing::InitGoogleTest(&argc, argv);
        auto testResult = RUN_ALL_TESTS();
        app.exit(testResult);
    });

    return app.exec();
}

TEST(multi_crate, ObjectMappingsJson)
{
    MultiCrateObjectMappings srcMappings;

    for (int i=0; i<10; ++i)
    {
        srcMappings.insertMapping(QUuid(), QUuid());
    }

    auto json = to_json(srcMappings);
    auto dstMappings = object_mappings_from_json(json);

    ASSERT_EQ(srcMappings, dstMappings);
}

TEST(multi_crate, MulticrateVMEConfigJson)
{
    register_mvme_qt_metatypes();

    auto conf0 = new VMEConfig;
    auto e00 = new EventConfig;
    auto m00 = new ModuleConfig;
    e00->addModuleConfig(m00);
    conf0->addEventConfig(e00);

    auto conf1 = new VMEConfig;
    auto e10 = new EventConfig;
    auto m10 = new ModuleConfig;
    e10->addModuleConfig(m10);
    conf1->addEventConfig(e10);

    MulticrateVMEConfig srcCfg;
    srcCfg.addCrateConfig(conf0);
    srcCfg.addCrateConfig(conf1);
    srcCfg.setIsCrossCrateEvent(0, true);
    srcCfg.setCrossCrateEventMainModuleId(0, m00->getId());
    srcCfg.setObjectSettings(m00->getId(), { { "foo", 42u }, { "bar", "asdf" } });
    ASSERT_TRUE(srcCfg.isCrossCrateEvent(0));
    ASSERT_FALSE(srcCfg.isCrossCrateEvent(1));
    ASSERT_EQ(srcCfg.getCrossCrateEventMainModuleId(0), m00->getId());
    ASSERT_TRUE(srcCfg.getCrossCrateEventMainModuleId(1).isNull());

    auto mergeResult = make_merged_vme_config(
        srcCfg.getCrateConfigs(),
        srcCfg.getCrossCrateEventIndexes(),
        srcCfg.getMergedObjectMappings());

    srcCfg.setMergedConfig(mergeResult.first.release());
    srcCfg.setMergedObjectMappings(mergeResult.second);

    // serialize
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
    ASSERT_EQ(dstCfg.getCrossCrateEventMainModuleId(0), m00->getId());
    ASSERT_TRUE(dstCfg.getCrossCrateEventMainModuleId(1).isNull());

    ASSERT_EQ(srcCfg.getMergedObjectMappings(), dstCfg.getMergedObjectMappings());
    ASSERT_EQ(srcCfg.getMergedConfig()->getId(), dstCfg.getMergedConfig()->getId());
    ASSERT_EQ(srcCfg.getObjectSettings(m00->getId()), dstCfg.getObjectSettings(m00->getId()));

    qDebug() << dstCfg.getObjectSettings(m00->getId());
}

TEST(multi_crate, MakeMergedVMEConfig)
{
    QObject parent;

    // main crate
    auto conf0 = new VMEConfig(&parent);

    {
        auto e00 = new EventConfig;
        auto m00 = new ModuleConfig;
        e00->addModuleConfig(m00);
        conf0->addEventConfig(e00);

        auto e01 = new EventConfig;
        auto m01 = new ModuleConfig;
        e01->addModuleConfig(m01);
        conf0->addEventConfig(e01);
    }

    // secondary crate
    auto conf1 = new VMEConfig(&parent);

    {
        auto e10 = new EventConfig;
        auto m10 = new ModuleConfig;
        e10->addModuleConfig(m10);
        conf1->addEventConfig(e10);

        auto e11 = new EventConfig;
        auto m11 = new ModuleConfig;
        e11->addModuleConfig(m11);
        conf1->addEventConfig(e11);
    }

    // event 0 is cross crate
    {
        auto mergeResult1 = make_merged_vme_config(
            std::vector<VMEConfig *>{ conf0, conf1},
            std::set<int>{ 0 });

        // 1 merged, two unmerged events
        ASSERT_EQ(mergeResult1.first->getEventConfigs().size(), 3);

        ASSERT_EQ(mergeResult1.first->getEventConfig(0)->getModuleConfigs().size(), 2);
        ASSERT_EQ(mergeResult1.first->getEventConfig(1)->getModuleConfigs().size(), 1);
        ASSERT_EQ(mergeResult1.first->getEventConfig(2)->getModuleConfigs().size(), 1);

        // 1 merged event, 2 unmerged events, 4 modules
        ASSERT_EQ(mergeResult1.second.cratesToMerged.size(), 7);

        auto mergeResult2 = make_merged_vme_config(
            std::vector<VMEConfig *>{ conf0, conf1},
            std::set<int>{ 0 },
            mergeResult1.second);

        for (int ei=0; ei<3; ++ei)
        {
            ASSERT_EQ(mergeResult1.first->getEventConfig(ei)->getId(),
                      mergeResult2.first->getEventConfig(ei)->getId());

            auto e1 = mergeResult1.first->getEventConfig(ei);
            auto e2 = mergeResult2.first->getEventConfig(ei);

            for (int mi=0; mi<e1->getModuleConfigs().size(); ++mi)
            {
                ASSERT_EQ(e1->getModuleConfig(mi)->getId(),
                          e2->getModuleConfig(mi)->getId());
            }
        }
    }

    // event 1 is cross crate
    {
        auto mergeResult1 = make_merged_vme_config(
            std::vector<VMEConfig *>{ conf0, conf1},
            std::set<int>{ 1 });

        // 1 merged, two unmerged events
        ASSERT_EQ(mergeResult1.first->getEventConfigs().size(), 3);

        ASSERT_EQ(mergeResult1.first->getEventConfig(0)->getModuleConfigs().size(), 2);
        ASSERT_EQ(mergeResult1.first->getEventConfig(1)->getModuleConfigs().size(), 1);
        ASSERT_EQ(mergeResult1.first->getEventConfig(2)->getModuleConfigs().size(), 1);

        // 1 merged event, 2 unmerged events, 4 modules
        ASSERT_EQ(mergeResult1.second.cratesToMerged.size(), 7);

        auto mergeResult2 = make_merged_vme_config(
            std::vector<VMEConfig *>{ conf0, conf1},
            std::set<int>{ 1 },
            mergeResult1.second);

        for (int ei=0; ei<3; ++ei)
        {
            ASSERT_EQ(mergeResult1.first->getEventConfig(ei)->getId(),
                      mergeResult2.first->getEventConfig(ei)->getId());

            auto e1 = mergeResult1.first->getEventConfig(ei);
            auto e2 = mergeResult2.first->getEventConfig(ei);

            for (int mi=0; mi<e1->getModuleConfigs().size(); ++mi)
            {
                ASSERT_EQ(e1->getModuleConfig(mi)->getId(),
                          e2->getModuleConfig(mi)->getId());
            }
        }
    }

    // both events0 and 1 are cross crate
    {
        auto mergeResult1 = make_merged_vme_config(
            std::vector<VMEConfig *>{ conf0, conf1 },
            std::set<int>{ 1, 0 });

        // 2 cross crate events merged from a total of 4 input events
        ASSERT_EQ(mergeResult1.first->getEventConfigs().size(), 2);

        ASSERT_EQ(mergeResult1.first->getEventConfig(0)->getModuleConfigs().size(), 2);
        ASSERT_EQ(mergeResult1.first->getEventConfig(1)->getModuleConfigs().size(), 2);

        // 2 merged events, 4 modules
        ASSERT_EQ(mergeResult1.second.cratesToMerged.size(), 6);

        auto mergeResult2 = make_merged_vme_config(
            std::vector<VMEConfig *>{ conf0, conf1},
            std::set<int>{ 1, 0 },
            mergeResult1.second);

        for (int ei=0; ei<2; ++ei)
        {
            ASSERT_EQ(mergeResult1.first->getEventConfig(ei)->getId(),
                      mergeResult2.first->getEventConfig(ei)->getId());

            auto e1 = mergeResult1.first->getEventConfig(ei);
            auto e2 = mergeResult2.first->getEventConfig(ei);

            for (int mi=0; mi<e1->getModuleConfigs().size(); ++mi)
            {
                ASSERT_EQ(e1->getModuleConfig(mi)->getId(),
                          e2->getModuleConfig(mi)->getId());
            }
        }
    }
}
