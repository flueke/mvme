#include "a2_exprtk.h"

#ifndef NDEBUG
#define exprtk_disable_enhanced_features    // reduces compilation time at cost of runtime performance
#endif
#define exprtk_disable_rtl_io_file          // we do not ever want to use the fileio package
#include <exprtk/exprtk.hpp>

#include <iostream>
#include "util/assert.h"

namespace a2
{
namespace a2_exprtk
{

/* TODO: make use of the lists of keywords and standard function in the
 * exprtk::detail namespace to check for name validity.  Or implement the
 * symbol table using a hidden private exprtk symbol_table instance instead of
 * the std::map. */
bool SymbolTable::add_scalar(const std::string &name, double &value)
{
    if (entries.find(name) != entries.end())
        return false;

    Entry entry  = {};
    entry.type   = Entry::Scalar;
    entry.scalar = &value;

    entries[name] = entry;

    return true;
}

bool SymbolTable::add_string(const std::string &name, std::string &str)
{
    if (entries.find(name) != entries.end())
        return false;

    Entry entry  = {};
    entry.type   = Entry::String;
    entry.string = &str;

    entries[name] = entry;

    return true;
}

bool SymbolTable::add_vector(const std::string &name, std::vector<double> &vec)
{
    if (entries.find(name) != entries.end())
        return false;

    Entry entry  = {};
    entry.type   = Entry::Vector;
    entry.vector = &vec;

    entries[name] = entry;

    return true;
}

bool SymbolTable::add_array(const std::string &name, double *array, size_t size)
{
    if (entries.find(name) != entries.end())
        return false;

    Entry entry  = {};
    entry.type   = Entry::Array;
    entry.array  = array;
    entry.size   = size;

    entries[name] = entry;

    return true;
}

bool SymbolTable::add_constant(const std::string &name, double value)
{
    if (entries.find(name) != entries.end())
        return false;

    Entry entry    = {};
    entry.type     = Entry::Constant;
    entry.constant = value;

    entries[name] = entry;

    return true;
}

namespace detail
{

typedef exprtk::symbol_table<double>    SymbolTable;
typedef exprtk::expression<double>      Expression;
typedef exprtk::parser<double>          Parser;
typedef Parser::settings_t              ParserSettings;
typedef exprtk::results_context<double> ResultsContext;

static const size_t CompileOptions = ParserSettings::e_replacer          +
                                   //ParserSettings::e_joiner            +
                                     ParserSettings::e_numeric_check     +
                                     ParserSettings::e_bracket_check     +
                                     ParserSettings::e_sequence_check    +
                                   //ParserSettings::e_commutative_check +
                                     ParserSettings::e_strength_reduction;

} // namespace detail

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
    m_d->expr_str= expr_str;
}

std::string Expression::getExpressionString() const
{
    return m_d->expr_str;
}

/* No error checking is done in here. Assumes all checks have been made
 * in SymbolTable::add_xyz! */
void Expression::registerSymbolTable(const SymbolTable &symtab)
{
    detail::SymbolTable tk_symtab;

    for (auto kv: symtab.entries)
    {
        auto &name  = kv.first;
        auto &entry = kv.second;

        using Entry = SymbolTable::Entry;

        switch (entry.type)
        {
            case Entry::Scalar:
                tk_symtab.add_variable(name, *entry.scalar);
                break;

            case Entry::String:
                tk_symtab.add_stringvar(name, *entry.string);
                break;

            case Entry::Vector:
                tk_symtab.add_vector(name, *entry.vector);
                break;
            case Entry::Array:
                tk_symtab.add_vector(name, entry.array, entry.size);
                break;

            case Entry::Constant:
                tk_symtab.add_constant(name, entry.constant);
                break;
        }
    }

    m_d->expression.register_symbol_table(tk_symtab);
}


/*  Register the expression local symbol table first, the constants and global tables after that:
    d->expr_begin.register_symbol_table(d->symtab_begin);
    d->expr_begin.register_symbol_table(d->symtab_globalConstants);
    d->expr_begin.register_symbol_table(d->symtab_globalFunctions);
    d->expr_begin.register_symbol_table(d->symtab_globalVariables);
*/
void Expression::compile()
{
    detail::SymbolTable symtab_globalConstants;
    symtab_globalConstants.add_constants();
    m_d->expression.register_symbol_table(symtab_globalConstants);

    detail::Parser parser(detail::CompileOptions);

    if (!parser.compile(m_d->expr_str, m_d->expression))
    {
        auto err = parser.get_error(0);
        exprtk::parser_error::update_error(err, m_d->expr_str);

        fprintf(stderr, "%s: begin_expr: #%lu errors: %s\n", __PRETTY_FUNCTION__,
                parser.error_count(),
                parser.error().c_str());
        throw make_error(err);
    }
}

double Expression::eval()
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
                    Result result;
                    result.type   = Result::String;
                    result.string = to_str(detail::ResultsContext::type_store_t::string_view(results[i]));
                } break;

            case detail::ResultsContext::type_store_t::e_vector:
                {
                    auto vv = detail::ResultsContext::type_store_t::vector_view(results[i]);
                    Result result;
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
