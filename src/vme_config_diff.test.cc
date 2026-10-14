#include <gtest/gtest.h>

#include "vme_config_diff.h"

using namespace mesytec::mvme::vme_config;

// Helper to create a test VME config
std::unique_ptr<VMEConfig> create_test_config_1()
{
    auto vmeConfig = std::make_unique<VMEConfig>();
    vmeConfig->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000001}")));
    vmeConfig->setObjectName("TestConfig");
    vmeConfig->setVMEController(VMEControllerType::MVLC_USB);

    // Add an event
    auto event1 = new EventConfig();
    event1->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000002}")));
    event1->setObjectName("Event0");
    event1->irqLevel = 1;
    event1->irqVector = 0;
    event1->setVariable("var1", vme_script::Variable("value1", "This is test variable 1"));
    event1->setVariable("var2", vme_script::Variable("value2", "This is test variable 2"));
    vmeConfig->addEventConfig(event1);

    // Add a module to the event
    auto module1 = new ModuleConfig();
    module1->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000003}")));
    module1->setObjectName("Module0");
    module1->setBaseAddress(0x10000000);
    vats::VMEModuleMeta meta;
    meta.typeName = "mdpp16_scp";
    module1->setModuleMeta(meta);
    module1->getResetScript()->setId(
        QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000006}")));
    module1->getReadoutScript()->setId(
        QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000007}")));

    // Add init script to module
    auto initScript = new VMEScriptConfig("init", "# Init script\nwrite a32 d16 0x6008 1\n");
    initScript->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000004}")));
    module1->addInitScript(initScript);
    event1->addModuleConfig(module1);

    return vmeConfig;
}

// Modified version of test config 1
std::unique_ptr<VMEConfig> create_test_config_2()
{
    auto vmeConfig = std::make_unique<VMEConfig>();
    vmeConfig->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000001}")));
    vmeConfig->setObjectName("TestConfig");                   // Same name
    vmeConfig->setVMEController(VMEControllerType::MVLC_ETH); // Changed!

    // Same event with different IRQ
    auto event1 = new EventConfig();
    event1->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000002}")));
    event1->setObjectName("Event0");
    event1->irqLevel = 2; // Changed!
    event1->irqVector = 0;
    event1->setVariable("var1", vme_script::Variable("value1", "This is test variable 1"));
    event1->setVariable("var2", vme_script::Variable("value3", "This is test variable 2"));
    vmeConfig->addEventConfig(event1);

    // Same module with different address
    auto module1 = new ModuleConfig();
    module1->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000003}")));
    module1->setObjectName("Module0");
    module1->setBaseAddress(0x20000000); // Changed!
    vats::VMEModuleMeta meta;
    meta.typeName = "mdpp16_scp";
    module1->setModuleMeta(meta);
    module1->getResetScript()->setId(
        QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000006}")));
    module1->getReadoutScript()->setId(
        QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000007}")));

    auto initScript =
        new VMEScriptConfig("init", "# Init script\nwrite a32 d16 0x6008 2\n"); // Changed!
    initScript->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000004}")));
    module1->addInitScript(initScript);
    event1->addModuleConfig(module1);

    // Add a second module (new!)
    auto module2 = new ModuleConfig();
    module2->setId(QUuid::fromString(QSL("{00000000-0000-0000-0000-000000000005}")));
    module2->setObjectName("Module1");
    module2->setBaseAddress(0x30000000);
    module2->setModuleMeta(meta);
    event1->addModuleConfig(module2);

    return vmeConfig;
}

TEST(VMEConfigDiff, BasicDiff)
{
    std::cout << "=== Testing Basic Config Diff ===" << std::endl << std::endl;

    auto config1 = create_test_config_1();
    auto config2 = create_test_config_2();

    auto diff = diff_vme_configs(config1.get(), config2.get());

    std::cout << "Has changes: " << (diff.hasChanges() ? "YES" : "NO") << std::endl;
    std::cout << diff.toCompactSummary().toStdString() << std::endl << std::endl;
    std::cout << diff.toText().toStdString() << std::endl;
    ASSERT_TRUE(diff.hasChanges());
}


TEST(VMEConfigDiff, NoChanges)
{
    std::cout << "=== Testing No Changes ===" << std::endl << std::endl;

    auto config1 = create_test_config_1();
    auto config2 = create_test_config_1();

    auto diff = diff_vme_configs(config1.get(), config2.get());

    std::cout << "Has changes: " << (diff.hasChanges() ? "YES" : "NO") << std::endl;
    std::cout << diff.toText().toStdString() << std::endl;
    ASSERT_TRUE(!diff.hasChanges());
}
