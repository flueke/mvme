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
#include "a2.h"
#include "a2_impl.h"
#include "util/sizes.h"

#include <iostream>

using namespace a2;
using namespace memory;

using std::cout;
using std::endl;

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

inline void ASSERT_EQ_OR_NAN(double a, double b)
{
    if (std::isnan(a))
        ASSERT_TRUE(std::isnan(b));
    else
        ASSERT_EQ(a, b);
};

TEST(a2ExpressionOperator, PassThroughSingleInput)
{
    try
    {

        Arena arena(Kilobytes(256));

        static double inputData[] =
        {
            0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
            8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
        };

        static const s32 inputSize = ArrayCount(inputData);

        std::vector<PipeVectors> inputs = {
            {
                ParamVec{inputData, inputSize},
                push_param_vector(&arena, inputSize, 0.0),
                push_param_vector(&arena, inputSize, 20.0),
            },
        };

        std::vector<std::string> input_prefixes = { "input0" };
        std::vector<std::string> input_units    = { "apples" };
        std::vector<s32> input_param_indexes    = { NoParamIndex };

        std::string expr_begin =
            "return [ 'output0', input0.unit, input0.size, input0.lower_limits, input0.upper_limits ];";

        std::string expr_step =
            "output0 := input0;";

        auto op = make_expression_operator(
            &arena,
            inputs,
            input_param_indexes,
            input_prefixes,
            input_units,
            expr_begin,
            expr_step);

        auto d = reinterpret_cast<ExpressionOperatorData *>(op.d);

        // structure check
        ASSERT_EQ(d->output_names.size(), 1);
        ASSERT_EQ(d->output_names[0], "output0");

        ASSERT_EQ(d->output_units.size(), 1);
        ASSERT_EQ(d->output_units[0], "apples");

        ASSERT_EQ(op.outputCount, 1);

        ASSERT_EQ(op.outputs[0].size, inputSize);

        ASSERT_EQ(op.outputLowerLimits[0].size, inputSize);
        ASSERT_EQ(op.outputUpperLimits[0].size, inputSize);

        ASSERT_EQ(op.outputLowerLimits[0][0], 0.0);
        ASSERT_EQ(op.outputUpperLimits[0][0], 20.0);

        // step
        expression_operator_step(&op);

        // data check
        for (s32 i = 0; i < inputSize; i++)
        {
            ASSERT_EQ_OR_NAN(op.outputs[0][i], inputData[i]);
        }
    } catch (const ExpressionOperatorSemanticError &e)
    {
        std::cerr << e.message << endl;
        throw;
    } catch (const std::exception &e)
    {
        std::cerr << e.what() << endl;
        throw;
    }
}

TEST(a2ExpressionOperator, PassThroughTwoInputs)
{
    Arena arena(Kilobytes(256));

    static double inputData0[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };

    static const s32 inputSize0 = ArrayCount(inputData0);

    static double inputData1[] =
    {
        1.0, 2.0, 3.0,
    };

    static const s32 inputSize1 = ArrayCount(inputData1);

    std::vector<PipeVectors> inputs = {
        {
            ParamVec{inputData0, inputSize0},
            push_param_vector(&arena, inputSize0, 0.0),
            push_param_vector(&arena, inputSize0, 20.0),
        },
        {
            ParamVec{inputData1, inputSize1},
            push_param_vector(&arena, inputSize1, 0.0),
            push_param_vector(&arena, inputSize1, 5.0),
        },
    };

    std::vector<std::string> input_prefixes = { "input0", "input1" };
    std::vector<std::string> input_units    = { "apples", "oranges" };
    std::vector<s32> input_param_indexes    = { NoParamIndex, NoParamIndex };

    std::string expr_begin =
        "return ["
        "   'output0', input0.unit, input0.size, input0.lower_limits, input0.upper_limits,"
        "   'output1', input1.unit, input1.size, input1.lower_limits, input1.upper_limits"
        "];"
        ;

    std::string expr_step =
        "output0 := input0;\n"
        "output1 := input1;\n"
        ;

    auto op = make_expression_operator(
        &arena,
        inputs,
        input_param_indexes,
        input_prefixes,
        input_units,
        expr_begin,
        expr_step);

    auto d = reinterpret_cast<ExpressionOperatorData *>(op.d);

    // structure check
    ASSERT_EQ(d->output_names.size(), 2);
    ASSERT_EQ(d->output_names[0], "output0");
    ASSERT_EQ(d->output_names[1], "output1");

    ASSERT_EQ(d->output_units.size(), 2);
    ASSERT_EQ(d->output_units[0], "apples");
    ASSERT_EQ(d->output_units[1], "oranges");

    ASSERT_EQ(op.outputCount, 2);

    // outputs[0]
    ASSERT_EQ(op.outputLowerLimits[0].size, inputSize0);
    ASSERT_EQ(op.outputUpperLimits[0].size, inputSize0);

    ASSERT_EQ(op.outputLowerLimits[0][0], 0.0);
    ASSERT_EQ(op.outputUpperLimits[0][0], 20.0);

    // outputs[1]
    ASSERT_EQ(op.outputLowerLimits[1].size, inputSize1);
    ASSERT_EQ(op.outputUpperLimits[1].size, inputSize1);

    ASSERT_EQ(op.outputLowerLimits[1][0], 0.0);
    ASSERT_EQ(op.outputUpperLimits[1][0], 5.0);

    // step
    expression_operator_step(&op);

    // data check
    for (s32 i = 0; i < inputSize0; i++)
    {
        //cout << i << " " << op.outputs[0][i] << " " << inputData0[i] << endl;
        ASSERT_EQ_OR_NAN(op.outputs[0][i], inputData0[i]);
    }

    for (s32 i = 0; i < inputSize1; i++)
    {
        //cout << i << " " << op.outputs[1][i] << " " << inputData1[i] << endl;
        ASSERT_EQ_OR_NAN(op.outputs[1][i], inputData1[i]);
    }

#if 0
    cout << "Symbols in 'step' symbol table:" << endl;

    for (auto symbol: d->symtab_step.getSymbolNames())
    {
        cout << "  " << symbol << endl;
    }
#endif
}

TEST(a2ExpressionOperator, OutputSpecifications)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };

    static const s32 inputSize = ArrayCount(inputData);

    std::vector<PipeVectors> inputs = {
        {
            ParamVec{inputData, inputSize},
            push_param_vector(&arena, inputSize, 0.0),
            push_param_vector(&arena, inputSize, 20.0),
        },
    };

    std::vector<std::string> input_prefixes = { "input0" };
    std::vector<std::string> input_units    = { "apples" };
    std::vector<s32> input_param_indexes    = { NoParamIndex };

    std::string expr_step = "output0 := input0;";

    {
        // output limit sizes are different

        std::string expr_begin =
            "var output0.lower_limits[3];"
            "var output0.upper_limits[4];"
            "return [ 'output0', input0.unit, output0.lower_limits, output0.upper_limits ];";

        ASSERT_THROW(make_expression_operator(
                &arena,
                inputs,
                input_param_indexes,
                input_prefixes,
                input_units,
                expr_begin,
                expr_step),
            ExpressionOperatorSemanticError);
    }
}
