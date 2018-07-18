#include "gtest/gtest.h"
#include "analysis_util.h"

using namespace analysis;

TEST(analysis_util, CloneNames)
{
    {
        QSet<QString> names;
        QString clone;
        auto name = QSL("Name");

        clone = make_clone_name(name, names);
        ASSERT_EQ(clone, QString("Name"));
    }

    {
        QSet<QString> names = { QSL("Name") };
        QString clone;
        auto name = QSL("Name");

        clone = make_clone_name(name, names);
        ASSERT_EQ(clone, QString("Name Copy"));
    }

    {
        QSet<QString> names = { QSL("Name"), QSL("Name Copy") };
        QString clone;
        auto name = QSL("Name");

        clone = make_clone_name(name, names);
        ASSERT_EQ(clone, QString("Name Copy1"));
    }

    {
        QSet<QString> names = { QSL("Name"), QSL("Name Copy"), QSL("Name Copy1") };
        QString clone;
        auto name = QSL("Name");

        clone = make_clone_name(name, names);
        ASSERT_EQ(clone, QString("Name Copy2"));
    }
}
