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

struct ParserError: std::runtime_error
{
    std::string mode;
    std::string diagnostic;
    std::string src_location;
    std::string error_line;
    size_t line = 0, column = 0;

    ParserError(): std::runtime_error("ParserError") {}
};

class SymbolTable
{
    public:
#if 0
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
#endif

    SymbolTable();
    ~SymbolTable();

    SymbolTable(const SymbolTable &other);
    SymbolTable &operator=(const SymbolTable &other);

    std::vector<std::string> getSymbolNames() const;

    bool addScalar(const std::string &name, double &value);
    bool addString(const std::string &name, std::string &str);
    bool addVector(const std::string &name, std::vector<double> &vec);
    bool addVector(const std::string &name, double *array, size_t size);
    bool addConstant(const std::string &name, double value); // TO

    // NOTE: There's currently no way to get back to the original std::vector
    // registered via addVector().

    double *getScalar(const std::string &name);
    std::string *getString(const std::string &name);
    std::pair<double *, size_t> getVector(const std::string &name);
    //double getConstant(const std::string &name) const;

    private:
        friend class Expression;
        struct Private;
        std::unique_ptr<Private> m_d;
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
        double value();
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
