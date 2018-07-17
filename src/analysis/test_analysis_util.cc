#include "gtest/gtest.h"
#include "analysis_util.h"

using namespace analysis;

TEST(analysis_util, CloneNames)
{
    QSet<QString> names;
    QString clone;
    auto name = QSL("Name");

    clone = make_clone_name(name, names);
    ASSERT_EQ(clone, QString("Name"));
    names.insert(clone);

    clone = make_clone_name(name, names);
    ASSERT_EQ(clone, QString("Name Copy"));
    names.insert(clone);

    clone = make_clone_name(name, names);
    ASSERT_EQ(clone, QString("Name Copy1"));
    names.insert(clone);
}
