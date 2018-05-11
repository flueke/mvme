#include "a2_exprtk.h"

#ifndef NDEBUG
#define exprtk_disable_enhanced_features    // reduces compilation time at cost of runtime performance
#endif
#define exprtk_disable_rtl_io_file          // we do not ever want to use the fileio package
#include <exprtk/exprtk.hpp>

#include <iostream>

#include "a2_param.h"
#include "util/assert.h"
#include "util/nan.h"

#ifndef NDEBUG

#define a2_expr_debug(fmt, ...)\
do\
{\
    fprintf(stderr, fmt, ##__VA_ARGS__);\
} while (0)
#else  // NDEBUG

#define a2_expr_debug(fmt, ...)

#endif // NDEBUG


/* TODO list for a2_exprtk:
 * figure out if add_function() is needed, if so add an implementation to
   SymbolTable
   add_functions() is not easy to hide if it's at all possible:
   registering classes derived from exprtk::ifunction<T> requires an instance
   and that instance needs to stay around. Also acces to exprtk internals is
   needed.
   Lambdas can not be registed without implementation access either as that
   requires the register functions to be a template and the implementation to
   be visible.

 * understand why e_commutative_check behaves the way it does. see the note
   below CompileOptions for an explanation

 * implement "double getConstant(const std::string &name) const" if needed and
   possible

 */

namespace a2
{
namespace a2_exprtk
{

namespace detail
{

typedef exprtk::symbol_table<double>    SymbolTable;
typedef exprtk::expression<double>      Expression;
typedef exprtk::parser<double>          Parser;
typedef Parser::settings_t              ParserSettings;
typedef exprtk::results_context<double> ResultsContext;

static const size_t CompileOptions = ParserSettings::e_replacer          +
                                   //ParserSettings::e_joiner            + // Joins things like "< =" to become "<="
                                     ParserSettings::e_numeric_check     +
                                     ParserSettings::e_bracket_check     +
                                     ParserSettings::e_sequence_check    +
                                     ParserSettings::e_commutative_check + // "3x" -> "3*x"
                                     ParserSettings::e_strength_reduction;

/* NOTE: I wanted to disable e_commutative_check but when doing so the
 * expression "3x+42" does not result in a parser error but instead behaves
 * like "x+42". I haven't tested the behaviour extensively yet as what I really
 * want is a parser error to be thrown. */

} // namespace detail

struct SymbolTable::Private
{
    detail::SymbolTable symtab_impl;
};

SymbolTable::SymbolTable()
    : m_d(std::make_unique<Private>())
{}

SymbolTable::~SymbolTable()
{
    //std::cout << __PRETTY_FUNCTION__ << std::endl;
}

SymbolTable::SymbolTable(const SymbolTable &other)
    : m_d(std::make_unique<Private>())
{
    // Reference counted shallow copy implementation
    m_d->symtab_impl = other.m_d->symtab_impl;
}

SymbolTable &SymbolTable::operator=(const SymbolTable &other)
{
    m_d->symtab_impl = other.m_d->symtab_impl;
    return *this;
}

bool SymbolTable::addScalar(const std::string &name, double &value)
{
    return m_d->symtab_impl.add_variable(name, value);
}

bool SymbolTable::addString(const std::string &name, std::string &str)
{
    return m_d->symtab_impl.add_stringvar(name, str);
}

bool SymbolTable::addVector(const std::string &name, std::vector<double> &vec)
{
    return m_d->symtab_impl.add_vector(name, vec);
}

bool SymbolTable::addVector(const std::string &name, double *array, size_t size)
{
    return m_d->symtab_impl.add_vector(name, array, size);
}

bool SymbolTable::addConstant(const std::string &name, double value)
{
    return m_d->symtab_impl.add_constant(name, value);
}

bool SymbolTable::createString(const std::string &name, const std::string &str)
{
    return m_d->symtab_impl.create_stringvar(name, str);
}

bool SymbolTable::addConstants()
{
    return m_d->symtab_impl.add_constants();
}

std::vector<std::string> SymbolTable::getSymbolNames() const
{
    std::vector<std::string> result;
    m_d->symtab_impl.get_variable_list(result);
    m_d->symtab_impl.get_stringvar_list(result);
    m_d->symtab_impl.get_vector_list(result);
    return result;
}

bool SymbolTable::symbolExists(const std::string &name) const
{
    return m_d->symtab_impl.symbol_exists(name);
}

double *SymbolTable::getScalar(const std::string &name)
{
    if (auto var_ptr = m_d->symtab_impl.get_variable(name))
    {
        return &var_ptr->ref();
    }

    return nullptr;
}

std::string *SymbolTable::getString(const std::string &name)
{
    if (auto var_ptr = m_d->symtab_impl.get_stringvar(name))
    {
        return &var_ptr->ref();
    }

    return nullptr;
}

std::pair<double *, size_t> SymbolTable::getVector(const std::string &name)
{
    if (auto var_ptr = m_d->symtab_impl.get_vector(name))
    {
        return std::make_pair(var_ptr->data(), var_ptr->size());
    }

    return std::pair<double *, size_t>(nullptr, 0);
}

SymbolTable SymbolTable::makeA2RuntimeLibrary()
{
    SymbolTable result;

    result.m_d->symtab_impl.add_function("is_valid",
                                         [](double p) { return static_cast<double>(is_param_valid(p)); });

    result.m_d->symtab_impl.add_function("is_invalid",
                                         [](double p) { return static_cast<double>(!is_param_valid(p)); });

    result.m_d->symtab_impl.add_function("make_invalid", invalid_param);

    result.m_d->symtab_impl.add_function("is_nan",
                                         [](double d) { return static_cast<double>(std::isnan(d)); });

    return result;
}

#if 0
double SymbolTable::getConstant(const std::string &name) const
{
    assert(false);
    return make_quiet_nan();
}
#endif

namespace
{

ParserError make_error(const exprtk::parser_error::type &error)
{
    ParserError result  = {};
    result.mode         = exprtk::parser_error::to_str(error.mode);
    result.diagnostic   = error.diagnostic;
    result.src_location = error.src_location;
    result.error_line   = error.error_line;
    result.line         = error.line_no;
    result.column       = error.column_no;
    return result;
}

} // anon namespace

struct Expression::Private
{
    std::string expr_str;
    detail::Expression expression;
};

Expression::Expression()
    : m_d(std::make_unique<Private>())
{
}

Expression::Expression(const std::string &expr_str)
    : Expression()
{
    setExpressionString(expr_str);
}

Expression::~Expression()
{
    //std::cout << __PRETTY_FUNCTION__ << std::endl;
}

void Expression::setExpressionString(const std::string &expr_str)
{
    m_d->expr_str = expr_str;
}

std::string Expression::getExpressionString() const
{
    return m_d->expr_str;
}

void Expression::registerSymbolTable(const SymbolTable &symtab)
{
    m_d->expression.register_symbol_table(symtab.m_d->symtab_impl);
}

/*  Symbol table hierarchies: register the expression local symbol table first,
 *  the constants and global tables after that:

    d->expr_begin.register_symbol_table(d->symtab_begin);
    d->expr_begin.register_symbol_table(d->symtab_globalConstants);
    d->expr_begin.register_symbol_table(d->symtab_globalFunctions);
    d->expr_begin.register_symbol_table(d->symtab_globalVariables);
*/
/** Registers the constants symbol table (pi, epsilon, inf) and compiles the
 * expression.
 * Throws ParserErrorList on error.
 */
void Expression::compile()
{
    detail::SymbolTable symtab_globalConstants;
    symtab_globalConstants.add_constants(); // pi, epsilon, inf
    m_d->expression.register_symbol_table(symtab_globalConstants);

    detail::Parser parser(detail::CompileOptions);

    if (!parser.compile(m_d->expr_str, m_d->expression))
    {
        ParserErrorList errorList;

        a2_expr_debug("Expression::compile(): #%lu errors:\n", parser.error_count());

        for (size_t err_idx = 0; err_idx < parser.error_count(); err_idx++)
        {
            auto err = parser.get_error(err_idx);
            exprtk::parser_error::update_error(err, m_d->expr_str);
            errorList.errors.emplace_back(make_error(err));

            a2_expr_debug("  msg=%s, line_num=%lu, col_num=%lu, line=%s\n",
                          parser.error().c_str(),
                          err.line_no,
                          err.column_no,
                          err.error_line.c_str()
                         );
        }

        throw errorList;
    }
}

double Expression::value()
{
    return m_d->expression.value();
}

std::vector<Expression::Result> Expression::results()
{
    std::vector<Result> ret;

    const auto &results = m_d->expression.results();

    for (size_t i = 0; i < results.count(); i++)
    {
        Result result = {};

        switch (results[i].type)
        {
            case detail::ResultsContext::type_store_t::e_scalar:
                {
                    result.type   = Result::Scalar;
                    result.scalar = detail::ResultsContext::type_store_t::scalar_view(results[i])();
                } break;

            case detail::ResultsContext::type_store_t::e_string:
                {
                    result.type   = Result::String;
                    result.string = to_str(detail::ResultsContext::type_store_t::string_view(results[i]));
                } break;

            case detail::ResultsContext::type_store_t::e_vector:
                {
                    auto vv = detail::ResultsContext::type_store_t::vector_view(results[i]);
                    result.type   = Result::Vector;
                    result.vector = std::vector<double>(vv.begin(), vv.end());
                } break;

            case detail::ResultsContext::type_store_t::e_unknown:
                InvalidCodePath;
        }

        ret.push_back(result);
    }

    return ret;
}

} // namespace a2_exprtk
} // namespace a2
