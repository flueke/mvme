#ifndef __A2_EXPRTK_H__
#define __A2_EXPRTK_H__

#include "a2.h"
#include "memory.h"

namespace a2
{

struct ExpressionData;

ExpressionData *expr_create(
    memory::Arena *arena,
    PipeVectors inPipe,
    const std::string &begin_expr,
    const std::string &step_expr);

void expr_destroy(ExpressionData *d);

void expr_eval_begin(ExpressionData *d);

void expr_eval_step(ExpressionData *d);

}; // namespace a2

#endif /* __A2_EXPRTK_H__ */
