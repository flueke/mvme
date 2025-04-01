#include <gtest/gtest.h>
#include "template_system.h"
#include <QCoreApplication>
#include <QTimer>
#include <map>

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

TEST(vats, LoadTemplates)
{
    auto templates = vats::read_templates();

    ASSERT_TRUE(templates.moduleMetas.size() > 0);
    std::map<QString, size_t> typeNameCounts;

    for (const auto &mm: templates.moduleMetas)
    {
        qDebug() << "ModuleMeta: typename =" << mm.typeName
            << ", templateFile =" << mm.templateFile
            << ", templatePath =" << mm.templatePath;
        ASSERT_TRUE(mm.typeId > 0 || mm.typeName.size() > 0);
        ASSERT_TRUE(mm.typeName.size() > 0);
        ASSERT_TRUE(mm.displayName.size() > 0);
        ASSERT_TRUE(mm.templatePath.size() > 0);
        ASSERT_TRUE(mm.eventSizeFilters.size() >= 0);

        qDebug() << "#eventSizeFilters =" << mm.eventSizeFilters.size();
        for (const auto &filterDef: mm.eventSizeFilters)
        {
            qDebug() << "  filterString =" << filterDef.filterString
                     << ", description =" << filterDef.description;
        }

        for (const auto &filterDef: mm.eventSizeFilters)
        {
            ASSERT_TRUE(filterDef.filterString.size() > 0);
            ASSERT_TRUE(filterDef.description.size() > 0);
        }

        ASSERT_EQ(typeNameCounts[mm.typeName], 0u);
        ++typeNameCounts[mm.typeName];
    }
}
