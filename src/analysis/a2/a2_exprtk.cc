#include "a2_exprtk.h"
#include "a2_impl.h"
#include <exprtk/exprtk.hpp>
#include <iostream>

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
        std::cout << __PRETTY_FUNCTION__ << ">>>> >>>> >>>> >>>>" << std::endl;
    }

    ~ExpressionData()
    {
        std::cout << __PRETTY_FUNCTION__ << "<<<< <<<< <<<< <<<<" << std::endl;
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

    d->symtab_begin.add_vector("input_lower_limits", op->inputLowerLimits[0].data, op->inputLowerLimits[0].size);
    d->symtab_begin.add_vector("input_upper_limits", op->inputUpperLimits[0].data, op->inputUpperLimits[0].size);

    d->expr_begin.register_symbol_table(d->symtab_begin);

    d->parser.compile(begin_expr_str, d->expr_begin);

    d->expr_begin.value();

    auto outputLowerLimits = d->symtab_begin.get_vector("lower_limits");
    auto outputUpperLimits = d->symtab_begin.get_vector("upper_limits");

    assert(outputLowerLimits);
    assert(outputUpperLimits);
    assert(outputLowerLimits->size() == outputUpperLimits->size());

    push_output_vectors(arena, op, 0, outputLowerLimits->size());

    for (size_t i = 0; i < outputLowerLimits->size(); i++)
    {
        op->outputLowerLimits[0][i] = outputLowerLimits->data()[i];
        op->outputUpperLimits[0][i] = outputUpperLimits->data()[i];
    }

    d->symtab_step.add_vector("input", op->inputs[0].data, op->inputs[0].size);
    d->symtab_step.add_vector("output", op->outputs[0].data, op->outputs[0].size);

    d->expr_step.register_symbol_table(d->symtab_step);

    d->parser.compile(step_expr_str, d->expr_step);
}

void expr_eval_step(ExpressionData *d)
{
    d->expr_step.value();
}

}; // namespace a2
