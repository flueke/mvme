#ifndef __A2_EXPRTK_H__
#define __A2_EXPRTK_H__

#include "a2.h"
#include "memory.h"

namespace a2
{

struct ExpressionData;

/* Assuming the operators inputs have been assigned before it is passed to expr_create().
 * Evaluates the begin_expr script to determine output size and limits.
 * Pushes the operators output pipe onto the arena.
 * Then creates an ExpressionData instance and assigns it to op->d.
 */
void expr_create(
    memory::Arena *arena,
    Operator *op,
    const std::string &begin_expr,
    const std::string &step_expr);

void expr_destroy(ExpressionData *d);

void expr_eval_begin(ExpressionData *d);

void expr_eval_step(ExpressionData *d);

}; // namespace a2

#endif /* __A2_EXPRTK_H__ */
