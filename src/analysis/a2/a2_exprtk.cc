/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "a2_exprtk.h"

#ifndef NDEBUG
//#define exprtk_enable_debugging             // verbose exprtk debug output
#define exprtk_disable_enhanced_features    // reduces compilation time at cost of runtime performance
#endif
#define exprtk_disable_rtl_io_file          // we do not ever want to use the fileio package
#include <exprtk/exprtk.hpp>

#include <iostream>

#include "a2_param.h"
#include "util/assert.h"

#ifndef NDEBUG

#define a2_expr_debug(fmt, ...)\
do\
{\
    fprintf(stderr, fmt, ##__VA_ARGS__);\
} while (0)
#else  // NDEBUG

#define a2_expr_debug(fmt, ...)

#endif // NDEBUG


namespace a2
{
namespace a2_exprtk
{

namespace detail
{

typedef exprtk::symbol_table<double>        SymbolTable;
typedef exprtk::expression<double>          Expression;
typedef exprtk::parser<double>              Parser;
typedef Parser::settings_t                  ParserSettings;
typedef exprtk::results_context<double>     ResultsContext;
typedef exprtk::function_compositor<double> Compositor;
typedef typename Compositor::function       CompositorFunction;

static const size_t CompileOptions = ParserSettings::e_replacer          +
                                     // Joins things like "< =" to become "<="
                                     //ParserSettings::e_joiner            +

                                     ParserSettings::e_numeric_check     +
                                     ParserSettings::e_bracket_check     +
                                     ParserSettings::e_sequence_check    +

                                     // "3x" -> "3*x"
                                     //ParserSettings::e_commutative_check +
                                     ParserSettings::e_strength_reduction;

/* Note: the commutative check also affects things that might be surprising like e.g.
 *
 *   for (var i := 0; i < 10; i += 1) { }
 *   var x := 0;
 *
 * will result in an "Invalid assignment operation error". The correct way to
 * write this would be to add a semicolon after the for-loop body.  Disabling
 * the commutative check makes the above code work, while at the same time
 * being more "C-like".
 */

template<typename T>
TypeStore to_typestore(const typename exprtk::igeneric_function<T>::generic_type &gt)
{
    using generic_type = typename exprtk::igeneric_function<T>::generic_type;
    typedef typename generic_type::scalar_view scalar_t;
    typedef typename generic_type::vector_view vector_t;
    typedef typename generic_type::string_view string_t;

    TypeStore result = {};

    switch (gt.type)
    {
        case generic_type::e_scalar:
            {
                result.type   = TypeStore::Scalar;
                result.scalar = scalar_t(gt)();
            } break;

        case generic_type::e_string:
            {
                result.type   = TypeStore::String;
                result.string = to_str(string_t(gt));
            } break;

        case generic_type::e_vector:
            {
                auto vv = vector_t(gt);
                result.type   = TypeStore::Vector;
                result.vector = std::vector<double>(vv.begin(), vv.end());
            } break;

        case generic_type::e_unknown:
        default:
            InvalidCodePath;
    }

    return result;
}

template<typename T>
struct GenericFunctionAdapter: public exprtk::igeneric_function<T>
{
    using parameter_list_t = typename exprtk::igeneric_function<T>::parameter_list_t;
    using generic_type = typename exprtk::igeneric_function<T>::generic_type;

    GenericFunctionAdapter(GenericFunction &f)
        : exprtk::igeneric_function<T>(f.getParameterSequence())
        , f_(f)
    {
    }

    // This is the overload called by exprtk when the parameter sequence string
    // is empty.
    inline T operator() (parameter_list_t params)
    {
        //std::cout << __PRETTY_FUNCTION__ << " " << params.size() << std::endl;

        GenericFunction::ParameterList adaptedParams;

        for (std::size_t i = 0; i < params.size(); ++i)
            adaptedParams.emplace_back(to_typestore<double>(params[i]));

        return f_(adaptedParams, 0);
    }

    // This is the overload called by exprtk when a parameter sequence string
    // is given.
    // f(psi,i_0,i_1,....,i_N) --> Scalar
    // psi is the 'param seq index' which is the index of the parameter
    // sequence alternative parsed by exprtk.
    inline T operator() (const std::size_t& psi, parameter_list_t params)
    {
        //std::cout << __PRETTY_FUNCTION__ << params.size() << std::endl;
        //std::cout << __PRETTY_FUNCTION__ << "psi=" << psi << std::endl;

        GenericFunction::ParameterList adaptedParams;

        for (std::size_t i = 0; i < params.size(); ++i)
            adaptedParams.emplace_back(to_typestore<double>(params[i]));

        return f_(adaptedParams, psi);
    }

    GenericFunction &f_;
};

} // namespace detail

struct SymbolTable::Private
{
    bool enableExceptions;
    detail::SymbolTable symtab_impl;
    std::vector<detail::GenericFunctionAdapter<double>> functionAdapters_;
};

SymbolTable::SymbolTable(bool enableExceptions)
    : m_d(std::make_unique<Private>())
{
    m_d->enableExceptions = enableExceptions;
}

SymbolTable::~SymbolTable()
{
    //std::cout << __PRETTY_FUNCTION__ << std::endl;
}

SymbolTable::SymbolTable(const SymbolTable &other)
    : m_d(std::make_unique<Private>())
{
    m_d->enableExceptions = other.m_d->enableExceptions;

    // exprtk uses a reference counted shallow copy implementation
    m_d->symtab_impl = other.m_d->symtab_impl;
}

SymbolTable &SymbolTable::operator=(const SymbolTable &other)
{
    if (this != &other)
    {
        m_d->enableExceptions = other.m_d->enableExceptions;
        m_d->symtab_impl = other.m_d->symtab_impl;
    }
    return *this;
}

bool SymbolTable::addScalar(const std::string &name, double &value)
{
    bool result = m_d->symtab_impl.add_variable(name, value);

    if (!result && m_d->enableExceptions)
    {
        SymbolError error(name);

        if (SymbolTable::isReservedSymbol(name))
            error.reason = SymbolError::Reason::IsReservedSymbol;
        else if (symbolExists(name))
            error.reason = SymbolError::Reason::SymbolExists;

        throw error;
    }

    return result;
}

bool SymbolTable::addString(const std::string &name, std::string &str)
{
    bool result = m_d->symtab_impl.add_stringvar(name, str);

    if (!result && m_d->enableExceptions)
    {
        SymbolError error(name);

        if (SymbolTable::isReservedSymbol(name))
            error.reason = SymbolError::Reason::IsReservedSymbol;
        else if (symbolExists(name))
            error.reason = SymbolError::Reason::SymbolExists;

        throw error;
    }

    return result;
}

bool SymbolTable::addVector(const std::string &name, std::vector<double> &vec)
{
    bool result = m_d->symtab_impl.add_vector(name, vec);

    if (!result && m_d->enableExceptions)
    {
        SymbolError error(name);

        if (SymbolTable::isReservedSymbol(name))
            error.reason = SymbolError::Reason::IsReservedSymbol;
        else if (symbolExists(name))
            error.reason = SymbolError::Reason::SymbolExists;
        else if (vec.size() == 0)
            error.reason = SymbolError::Reason::IsZeroLengthArray;

        throw error;
    }

    return result;
}

bool SymbolTable::addVector(const std::string &name, double *array, size_t size)
{
    bool result = m_d->symtab_impl.add_vector(name, array, size);

    if (!result && m_d->enableExceptions)
    {
        SymbolError error(name);

        if (SymbolTable::isReservedSymbol(name))
            error.reason = SymbolError::Reason::IsReservedSymbol;
        else if (symbolExists(name))
            error.reason = SymbolError::Reason::SymbolExists;
        else if (size == 0)
            error.reason = SymbolError::Reason::IsZeroLengthArray;

        throw error;
    }

    return result;
}

bool SymbolTable::addConstant(const std::string &name, double value)
{
    bool result = m_d->symtab_impl.add_constant(name, value);

    if (!result && m_d->enableExceptions)
    {
        SymbolError error(name);

        if (SymbolTable::isReservedSymbol(name))
            error.reason = SymbolError::Reason::IsReservedSymbol;
        else if (symbolExists(name))
            error.reason = SymbolError::Reason::SymbolExists;

        throw error;
    }

    return result;
}

bool SymbolTable::createString(const std::string &name, const std::string &str)
{
    bool result = m_d->symtab_impl.create_stringvar(name, str);

    if (!result && m_d->enableExceptions)
    {
        SymbolError error(name);

        if (SymbolTable::isReservedSymbol(name))
            error.reason = SymbolError::Reason::IsReservedSymbol;
        else if (symbolExists(name))
            error.reason = SymbolError::Reason::SymbolExists;

        throw error;
    }

    return result;
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

bool SymbolTable::addFunction(const std::string &name, Function00 f)
{
    return m_d->symtab_impl.add_function(name, f);
}

bool SymbolTable::addFunction(const std::string &name, Function01 f)
{
    return m_d->symtab_impl.add_function(name, f);
}

bool SymbolTable::addFunction(const std::string &name, Function02 f)
{
    return m_d->symtab_impl.add_function(name, f);
}

bool SymbolTable::addFunction(const std::string &name, Function03 f)
{
    return m_d->symtab_impl.add_function(name, f);
}

bool SymbolTable::addFunction(const std::string &name, GenericFunction &f)
{
    m_d->functionAdapters_.emplace_back(detail::GenericFunctionAdapter<double>(f));
    return m_d->symtab_impl.add_function(name, m_d->functionAdapters_.back());
}

bool SymbolTable::isReservedSymbol(const std::string &name)
{
    return exprtk::details::is_reserved_symbol(name);
}

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
        ret.push_back(detail::to_typestore<double>(results[i]));

    return ret;
}

struct FunctionCompositor::Private
{
    Private()
    {}

    explicit Private(const SymbolTable &symTab)
        : compositor_impl(symTab.m_d->symtab_impl)
    {}

    SymbolTable symbolTable;
    detail::Compositor compositor_impl;
};

FunctionCompositor::FunctionCompositor()
    : m_d(std::make_unique<Private>())
{}

FunctionCompositor::FunctionCompositor(const SymbolTable &symTab)
    : m_d(std::make_unique<Private>(symTab))
{}

FunctionCompositor::~FunctionCompositor()
{}

SymbolTable FunctionCompositor::getSymbolTable() const
{
    return m_d->symbolTable;
}

void FunctionCompositor::addAuxiliarySymbolTable(const SymbolTable &auxSymbols)
{
    m_d->compositor_impl.add_auxiliary_symtab(auxSymbols.m_d->symtab_impl);
}

bool FunctionCompositor::addFunction(const std::string &name, const std::string &expr,
                 const std::string &v0)
{
    bool result = m_d->compositor_impl.add(
        detail::CompositorFunction(name, expr, v0)
        );

    return result;
}

bool FunctionCompositor::addFunction(const std::string &name, const std::string &expr,
                 const std::string &v0, const std::string &v1)
{
    bool result = m_d->compositor_impl.add(
        detail::CompositorFunction(name, expr, v0, v1)
        );

    return result;
}

bool FunctionCompositor::addFunction(const std::string &name, const std::string &expr,
                 const std::string &v0, const std::string &v1,
                 const std::string &v2)
{
    bool result = m_d->compositor_impl.add(
        detail::CompositorFunction(name, expr, v0, v1, v2)
        );

    return result;
}

bool FunctionCompositor::addFunction(const std::string &name, const std::string &expr,
                 const std::string &v0, const std::string &v1,
                 const std::string &v2, const std::string &v3)
{
    bool result = m_d->compositor_impl.add(
        detail::CompositorFunction(name, expr, v0, v1, v2, v3)
        );

    return result;
}

bool FunctionCompositor::addFunction(const std::string &name, const std::string &expr,
                 const std::string &v0, const std::string &v1,
                 const std::string &v2, const std::string &v3,
                 const std::string &v4)
{
    bool result = m_d->compositor_impl.add(
        detail::CompositorFunction(name, expr, v0, v1, v2, v3, v4)
        );

    return result;
}

} // namespace a2_exprtk
} // namespace a2
