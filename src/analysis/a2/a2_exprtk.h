#ifndef __A2_EXPRTK_H__
#define __A2_EXPRTK_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace a2
{
namespace a2_exprtk
{

struct ParserError
{
    std::string mode;
    std::string diagnostic;
    std::string src_location;
    std::string error_line;
    size_t line = 0, column = 0;
};

class SymbolTable
{
    public:
        struct Entry
        {
            enum Type
            {
                Scalar,
                String,
                Vector,
                Array,
                Constant
            };

            Type type;

            union
            {
                double *scalar;
                std::string *string;
                std::vector<double> *vector;

                struct
                {
                    double *array;
                    size_t size;
                };

                double constant;
            };
        };

    bool add_scalar(const std::string &name, double &value);
    bool add_string(const std::string &name, std::string &str);
    bool add_vector(const std::string &name, std::vector<double> &vec);
    bool add_array(const std::string &name, double *array, size_t size);
    bool add_constant(const std::string &name, double value);
    // TODO: add_function();
    // search for ff00_functor in exprtk.hpp and copy/pasta that code

    private:
        friend class Expression;
        std::map<std::string, Entry> entries;
};

class Expression
{
    public:
        struct Result
        {
            enum Type
            {
                Scalar,
                String,
                Vector
            };

            Type type;

            double scalar;
            std::string string;
            std::vector<double> vector;
        };

        Expression();
        explicit Expression(const std::string &expr_str);
        virtual ~Expression();

        void setExpressionString(const std::string &expr_str);
        std::string getExpressionString() const;

        void registerSymbolTable(const SymbolTable &symtab);

        void compile();
        double eval();
        std::vector<Result> results();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

#if 0
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

void expr_eval_begin(ExpressionOperatorData *d);

void expr_eval_step(ExpressionOperatorData *d);
#endif

} // namespace a2_exprtk
} // namespace a2

#endif /* __A2_EXPRTK_H__ */
