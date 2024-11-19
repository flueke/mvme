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
#include "gtest/gtest.h"
#include "a2_exprtk.h"

#include <algorithm>
#include <cmath>
#include <iostream>

using std::cout;
using std::endl;

TEST(a2Exprtk, SymbolTableAddThings)
{
    using namespace a2::a2_exprtk;

    SymbolTable symtab;
    double scalar1 = 42.0;
    std::string string1 = "Hello, world!";
    std::vector<double> empty_vec;
    std::vector<double> filled_vec(10, 42.0);

    {
        auto var_names = symtab.getSymbolNames();
        ASSERT_TRUE(var_names.size() == 0);
    }

    {
        ASSERT_TRUE(symtab.addScalar("scalar1", scalar1));
        ASSERT_THROW(symtab.addScalar("scalar1", scalar1), SymbolError);
        auto var_names = symtab.getSymbolNames();
        ASSERT_EQ(var_names.size(), 1);
        ASSERT_TRUE(std::find(var_names.begin(), var_names.end(), "scalar1") != var_names.end());
    }

    {
        ASSERT_TRUE(symtab.addString("string1", string1));
        ASSERT_THROW(symtab.addScalar("scalar1", scalar1), SymbolError);
        ASSERT_THROW(symtab.addString("string1", string1), SymbolError);
        auto var_names = symtab.getSymbolNames();
        ASSERT_EQ(var_names.size(), 2);
        ASSERT_TRUE(std::find(var_names.begin(), var_names.end(), "scalar1") != var_names.end());
        ASSERT_TRUE(std::find(var_names.begin(), var_names.end(), "string1") != var_names.end());
    }

    {
        ASSERT_THROW(symtab.addVector("empty_vec", empty_vec), SymbolError);
        auto var_names = symtab.getSymbolNames();
        ASSERT_FALSE(std::find(var_names.begin(), var_names.end(), "vector1") != var_names.end());
    }

    {
        ASSERT_TRUE(symtab.addVector("vector1", filled_vec));
        ASSERT_THROW(symtab.addVector("vector1", filled_vec), SymbolError);

        auto var_names = symtab.getSymbolNames();
        ASSERT_EQ(var_names.size(), 3);
        ASSERT_TRUE(std::find(var_names.begin(), var_names.end(), "scalar1") != var_names.end());
        ASSERT_TRUE(std::find(var_names.begin(), var_names.end(), "string1") != var_names.end());
        ASSERT_TRUE(std::find(var_names.begin(), var_names.end(), "vector1") != var_names.end());
    }
}

TEST(a2Exprtk, SymbolTableCopyAndAssignAndGet)
{
    using namespace a2::a2_exprtk;

    double x = 42.0;
    std::string string1 = "Hello, world!";
    std::vector<double> filled_vec(10, 42.0);

    {
        SymbolTable src_symtab;

        src_symtab.addScalar("x",   x);
        src_symtab.addString("str", string1);
        src_symtab.addVector("vec", filled_vec);

        SymbolTable dst_symtab(src_symtab);

        ASSERT_NE(src_symtab.getScalar("x"), nullptr);
        ASSERT_EQ(src_symtab.getScalar("x"), dst_symtab.getScalar("x"));

        ASSERT_NE(src_symtab.getString("str"), nullptr);
        ASSERT_EQ(src_symtab.getString("str"), dst_symtab.getString("str"));

        ASSERT_NE(src_symtab.getVector("vec").first, nullptr);
        ASSERT_EQ(src_symtab.getVector("vec").first, dst_symtab.getVector("vec").first);
    }

    {
        SymbolTable src_symtab;

        src_symtab.addScalar("x",   x);
        src_symtab.addString("str", string1);
        src_symtab.addVector("vec", filled_vec);

        SymbolTable dst_symtab;

        dst_symtab = src_symtab;

        ASSERT_NE(src_symtab.getScalar("x"), nullptr);
        ASSERT_EQ(src_symtab.getScalar("x"), dst_symtab.getScalar("x"));

        ASSERT_NE(src_symtab.getString("str"), nullptr);
        ASSERT_EQ(src_symtab.getString("str"), dst_symtab.getString("str"));

        ASSERT_NE(src_symtab.getVector("vec").first, nullptr);
        ASSERT_EQ(src_symtab.getVector("vec").first, dst_symtab.getVector("vec").first);
    }

    // shallow copy test: modifications after copy construction will affect
    // both source and destination
    {
        SymbolTable src_symtab;

        src_symtab.addScalar("x",   x);

        SymbolTable dst_symtab(src_symtab);

        // Modify src_symtab after the copy
        src_symtab.addString("str", string1);

        ASSERT_NE(src_symtab.getScalar("x"), nullptr);
        ASSERT_EQ(src_symtab.getScalar("x"), dst_symtab.getScalar("x"));

        ASSERT_NE(src_symtab.getString("str"), nullptr);
        ASSERT_EQ(src_symtab.getString("str"), dst_symtab.getString("str"));
    }
}

