#include "gtest/gtest.h"
#include "vme_script.h"
#include "vme_script_p.h"
#include "vme_script_variables.h"
#include "util/qt_str.h"
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
        ASSERT_TRUE(v);
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
        { "first",  { { QSL("aa"), Variable("firstValue") } } },
        { "second", { { QSL("bb"), Variable("secondValue") } } },
        { "third",  { { QSL("aa"), Variable("thirdValue") } } },
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
        { "first", {{ QSL("foo"), Variable("bar") }}},
        { "second", {{ QSL("truth"), Variable("42") }}},
    };

    {
        QString input;
        ASSERT_TRUE(expand_variables(input, symtabs, 0).isEmpty());

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 0);
    }

    {
        QString input = "${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = " ${foo} ";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL(" bar "));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "${foo} sss";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar sss"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = " ${foo} sss";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL(" bar sss"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "ppp ${foo} sss";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("ppp bar sss"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = " ppp  ${foo}  sss ";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL(" ppp  bar  sss "));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "ppp ${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("ppp bar"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "ppp ${foo} ";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("ppp bar "));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "$${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("$bar"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "${foo}$";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar$"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "{${foo}}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("{bar}"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        SymbolTables symtabs2 = symtabs;
        symtabs2.push_back({"first", {{ QSL("${weird"), Variable("odd") }}});

        QString input = "${${weird}";
        ASSERT_EQ(expand_variables(input, symtabs2, 0), QSL("odd"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("${weird") });
    }

    {
        SymbolTables symtabs2 = symtabs;
        symtabs2.push_back({"first", {{ QSL("dollars"), Variable("${euros}") }}});

        QString input = "${dollars}";
        ASSERT_EQ(expand_variables(input, symtabs2, 0), QSL("${euros}"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("dollars") });
    }

    {
        SymbolTables symtabs2 = symtabs;
        symtabs2.push_back({"first", {{ QSL("broken\nthings"), Variable("are interesting") }}});

        QString input = "${broken\nthings}";
        ASSERT_EQ(expand_variables(input, symtabs2, 0), QSL("are interesting"));

        // Note: this does not work with pre_parse() because it will split the
        // input string into two separate lines.
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
        {"first", {{ QSL("foo"), Variable("bar") }}},
        {"second", {{ QSL("truth"), Variable("42") }}},
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

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "${foo} ${foo}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("bar bar"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("foo") });
    }

    {
        QString input = "${truth}";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("42"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        ASSERT_EQ(pp[0].varRefs, QSet<QString>{ QSL("truth") });
    }

    {
        QString input = "p${truth}${foo}s";
        ASSERT_EQ(expand_variables(input, symtabs, 0), QSL("p42bars"));

        auto pp = pre_parse(input);
        ASSERT_EQ(pp.size(), 1);
        QSet<QString> expected{ QSL("truth"), QSL("foo") };
        ASSERT_EQ(pp[0].varRefs, expected);
    }

    //} catch (ParseError &e)
    //{
    //    std::cout << e.toString().toStdString() << std::endl;
    //    ASSERT_TRUE(false);
    //}
}

TEST(vme_script_parsing, ExpandVariablesErrors)
{
    SymbolTables symtabs =
    {
        {"first", {{ QSL("foo"), Variable("bar") }}},
        {"second", {{ QSL("truth"), Variable("42") }}},
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

TEST(vme_script_parsing, CollectVarRefs)
{
    {
        QString input = "${foo}\n${bar} ${fourtytwo}";
        QSet<QString> expected = { "foo", "bar", "fourtytwo" };
        ASSERT_EQ(collect_variable_references(input), expected);
    }

    {
        // unterminated variable reference
        QString input = "${foo}\n${bar} ${fourtytwo";
        ASSERT_THROW(collect_variable_references(input), ParseError);
    }
}


TEST(vme_script_expressions, EvaluateExpression)
{
    try
    {
        ASSERT_EQ(evaluate_expressions("$(1)", 0), QSL("1"));
        ASSERT_EQ(evaluate_expressions("$(3.14)", 0), QSL("3.14"));
        ASSERT_EQ(evaluate_expressions("$(7.4)", 0), QSL("7.4"));
        ASSERT_EQ(evaluate_expressions("$(7.5)", 0), QSL("7.5"));

        ASSERT_EQ(evaluate_expressions("$(1+1)", 0), QSL("2"));
        ASSERT_EQ(evaluate_expressions("$(1 + 1)", 0), QSL("2"));
        ASSERT_EQ(evaluate_expressions("$( 1+1 )", 0), QSL("2"));

        ASSERT_EQ(evaluate_expressions("$(3*4+8)", 0), QSL("20"));
        ASSERT_EQ(evaluate_expressions("$(-(3*4+8))", 0), QSL("-20"));

    } catch (ParseError &e)
    {
        std::cout << e.toString().toStdString() << std::endl;
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

        // negative results

        {
            // long form of the write command, quoted, yielding a negative result
            QString input = "write a32 d16 0x6070 \"$(-1 - 3 - 5)\"";
            ASSERT_THROW(parse(input), ParseError);
        }

        {
            // long form of the write command, unquoted
            QString input = "write a32 d16 0x6070 $(-1 - 3 - 5)";
            ASSERT_THROW(parse(input), ParseError);
        }

        {
            // short form of the write command
            QString input = "0x6070 $(-1 - 3 - 5)";
            ASSERT_THROW(parse(input), ParseError);
        }

    } catch (ParseError &e)
    {
        std::cout << e.toString().toStdString() << std::endl;
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

        QString input = "set foo \"$( 7 * 6 )\"\nset \"the_var\" ${foo}";
        try
        {
        auto script = parse(input, symtabs);
        } catch (const vme_script::ParseError &e)
        {
            qDebug() << e.toString();
            throw;
        }

        ASSERT_EQ(symtab0.value("foo").value, QSL("42"));
        ASSERT_EQ(symtab0.value("the_var").value, QSL("42"));
    }

    {
        SymbolTables symtabs;
        symtabs.resize(1);
        SymbolTable &symtab0 = symtabs[0];

        QString input = "set foo $( 7 * 6 )\nset \"the_var\" ${foo}";
        auto script = parse(input, symtabs);

        ASSERT_EQ(symtab0.value("foo").value, QSL("42"));
        ASSERT_EQ(symtab0.value("the_var").value, QSL("42"));
    }
}
