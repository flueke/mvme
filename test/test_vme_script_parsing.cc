#include "gtest/gtest.h"
#include "vme_script.h"
#include "vme_script_p.h"
#include <QDebug>

using namespace vme_script;

TEST(vme_script_variables, VariableToBool)
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

TEST(vme_script_variables, LookupVariable)
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

TEST(vme_script_variables, ExpandSingleVariable)
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

TEST(vme_script_variables, ExpandMultipleVariables)
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

TEST(vme_script_variables, ExpandVariablesErrors)
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


TEST(vme_script_expressions, EvaluateExpression)
{
    try
    {
        ASSERT_EQ(evaluate_expressions("$(1)", 0), QSL("1"));
        ASSERT_EQ(evaluate_expressions("$(3.14)", 0), QSL("3"));
        ASSERT_EQ(evaluate_expressions("$(7.4)", 0), QSL("7"));
        ASSERT_EQ(evaluate_expressions("$(7.5)", 0), QSL("8"));

        ASSERT_EQ(evaluate_expressions("$(1+1)", 0), QSL("2"));
        ASSERT_EQ(evaluate_expressions("$(1 + 1)", 0), QSL("2"));
        ASSERT_EQ(evaluate_expressions("$( 1+1 )", 0), QSL("2"));

        ASSERT_EQ(evaluate_expressions("$(3*4+8)", 0), QSL("20"));
        //ASSERT_EQ(evaluate_expressions("$(-(3*4+8))", 0), QSL("0"));
        ASSERT_THROW(evaluate_expressions("$(-(3*4+8))", 0), ParseError);

    } catch (ParseError &e)
    {
        std::cout << e.what().toStdString() << std::endl;
        ASSERT_TRUE(false);
    }
}

TEST(vme_script_expressions, ParseExpression)
{
    try
    {
        {
            // long form of the write command, quoted
            QString input = "write a32 d16 0x6070 \"$(1 + 3 + 5)\"";
            auto script = parse(input);
            ASSERT_EQ(script.size(), 1);
            ASSERT_EQ(script[0].type, CommandType::Write);
            ASSERT_EQ(script[0].address, 0x6070);
            ASSERT_EQ(script[0].value, 1+3+5);
        }

        {
            // long form of the write command, unquoted
            QString input = "write a32 d16 0x6070 $(1 + 3 + 5)";
            auto script = parse(input);
            ASSERT_EQ(script.size(), 1);
        }

        {
            // short form of the write command, quoted
            QString input = "0x6070 \"$(1 + 3 + 5)\"";
            auto script = parse(input);
            ASSERT_EQ(script.size(), 1);
        }

        {
            // short form of the write command
            QString input = "0x6070 $(1 + 3 + 5)";
            auto script = parse(input);
            ASSERT_EQ(script.size(), 1);
        }

    } catch (ParseError &e)
    {
        std::cout << e.what().toStdString() << std::endl;
        ASSERT_TRUE(false);
    }
}

TEST(vme_script_atomics, ReadVariableReference)
{
    {
        auto rr = read_atomic_variable_reference("{foo}");
        ASSERT_EQ(rr.first, "{foo}");
        ASSERT_TRUE(rr.second);
    }

    {
        auto rr = read_atomic_variable_reference("{foo} a  ");
        ASSERT_EQ(rr.first, "{foo}");
        ASSERT_TRUE(rr.second);
    }

    {
        auto rr = read_atomic_variable_reference("{ f o o }aa  ");
        ASSERT_EQ(rr.first, "{ f o o }");
        ASSERT_TRUE(rr.second);
    }

    {
        auto rr = read_atomic_variable_reference("{ f o o aa");
        ASSERT_EQ(rr.first, "{ f o o aa");
        ASSERT_FALSE(rr.second);
    }
}

