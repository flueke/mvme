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
    using Container = std::vector<ParserError>;

    Container errors;

    ParserErrorList(): std::runtime_error("ParserErrorList") {}

    Container::const_iterator begin() const { return errors.begin(); }
    Container::const_iterator end() const   { return errors.end(); }
};

struct SymbolError: public std::runtime_error
{
    enum class Reason
    {
        Unspecified,
        IsReservedSymbol,
        SymbolExists,
        IsZeroLengthArray,
    };

    explicit SymbolError(const std::string symbolName_ = {},
                         Reason reason_ = Reason::Unspecified)
        : std::runtime_error("SymbolError")
        , symbolName(symbolName_)
        , reason(reason_)
    { }

    std::string symbolName;
    Reason reason = Reason::Unspecified;
};

class SymbolTable
{
    public:
        /* If enableExceptions is true the addXYZ() and createString() methods will
         * throw an instance of SymbolError if registering the symbol fails.
         * Otherwise the methods will return false on error.
         * Note that addConstants() will not throw. */
        explicit SymbolTable(bool enableExceptions=true);
        ~SymbolTable();

        /* Uses the exprtk::symbol_table copy constructor internally, which
         * performs a reference counted shallow copy. Thus the same semantics as
         * for the exprtk symbol tables apply. */
        SymbolTable(const SymbolTable &other);
        SymbolTable &operator=(const SymbolTable &other);

        bool addScalar(const std::string &name, double &value);
        bool addString(const std::string &name, std::string &str);
        bool addVector(const std::string &name, std::vector<double> &vec);
        bool addVector(const std::string &name, double *array, size_t size);
        bool addConstant(const std::string &name, double value);

        bool createString(const std::string &name, const std::string &str);

        bool addConstants(); // pi, epsilon, inf

        /* NOTE: There's currently no way to get back to the original std::vector
         * registered via addVector(), only the pointer and size can be queried. */

        std::vector<std::string> getSymbolNames() const;
        bool symbolExists(const std::string &name) const;

        double *getScalar(const std::string &name);
        std::string *getString(const std::string &name);
        std::pair<double *, size_t> getVector(const std::string &name);

        using Function00 = double (*)();
        using Function01 = double (*)(double);
        using Function02 = double (*)(double, double);
        using Function03 = double (*)(double, double, double);

        bool addFunction(const std::string &name, Function00 f);
        bool addFunction(const std::string &name, Function01 f);
        bool addFunction(const std::string &name, Function02 f);
        bool addFunction(const std::string &name, Function03 f);

        static bool isReservedSymbol(const std::string &name);

    private:
        friend class Expression;
        friend class FunctionCompositor;
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

class FunctionCompositor
{
    public:
        FunctionCompositor();
        FunctionCompositor(const SymbolTable &symTab);
        ~FunctionCompositor();

        SymbolTable getSymbolTable() const;

        void addAuxiliarySymbolTable(const SymbolTable &auxSymbols);

        bool addFunction(const std::string &name, const std::string &expr,
                         const std::string &v0);

        bool addFunction(const std::string &name, const std::string &expr,
                         const std::string &v0, const std::string &v1);

        bool addFunction(const std::string &name, const std::string &expr,
                         const std::string &v0, const std::string &v1,
                         const std::string &v2);

        bool addFunction(const std::string &name, const std::string &expr,
                         const std::string &v0, const std::string &v1,
                         const std::string &v2, const std::string &v3);

        bool addFunction(const std::string &name, const std::string &expr,
                         const std::string &v0, const std::string &v1,
                         const std::string &v2, const std::string &v3,
                         const std::string &v4);

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // namespace a2_exprtk
} // namespace a2

#endif /* __A2_EXPRTK_H__ */
