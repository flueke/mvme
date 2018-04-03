#ifndef __A2_EXPRTK_H__
#define __A2_EXPRTK_H__

#include "memory.h"

namespace a2
{

struct Operator;
struct ExpressionOperatorData;

struct ExpressionParserError
{
    std::string mode;
    std::string diagnostic;
    std::string src_location;
    std::string error_line;
    size_t line = 0, column = 0;
};

// TODO Split this into multiple steps:
// [creation], compile begin, compile step
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

void expr_destroy(ExpressionOperatorData *d);

void expr_eval_begin(ExpressionOperatorData *d);

void expr_eval_step(ExpressionOperatorData *d);

}; // namespace a2

#endif /* __A2_EXPRTK_H__ */