TEST(vme_script_atomics, ReadExpression)
{
    {
        auto rr = read_atomic_expression("(foo)");
        ASSERT_EQ(rr.first, "(foo)");
        ASSERT_TRUE(rr.second);
    }

    {
        auto rr = read_atomic_expression("(foo))");
        ASSERT_EQ(rr.first, "(foo)");
        ASSERT_TRUE(rr.second);
    }

    {
        auto rr = read_atomic_expression("(foo) a");
        ASSERT_EQ(rr.first, "(foo)");
        ASSERT_TRUE(rr.second);
    }

    {
        auto rr = read_atomic_expression("(f o o)aa");
        ASSERT_EQ(rr.first, "(f o o)");
        ASSERT_TRUE(rr.second);
    }

    {
        auto rr = read_atomic_expression("((f o o)aa");
        ASSERT_EQ(rr.first, "((f o o)aa");
        ASSERT_FALSE(rr.second);
    }

    {
        auto rr = read_atomic_expression("((())) ");
        ASSERT_EQ(rr.first, "((()))");
        ASSERT_TRUE(rr.second);
    }
}

TEST(vme_script_atomics, SplitIntoParts)
{
    {
        auto parts = split_into_atomic_parts("a b c", 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "b");
        ASSERT_EQ(parts[2], "c");
    }

    {
        auto parts = split_into_atomic_parts(" a b c ", 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "b");
        ASSERT_EQ(parts[2], "c");
    }

    {
        auto parts = split_into_atomic_parts(" a \"foo bar\" c ", 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "foo bar");
        ASSERT_EQ(parts[2], "c");
    }

    {
        auto parts = split_into_atomic_parts(" a \"foo bar\"blob c ", 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "foo barblob");
        ASSERT_EQ(parts[2], "c");
    }

    {
        auto parts = split_into_atomic_parts(" a blob\"foo bar\" c ", 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "blobfoo bar");
        ASSERT_EQ(parts[2], "c");
    }

    {
        auto str = R"_(some"things"${that}slumber" should never be awoken"$(6 * 7)"one""two")_";
        auto parts = split_into_atomic_parts(str, 0);
        ASSERT_EQ(parts.size(), 1);
        ASSERT_EQ(parts[0], "somethings${that}slumber should never be awoken$(6 * 7)onetwo");
    }

    {
        auto str = "a $(1 * (2 + 3)) b";
        auto parts = split_into_atomic_parts(str, 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "$(1 * (2 + 3))");
        ASSERT_EQ(parts[2], "b");
    }

    {
        auto str = "a $(1 * (2 + ${myvar})) b";
        auto parts = split_into_atomic_parts(str, 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "$(1 * (2 + ${myvar}))");
        ASSERT_EQ(parts[2], "b");
    }

    {
        auto str = "a $(1 * (2 + ${my var})) b";
        auto parts = split_into_atomic_parts(str, 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "$(1 * (2 + ${my var}))");
        ASSERT_EQ(parts[2], "b");
    }

    {
        auto str = "a $(1 * (2 + $(6 * 7))) b";
        auto parts = split_into_atomic_parts(str, 0);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "$(1 * (2 + $(6 * 7)))");
        ASSERT_EQ(parts[2], "b");
    }

    {
        auto str = "a $(1 + 2) $(3 * 4)$(5+6) b";
        auto parts = split_into_atomic_parts(str, 0);
        ASSERT_EQ(parts.size(), 4);
        ASSERT_EQ(parts[0], "a");
        ASSERT_EQ(parts[1], "$(1 + 2)");
        ASSERT_EQ(parts[2], "$(3 * 4)$(5+6)");
        ASSERT_EQ(parts[3], "b");
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

TEST(vme_script_parsing, SetWithExpressions)
{
    {
        SymbolTables symtabs;
        symtabs.resize(1);
        SymbolTable &symtab0 = symtabs[0];

        QString input = "set foo \"$( 7 * 6 )\"\nset \"the var\" ${foo}";
        auto script = parse(input, symtabs);

        ASSERT_EQ(symtab0.value("foo").value, QSL("42"));
        ASSERT_EQ(symtab0.value("the var").value, QSL("42"));
    }

    {
        SymbolTables symtabs;
        symtabs.resize(1);
        SymbolTable &symtab0 = symtabs[0];

        QString input = "set foo $( 7 * 6 )\nset \"the var\" ${foo}";
        auto script = parse(input, symtabs);

        ASSERT_EQ(symtab0.value("foo").value, QSL("42"));
        ASSERT_EQ(symtab0.value("the var").value, QSL("42"));
    }
}
