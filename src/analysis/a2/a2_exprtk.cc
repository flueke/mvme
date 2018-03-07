#include "a2_exprtk.h"
#include <exprtk/exprtk.hpp>
#include <iostream>

#include "a2_impl.h"
#include "util/assert.h"

namespace a2
{

typedef exprtk::symbol_table<double> symbol_table_t;
typedef exprtk::expression<double>     expression_t;
typedef exprtk::parser<double>             parser_t;

struct ExpressionData
{
    symbol_table_t symtab_begin;
    symbol_table_t symtab_step;
    expression_t expr_begin;
    expression_t expr_step;
    parser_t parser;

    ExpressionData()
    {
        std::cout << __PRETTY_FUNCTION__ << "  " << this << std::endl;
    }

    ~ExpressionData()
    {
        std::cout << __PRETTY_FUNCTION__ << " " << this << std::endl;
    }
};

void expr_create(
    memory::Arena *arena,
    Operator *op,
    const std::string &begin_expr_str,
    const std::string &step_expr_str)
{
    assert(op->type == Operator_Expression);
    assert(op->inputCount == 1);

    auto d = arena->pushObject<ExpressionData>();
    op->d = d;

    d->symtab_begin.add_constants();
    d->symtab_step.add_constants();

    do_and_assert(d->symtab_begin.add_vector(
            "input_lower_limits", op->inputLowerLimits[0].data, op->inputLowerLimits[0].size));

    do_and_assert(d->symtab_begin.add_vector(
            "input_upper_limits", op->inputUpperLimits[0].data, op->inputUpperLimits[0].size));

    d->expr_begin.register_symbol_table(d->symtab_begin);

    if (!d->parser.compile(begin_expr_str, d->expr_begin))
    {
        fprintf(stderr, "%s: begin_expr: %s\n", __PRETTY_FUNCTION__, d->parser.error().c_str());
        throw d->parser.error();
    }

    // evaluate the "begin" script
    d->expr_begin.value();

    // [SECTION 20 - EXPRESSION RETURN VALUES]
    typedef exprtk::results_context<double> results_context_t;
    typedef typename results_context_t::type_store_t type_t;
    typedef typename type_t::vector_view vector_t;

    assert(d->expr_begin.results().count() == 2);
    const auto &results = d->expr_begin.results();
    assert(results[0].type == type_t::e_vector);
    assert(results[1].type == type_t::e_vector);

    vector_t outputLowerLimits(results[0]);
    vector_t outputUpperLimits(results[1]);

    assert(outputLowerLimits.size() > 0);
    assert(outputLowerLimits.size() == outputUpperLimits.size());

//#ifndef NDEBUG
#if 0
    for (size_t i = 0; i < outputLowerLimits.size(); i++)
    {
        fprintf(stderr, "%s, [%lu] [%lf, %lf)\n",
                __PRETTY_FUNCTION__,
                i,
                outputLowerLimits[i],
                outputUpperLimits[i]);
    }
#endif

    push_output_vectors(arena, op, 0, outputLowerLimits.size());

    for (size_t i = 0; i < outputLowerLimits.size(); i++)
    {
        op->outputLowerLimits[0][i] = outputLowerLimits[i];
        op->outputUpperLimits[0][i] = outputUpperLimits[i];
    }

    // step expression symbol table setup

    // input limits
    do_and_assert(d->symtab_step.add_vector(
            "input_lower_limits", op->inputLowerLimits[0].data, op->inputLowerLimits[0].size));

    do_and_assert(d->symtab_step.add_vector(
            "input_upper_limits", op->inputUpperLimits[0].data, op->inputUpperLimits[0].size));

    // output limits
    do_and_assert(d->symtab_step.add_vector(
            "output_lower_limits", op->outputLowerLimits[0].data, op->outputLowerLimits[0].size));

    do_and_assert(d->symtab_step.add_vector(
            "output_upper_limits", op->outputUpperLimits[0].data, op->outputUpperLimits[0].size));

    // input and output data arrays
    do_and_assert(d->symtab_step.add_vector(
            "input", op->inputs[0].data, op->inputs[0].size));

    do_and_assert(d->symtab_step.add_vector(
            "output", op->outputs[0].data, op->outputs[0].size));

    d->expr_step.register_symbol_table(d->symtab_step);

    if (!d->parser.compile(step_expr_str, d->expr_step))
    {
        fprintf(stderr, "%s: step_expr: %s\n", __PRETTY_FUNCTION__, d->parser.error().c_str());
        throw d->parser.error();
    }
}

void expr_eval_step(ExpressionData *d)
{
    d->expr_step.value();
}

}; // namespace a2
