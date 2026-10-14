/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvme_session.h"
#include "template_system.h"
#include "vme_config_diff.h"
#include "vme_config_util.h"

#include <QCoreApplication>
#include <QDebug>
#include <cassert>
#include <iostream>

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

void test_basic_diff()
{
    std::cout << "=== Testing Basic Config Diff ===" << std::endl << std::endl;

    auto config1 = create_test_config_1();
    auto config2 = create_test_config_2();

    auto diff = diff_vme_configs(config1.get(), config2.get());

    std::cout << "Has changes: " << (diff.hasChanges() ? "YES" : "NO") << std::endl;
    std::cout << diff.toCompactSummary().toStdString() << std::endl << std::endl;

    std::cout << diff.toText().toStdString() << std::endl;
}

void test_no_changes()
{
    std::cout << "=== Testing No Changes ===" << std::endl << std::endl;

    auto config1 = create_test_config_1();
    auto config2 = create_test_config_1();

    auto diff = diff_vme_configs(config1.get(), config2.get());

    std::cout << "Has changes: " << (diff.hasChanges() ? "YES" : "NO") << std::endl;
    std::cout << diff.toText().toStdString() << std::endl;
    assert(!diff.hasChanges());
}

void test_file_diff(const QString &file1, const QString &file2)
{
    std::cout << "=== Diffing Config Files ===" << std::endl;
    std::cout << "File A: " << file1.toStdString() << std::endl;
    std::cout << "File B: " << file2.toStdString() << std::endl << std::endl;

    auto [config1, error1] =
        read_vme_config_from_file(file1, [](const QString &msg) { qDebug() << msg; });

    if (!config1)
    {
        std::cerr << "Failed to load " << file1.toStdString() << std::endl;
        if (!error1.isEmpty())
            std::cerr << "Error: " << error1.toStdString() << std::endl;
        return;
    }

    auto [config2, error2] =
        read_vme_config_from_file(file2, [](const QString &msg) { qDebug() << msg; });

    if (!config2)
    {
        std::cerr << "Failed to load " << file2.toStdString() << std::endl;
        if (!error2.isEmpty())
            std::cerr << "Error: " << error2.toStdString() << std::endl;
        return;
    }

    auto diff = diff_vme_configs(config1.get(), config2.get());

    std::cout << "Has changes: " << (diff.hasChanges() ? "YES" : "NO") << std::endl;
    std::cout << diff.toCompactSummary().toStdString() << std::endl << std::endl;

    if (diff.hasChanges())
    {
        std::cout << diff.toText().toStdString() << std::endl;

        // Show statistics
        auto added = diff.getAddedObjects();
        auto removed = diff.getRemovedObjects();
        auto modified = diff.getModifiedObjects();

        std::cout << "\nStatistics:" << std::endl;
        std::cout << "  Added objects:    " << added.size() << std::endl;
        std::cout << "  Removed objects:  " << removed.size() << std::endl;
        std::cout << "  Modified objects: " << modified.size() << std::endl;
    }
}

int main(int argc, char *argv[])
{
    register_mvme_qt_metatypes();
    QCoreApplication app(argc, argv);

    if (argc == 3)
    {
        // Diff two files from command line
        test_file_diff(argv[1], argv[2]);
    }
    else
    {
        // Run built-in tests
        test_basic_diff();
        std::cout << std::endl;
        test_no_changes();

        if (argc == 1)
        {
            std::cout << "\nUsage: " << argv[0] << " <config_file_1> <config_file_2>" << std::endl;
            std::cout << "       Diffs two VME config files" << std::endl;
        }
    }

    return 0;
}