TEST(a2Exprtk, ExpressionBasicEval)
{
    using namespace a2::a2_exprtk;

    {
        // undefined variable x
        Expression expr("3*x + 42");
        ASSERT_ANY_THROW(expr.compile());
    }

    {
        // internal variable definition
        Expression expr("var x := 5; 3*x + 42");
        expr.compile();
        ASSERT_EQ(expr.value(), (3 * 5 + 42));
    }

    {
        // internal variable
        Expression expr("var x := 5; 3*x + 42");
        expr.compile();
        ASSERT_EQ(expr.value(), (3 * 5 + 42));
        ASSERT_EQ(expr.results().size(), 0);
    }

    {
        // using a constant
        Expression expr("3*x + 42");
        SymbolTable symtab;
        symtab.addConstant("x", 5.0);
        expr.registerSymbolTable(symtab);
        expr.compile();
        ASSERT_EQ(expr.value(), (3 * 5 + 42));
        ASSERT_EQ(expr.results().size(), 0);
    }

    {
        // using an external variable
        Expression expr("3*x + 42");
        SymbolTable symtab;
        double x = 5.0;
        symtab.addScalar("x", x);
        expr.registerSymbolTable(symtab);
        expr.compile();
        ASSERT_EQ(expr.value(), (3 * 5 + 42));
        ASSERT_EQ(expr.results().size(), 0);
    }
}

TEST(a2Exprtk, ExpressionReturnResult)
{
    using namespace a2::a2_exprtk;

    Expression expr(
        "var d    := 42.0;"
        "var v[3] := { 1, 2, 3};"
        "var s    := 'Hello, World!';"

        "return [d, v, s];"
        );

    expr.compile();
    ASSERT_TRUE(std::isnan(expr.value()));
    auto results = expr.results();
    ASSERT_EQ(results.size(), 3);
    ASSERT_EQ(results[0].type, Expression::Result::Scalar);
    ASSERT_EQ(results[1].type, Expression::Result::Vector);
    ASSERT_EQ(results[2].type, Expression::Result::String);
}

TEST(a2Exprtk, VariableNames)
{
    using namespace a2::a2_exprtk;

    SymbolTable symtab;

    double x = 42;

    ASSERT_TRUE(symtab.addScalar("foo.bar", x));
    ASSERT_EQ(symtab.getScalar("foo.bar"), &x);

    ASSERT_THROW(symtab.addScalar("break", x), SymbolError);
    ASSERT_THROW(symtab.addScalar("endsWithDot.", x), SymbolError);
    ASSERT_THROW(symtab.addScalar(".startWithDot", x), SymbolError);

    Expression expr("foo.bar * 2");
    expr.registerSymbolTable(symtab);
    expr.compile();

    ASSERT_EQ(expr.value(), x * 2.0);
}

TEST(a2Exprtk, CreateStringVar)
{
    using namespace a2::a2_exprtk;

    SymbolTable symtab;

    ASSERT_TRUE(symtab.createString("str", "foobar"));
    ASSERT_NE(symtab.getString("str"), nullptr);
    ASSERT_EQ(*symtab.getString("str"), "foobar");
}
