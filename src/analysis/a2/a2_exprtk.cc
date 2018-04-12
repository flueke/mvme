#include "a2_exprtk.h"

#ifndef NDEBUG
#define DO_DEBUG_PRINTS
#endif

#ifndef NDEBUG
#define exprtk_disable_enhanced_features    // reduces compilation time at cost of runtime performance
#endif
#define exprtk_disable_rtl_io_file          // we do not ever want to use the fileio package
#include <exprtk/exprtk.hpp>

#include <iostream>
#include "util/assert.h"
#include "util/nan.h"

/* TODO list for a2_exprtk:
 * figure out if add_function() is needed, if so add an implementation to
   SymbolTable

 * understand why e_commutative_check behaves the way it does. see the note
   below CompileOptions for an explanation

 * implement double getConstant(const std::string &name) const; if needed and
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
{}

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
void Expression::compile()
{
    detail::SymbolTable symtab_globalConstants;
    symtab_globalConstants.add_constants(); // pi, epsilon, inf
    m_d->expression.register_symbol_table(symtab_globalConstants);

    detail::Parser parser(detail::CompileOptions);

    if (!parser.compile(m_d->expr_str, m_d->expression))
    {
        auto err = parser.get_error(0);
        exprtk::parser_error::update_error(err, m_d->expr_str);

#ifdef DO_DEBUG_PRINTS
        fprintf(stderr,
                "%s: begin_expr: #%lu errors, first error:\n"
                "  %s, line_num=%lu, col_num=%lu, line=%s\n",
                __PRETTY_FUNCTION__,
                parser.error_count(),
                parser.error().c_str(),
                err.line_no,
                err.column_no,
                err.error_line.c_str()
                );
#endif
        throw make_error(err);
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

#if 0
struct ExpressionOperatorData
{
    SymbolTable symtab_globalConstants; // pi, epsilon, inf
    SymbolTable symtab_globalFunctions; // is_valid(), is_invalid(), make_invalid()
    SymbolTable symtab_globalVariables; // runid
    SymbolTable symtab_begin;
    SymbolTable symtab_step;

    Expression expr_begin;
    Expression expr_step;

    Parser parser;

    ExpressionOperatorData()
        : parser(CompileOptions)
    {
        std::cout << __PRETTY_FUNCTION__ << "  " << this << std::endl;
    }

    ~ExpressionOperatorData()
    {
        std::cout << __PRETTY_FUNCTION__ << " " << this << std::endl;
    }
};

ExpressionParserError make_error(const exprtk::parser_error::type &error)
{
    ExpressionParserError result = {};

    result.mode         = exprtk::parser_error::to_str(error.mode);
    result.diagnostic   = error.diagnostic;
    result.src_location = error.src_location;
    result.error_line   = error.error_line;
    result.line         = error.line_no;
    result.column       = error.column_no;

    return result;
}

void expr_create(
    memory::Arena *arena,
    Operator *op,
    const std::string &begin_expr_str,
    const std::string &step_expr_str)
{
    assert(op->type == Operator_Expression);
    assert(op->inputCount == 1);

    auto d = arena->pushObject<ExpressionOperatorData>();
    op->d = d;

    d->symtab_globalConstants.add_constants();

    // TODO: add is_valid, is_invalid, make_invalid, make_nan, is_nan
    //d->symtab_globalFunctions.add_function();

    //
    // begin expression
    //

    do_and_assert(d->symtab_begin.add_vector(
            "input_lower_limits", op->inputLowerLimits[0].data, op->inputLowerLimits[0].size));

    do_and_assert(d->symtab_begin.add_vector(
            "input_upper_limits", op->inputUpperLimits[0].data, op->inputUpperLimits[0].size));

    d->expr_begin.register_symbol_table(d->symtab_begin);
    d->expr_begin.register_symbol_table(d->symtab_globalConstants);
    d->expr_begin.register_symbol_table(d->symtab_globalFunctions);
    d->expr_begin.register_symbol_table(d->symtab_globalVariables);

    if (!d->parser.compile(begin_expr_str, d->expr_begin))
    {
        auto err = d->parser.get_error(0);

        exprtk::parser_error::update_error(err, begin_expr_str);

        fprintf(stderr, "%s: begin_expr: #%lu errors: %s\n", __PRETTY_FUNCTION__,
                d->parser.error_count(),
                d->parser.error().c_str());
        throw make_error(err);
    }

    // evaluate the "begin" script
    d->expr_begin.value();

    // [SECTION 20 - EXPRESSION RETURN VALUES]
    typedef exprtk::results_context<double> results_context_t;
    typedef typename results_context_t::type_store_t type_t;
    typedef typename type_t::vector_view vector_t;

    //assert(d->expr_begin.results().count() == 2);
    const auto &results = d->expr_begin.results();
    assert(results[0].type == type_t::e_vector);
    assert(results[1].type == type_t::e_vector);

    vector_t outputLowerLimits(results[0]);
    vector_t outputUpperLimits(results[1]);

    assert(outputLowerLimits.size() > 0);
    assert(outputLowerLimits.size() == outputUpperLimits.size());

//#ifndef NDEBUG
#if 0
    for (size_t i = 0; i < outputLowerLimits.size(); i++)
    {
        fprintf(stderr, "%s, [%lu] [%lf, %lf)\n",
                __PRETTY_FUNCTION__,
                i,
                outputLowerLimits[i],
                outputUpperLimits[i]);
    }
#endif

    push_output_vectors(arena, op, 0, outputLowerLimits.size());

    for (size_t i = 0; i < outputLowerLimits.size(); i++)
    {
        op->outputLowerLimits[0][i] = outputLowerLimits[i];
        op->outputUpperLimits[0][i] = outputUpperLimits[i];
    }

    //
    // step expression
    //

    // input limits
    do_and_assert(d->symtab_step.add_vector(
            "input_lower_limits", op->inputLowerLimits[0].data, op->inputLowerLimits[0].size));

    do_and_assert(d->symtab_step.add_vector(
            "input_upper_limits", op->inputUpperLimits[0].data, op->inputUpperLimits[0].size));

    // output limits
    do_and_assert(d->symtab_step.add_vector(
            "output_lower_limits", op->outputLowerLimits[0].data, op->outputLowerLimits[0].size));

    do_and_assert(d->symtab_step.add_vector(
            "output_upper_limits", op->outputUpperLimits[0].data, op->outputUpperLimits[0].size));

    // input and output data arrays
    do_and_assert(d->symtab_step.add_vector(
            "input", op->inputs[0].data, op->inputs[0].size));

    do_and_assert(d->symtab_step.add_vector(
            "output", op->outputs[0].data, op->outputs[0].size));

    d->expr_step.register_symbol_table(d->symtab_step);
    d->expr_step.register_symbol_table(d->symtab_globalConstants);
    d->expr_step.register_symbol_table(d->symtab_globalFunctions);
    d->expr_step.register_symbol_table(d->symtab_globalVariables);

    if (!d->parser.compile(step_expr_str, d->expr_step))
    {
        auto err = d->parser.get_error(0);
        fprintf(stderr, "%s: step_expr: #%lu errors: %s\n", __PRETTY_FUNCTION__,
                d->parser.error_count(),
                d->parser.error().c_str());
        throw make_error(err);
    }
}

void expr_eval_step(ExpressionOperatorData *d)
{
    d->expr_step.value();
}
#endif

} // namespace a2_exprtk
} // namespace a2
