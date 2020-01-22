#include "gtest/gtest.h"
#include "vme_script_variables.h"

using namespace vme_script;

TEST(vme_script_variables, CompareVariables)
{
    {
        Variable v1;
        Variable v2;
        ASSERT_TRUE(v1 == v2);
        ASSERT_FALSE(v1 != v2);
    }

    {
        Variable v1("value1", "location1", "comment1");
        Variable v2("value2", "location2", "comment2");

        ASSERT_FALSE(v1 == v2);
        ASSERT_TRUE(v1 != v2);

        ASSERT_EQ(v1.value, QString("value1"));
        ASSERT_EQ(v1.definitionLocation, QString("location1"));
        ASSERT_EQ(v1.comment, QString("comment1"));

        ASSERT_EQ(v2.value, QString("value2"));
        ASSERT_EQ(v2.definitionLocation, QString("location2"));
        ASSERT_EQ(v2.comment, QString("comment2"));
    }

    {
        Variable v1("value", "location", "comment");
        Variable v2("value", "location", "comment");

        ASSERT_TRUE(v1 == v2);
        ASSERT_FALSE(v1 != v2);

        ASSERT_EQ(v1.value, v2.value);
        ASSERT_EQ(v1.definitionLocation, v2.definitionLocation);
        ASSERT_EQ(v1.comment, v2.comment);
    }

    // different values
    {
        Variable v1("value", "location", "comment");
        Variable v2("value2", "location", "comment");

        ASSERT_FALSE(v1 == v2);
        ASSERT_TRUE(v1 != v2);
    }

    // different locations
    {
        Variable v1("value", "location", "comment");
        Variable v2("value", "location2", "comment");

        ASSERT_FALSE(v1 == v2);
        ASSERT_TRUE(v1 != v2);
    }

    // different comments
    {
        Variable v1("value", "location", "comment");
        Variable v2("value", "location", "comment2");

        ASSERT_FALSE(v1 == v2);
        ASSERT_TRUE(v1 != v2);
    }
}

TEST(vme_script_variables, CompareSymbolTables)
{
    {
        SymbolTable symtab1;
        SymbolTable symtab2;

        ASSERT_TRUE(symtab1 == symtab2);
        ASSERT_FALSE(symtab1 != symtab2);

        symtab1.name = "tableName1";

        ASSERT_FALSE(symtab1 == symtab2);
        ASSERT_TRUE(symtab1 != symtab2);

        symtab2.name = "tableName1";

        ASSERT_TRUE(symtab1 == symtab2);
        ASSERT_FALSE(symtab1 != symtab2);
    }

    {
        SymbolTable symtab1;
        SymbolTable symtab2;

        symtab1["var1"] = Variable("value1");

        ASSERT_FALSE(symtab1 == symtab2);
        ASSERT_TRUE(symtab1 != symtab2);

        symtab2["var1"] = Variable("value1");

        ASSERT_TRUE(symtab1 == symtab2);
        ASSERT_FALSE(symtab1 != symtab2);

        symtab1["var1"].value = "foobar";

        ASSERT_FALSE(symtab1 == symtab2);
        ASSERT_TRUE(symtab1 != symtab2);
    }
}

TEST(vme_script_variables, SerializeVariable)
{
    Variable v1("value1", "location1", "comment1");

    auto vj1 = to_json(v1);

    auto v2 = variable_from_json(vj1);

    ASSERT_FALSE(vj1.isEmpty());
    ASSERT_TRUE(v1 == v2);

    v1.value = "value2";

    auto vj2 = to_json(v1);
    auto v3 = variable_from_json(vj2);

    ASSERT_FALSE(vj2.isEmpty());
    ASSERT_TRUE(v1 == v3);
    ASSERT_FALSE(v2 == v3);
}

TEST(vme_script_variables, SerializeSymbolTable)
{
    SymbolTable symtab1;

    symtab1.name = "symtab1";
    symtab1["var1"] =  Variable("value1", "location1", "comment1");
    symtab1["var2"] =  Variable("value2", "location2", "comment2");

    auto sj1 = to_json(symtab1);

    auto symtab2 = symboltable_from_json(sj1);

    ASSERT_FALSE(sj1.isEmpty());
    ASSERT_TRUE(symtab1 == symtab2);
    ASSERT_EQ(symtab1["var1"], symtab2["var1"]);
    ASSERT_EQ(symtab1["var2"], symtab2["var2"]);
}
