#include "a2_exprtk.h"
#include <exprtk/exprtk.hpp>
#include <iostream>

namespace a2
{

struct ExpressionData
{
    ExpressionData()
    {
        std::cout << __PRETTY_FUNCTION__ << ">>>> >>>> >>>> >>>>" << std::endl;
    }

    ~ExpressionData()
    {
        std::cout << __PRETTY_FUNCTION__ << "<<<< <<<< <<<< <<<<" << std::endl;
    }
};

ExpressionData *expr_create(
    memory::Arena *arena,
    PipeVectors inPipe,
    const std::string &begin_expr,
    const std::string &step_expr)
{
    auto result = arena->pushStruct<ExpressionData>();
    return nullptr;
}

void expr_destroy(ExpressionData *d)
{
}

void expr_eval_begin(ExpressionData *d)
{
}

void expr_eval_step(ExpressionData *d)
{
}

}; // namespace a2
