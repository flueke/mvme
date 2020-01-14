#include "gtest/gtest.h"
#include "vme_script.h"
#include <QDebug>

using namespace vme_script;

TEST(vme_script_parsing, VariableToBool)
{
    {
        Variable v { "value", 42 };
        ASSERT_TRUE(v);
    }

    {
        Variable v { " ", 42 };
        ASSERT_TRUE(v);
    }

    {
        Variable v { "", 42 };
        ASSERT_FALSE(v);
    }

    {
        Variable v { {}, 42 };
        ASSERT_FALSE(v);
    }
}

TEST(vme_script_parsing, LookupVariable)
{
    SymbolTables symtabs =
    {
        { { QSL("aa"), Variable("firstValue") } },
        { { QSL("bb"), Variable("secondValue") } },
        { { QSL("aa"), Variable("thirdValue") } },
    };

    {
        auto v = lookup_variable("foo", symtabs);
        ASSERT_FALSE(v);
    }

    {
        auto v = lookup_variable("aa", symtabs);
        ASSERT_TRUE(v);
        ASSERT_EQ(v.value, QSL("firstValue"));
    }

    {
        auto v = lookup_variable("bb", symtabs);
        ASSERT_TRUE(v);
        ASSERT_EQ(v.value, QSL("secondValue"));
    }
}

TEST(vme_script_parsing, ExpandSingleVariable)
{
    //try
    //{

    SymbolTables symtabs =
    {
        {{ QSL("foo"), Variable("bar") }},
        {{ QSL("truth"), Variable("42") }},
    };

    {
        QString input;
        ASSERT_TRUE(expand_variables(input, symtabs, 0).isEmpty());
    }

    {
        QString input = "${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar"));
    }

    {
        QString input = " ${foo} ";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL(" bar "));
    }

    {
        QString input = "${foo} sss";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar sss"));
    }

    {
        QString input = " ${foo} sss";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL(" bar sss"));
    }

    {
        QString input = "ppp ${foo} sss";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("ppp bar sss"));
    }

    {
        QString input = " ppp  ${foo}  sss ";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL(" ppp  bar  sss "));
    }

    {
        QString input = "ppp ${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("ppp bar"));
    }

    {
        QString input = "ppp ${foo} ";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("ppp bar "));
    }

    {
        QString input = "$${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("$bar"));
    }

    {
        QString input = "${foo}$";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar$"));
    }

    {
        QString input = "{${foo}}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("{bar}"));
    }

    {
        SymbolTables symtabs2 = symtabs;
        symtabs2.push_back({{ QSL("${weird"), Variable("odd") }});

        QString input = "${${weird}";
        ASSERT_EQ(expand_variables(input, symtabs2, 0), QSL("odd"));
    }

    {
        SymbolTables symtabs2 = symtabs;
        symtabs2.push_back({{ QSL("dollars"), Variable("${euros}") }});

        QString input = "${dollars}";
        ASSERT_EQ(expand_variables(input, symtabs2, 0), QSL("${euros}"));
    }

    {
        SymbolTables symtabs2 = symtabs;
        symtabs2.push_back({{ QSL("broken\nthings"), Variable("are interesting") }});

        QString input = "${broken\nthings}";
        ASSERT_EQ(expand_variables(input, symtabs2, 0), QSL("are interesting"));
    }

    //} catch (ParseError &e)
    //{
    //    std::cout << e.what().toStdString() << std::endl;
    //    ASSERT_TRUE(false);
    //}
}

TEST(vme_script_parsing, ExpandMultipleVariables)
{
    SymbolTables symtabs =
    {
        {{ QSL("foo"), Variable("bar") }},
        {{ QSL("truth"), Variable("42") }},
    };

    //try
    //{

    {
        QString input;
        ASSERT_TRUE(expand_variables(input, symtabs, 0).isEmpty());
    }

    {
        QString input = "${foo}${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("barbar"));
    }

    {
        QString input = "${foo} ${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar bar"));
    }

    {
        QString input = "${truth}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("42"));
    }

    {
        QString input = "p${truth}${foo}s";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("p42bars"));
    }

    //} catch (ParseError &e)
    //{
    //    std::cout << e.what().toStdString() << std::endl;
    //    ASSERT_TRUE(false);
    //}
}

