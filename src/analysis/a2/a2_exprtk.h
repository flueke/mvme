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

struct ParserErrorList: std::runtime_error
{
    std::vector<ParserError> errors;

    ParserErrorList(): std::runtime_error("ParserErrorList") {}
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

    bool addScalar(const std::string &name, double &value);
    bool addString(const std::string &name, std::string &str);
    bool addVector(const std::string &name, std::vector<double> &vec);
    bool addVector(const std::string &name, double *array, size_t size);
    bool addConstant(const std::string &name, double value); // TO

    bool createString(const std::string &name, const std::string &str);

    bool addConstants(); // pi, epsilon, inf

    // NOTE: There's currently no way to get back to the original std::vector
    // registered via addVector(), only the pointer and size can be queried.

    std::vector<std::string> getSymbolNames() const;
    bool symbolExists(const std::string &name) const;

    double *getScalar(const std::string &name);
    std::string *getString(const std::string &name);
    std::pair<double *, size_t> getVector(const std::string &name);
    //double getConstant(const std::string &name) const;

    /* Runtime library containing frequently used functions for use in expressions.
     * An instance of this will automatically be registered for expressions in
     * make_expression_operator().
     * Contains the following functions:
     * is_valid(p), is_invalid(p), make_invalid(), is_nan(d)
     */
    static SymbolTable makeA2RuntimeLibrary();

    private:
        friend class Expression;
        friend struct SymbolTableHelper;
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
        double eval() { return value(); }

        std::vector<Result> results();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // namespace a2_exprtk
} // namespace a2

#endif /* __A2_EXPRTK_H__ */