TEST(vme_script_parsing, ExpandVariablesErrors)
{
    SymbolTables symtabs =
    {
        {{ QSL("foo"), Variable("bar") }},
        {{ QSL("truth"), Variable("42") }},
    };

    {
        QString input = "${undefined_name}";
        ASSERT_THROW(expand_variables(input, symtabs, 0), ParseError);
    }

    {
        QString input = "${foo }";
        ASSERT_THROW(expand_variables(input, symtabs, 0), ParseError);
    }

    {
        QString input = "${ foo}";
        ASSERT_THROW(expand_variables(input, symtabs, 0), ParseError);
    }

    {
        QString input = "${ foo}";
        ASSERT_THROW(expand_variables(input, symtabs, 0), ParseError);
    }

    {
        QString input = "ppp ${foo sss";
        ASSERT_THROW(expand_variables(input, symtabs, 0), ParseError);
    }
}


TEST(vme_script_parsing, EvaluateExpression)
{
    ASSERT_EQ(evaluate_expressions("$(1)", 0), QSL("1"));
    ASSERT_EQ(evaluate_expressions("$(3.14)", 0), QSL("3"));
    ASSERT_EQ(evaluate_expressions("$(7.4)", 0), QSL("7"));
    ASSERT_EQ(evaluate_expressions("$(7.5)", 0), QSL("8"));

    ASSERT_EQ(evaluate_expressions("$(1+1)", 0), QSL("2"));
    ASSERT_EQ(evaluate_expressions("$(1 + 1)", 0), QSL("2"));
    ASSERT_EQ(evaluate_expressions("$( 1+1 )", 0), QSL("2"));

    ASSERT_EQ(evaluate_expressions("$(3*4+8)", 0), QSL("20"));
    ASSERT_EQ(evaluate_expressions("$(-(3*4+8))", 0), QSL("0")); // negative results should be set to 0
}

TEST(vme_script_parsing, ParseExpression)
{
    try
    {
        QString input = R"_(write a32 d16 0x6070 "$(1 + 3 + 5)")_";
        auto script = parse(input);
        ASSERT_EQ(script.size(), 1);

    } catch (ParseError &e)
    {
        std::cout << e.what().toStdString() << std::endl;
        ASSERT_TRUE(false);
    }
}

TEST(vme_script_parsing, SetVariableCommand)
{
    {
        SymbolTables symtabs;
        symtabs.resize(2);

        SymbolTable &symtab0 = symtabs[0];
        SymbolTable &symtab1 = symtabs[1];

        QString input = "set foo bar";
        auto script = parse(input, symtabs);
        ASSERT_EQ(symtab0.value("foo").value, QSL("bar"));
        ASSERT_TRUE(script.isEmpty());
        ASSERT_TRUE(symtab1.isEmpty());

        input = "set foo yay";
        script = parse(input, symtabs);
        ASSERT_EQ(symtab0.value("foo").value, QSL("yay"));
        ASSERT_TRUE(script.isEmpty());
        ASSERT_TRUE(symtab1.isEmpty());

        input = "set var2 stuff";
        script = parse(input, symtabs);
        ASSERT_EQ(symtab0.value("foo").value, QSL("yay"));
        ASSERT_EQ(symtab0.value("var2").value, QSL("stuff"));
        ASSERT_TRUE(script.isEmpty());
        ASSERT_TRUE(symtab1.isEmpty());

        input = "set var2 ${foo}";
        script = parse(input, symtabs);
        ASSERT_EQ(symtab0.value("foo").value, QSL("yay"));
        ASSERT_EQ(symtab0.value("var2").value, QSL("yay"));
        ASSERT_TRUE(script.isEmpty());
        ASSERT_TRUE(symtab1.isEmpty());

        input = "set var2 things\nset var2 ${var2}";
        script = parse(input, symtabs);
        ASSERT_EQ(symtab0.value("foo").value, QSL("yay"));
        ASSERT_EQ(symtab0.value("var2").value, QSL("things"));
        ASSERT_TRUE(script.isEmpty());
        ASSERT_TRUE(symtab1.isEmpty());
    }
}

#if 0
TEST(vme_script_parsing, QuotedExpressions)
{
    SymbolTables symtabs;
    symtabs.resize(1);
    SymbolTable &symtab0 = symtabs[0];

    QString input = "set foo \"$( 7 * 6 )\"\nset \"the var\" ${foo}";
    auto script = parse(input, symtabs);

    qDebug() << symtab0.value("foo").value;

    ASSERT_EQ(symtab0.value("foo").value, QSL("42"));
    ASSERT_EQ(symtab0.value("the var").value, QSL("42"));

}
#endif

